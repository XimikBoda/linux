/*
 * MT62XX/MT6260 LCD Framebuffer Driver
 *
 * Drives ST7789S or compatible panel via 8-bit parallel (Intel 8080 style)
 * interface using direct CPU writes to the parallel port.
 *
 * Framebuffer resolution and refresh rate are configurable via kernel cmdline:
 *   mt62xx_fb.width=240  (default 240)
 *   mt62xx_fb.height=320 (default 320)
 *   mt62xx_fb.fps=15     (default 15)
 *
 * Framebuffer memory: dynamically allocated with kmalloc (order-N page
 * allocation under the hood). For 240x320x16bpp = 150KB this is a single
 * order-6 buddy allocation (256KB) with the tail trimmed by the slab
 * allocator. Lowest practical pressure for a dynamic allocation.
 *
 * Data written to the buffer is NOT sent directly to LCD (no DMA).
 * A periodic work queue reads from the buffer and writes pixel data
 * to the LCD parallel port manually.
 *
 * Write width rules (from proven working devmem sequence):
 *   - Commands and parameters: writew (16-bit) to PARALLEL0_A0_LOW/HIGH
 *   - Pixel data after RAMWR:  writeb (8-bit) to PARALLEL0_A0_HIGH
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include "mt62xx-fb.h"

#define DRIVER_NAME "mt62xx-fb"

/* Default framebuffer parameters (overridable via kernel cmdline) */
#define DEFAULT_WIDTH   240
#define DEFAULT_HEIGHT  320
#define DEFAULT_FPS     15

static int fb_width = DEFAULT_WIDTH;
static int fb_height = DEFAULT_HEIGHT;
static int fb_fps = DEFAULT_FPS;

module_param_named(width, fb_width, int, 0444);
MODULE_PARM_DESC(width, "Framebuffer width in pixels (default: 240)");
module_param_named(height, fb_height, int, 0444);
MODULE_PARM_DESC(height, "Framebuffer height in pixels (default: 320)");
module_param_named(fps, fb_fps, int, 0444);
MODULE_PARM_DESC(fps, "Framebuffer refresh rate in Hz (default: 15)");

#define LCD_BPP         16
#define LCD_FB_SIZE(w, h)   ((w) * (h) * LCD_BPP / 8)
#define LCD_FB_PIXELS(w, h) ((w) * (h))

/* Delay constants (milliseconds) */
#define SLEEP_EXIT_DELAY_MS     120
#define DISPLAY_ON_DELAY_MS     120
#define DISPLAY_BLANK_DELAY_MS  20
#define CLOCK_STABLE_DELAY_US   10
#define SOFT_RESET_DELAY_US     10
#define BACKLIGHT_STABLE_US     100

/* Number of backlight ISINK channels to enable */
#define BACKLIGHT_NUM_CHANNELS  3

/* MADCTL value: normal orientation, no rotation/mirror */
#define MADCTL_NORMAL           0x00

struct mt62xx_fb_info {
	struct fb_info *info;
	struct platform_device *pdev;
	void __iomem *lcd_base;
	void __iomem *gpio_base;
	void __iomem *config_base;
	void __iomem *isink_base;
	void __iomem *pmu_base;

	/* Framebuffer -- dynamically allocated kernel memory */
	void *fb_virt;               /* kmalloc'd virtual address */
	unsigned long fb_phys;       /* physical address (for info only) */
	int width;
	int height;
	int fps;
	unsigned long fb_size;

	bool panel_initialized;
	struct delayed_work update_work;
};

/* ============================================================================
 * Parallel Port Write
 *
 * Working devmem uses 16-bit writes for commands/params, 8-bit for pixels.
 * LCD_PARALLEL0_A0_LOW  = command register (A0=0)
 * LCD_PARALLEL0_A0_HIGH = data register (A0=1)
 * ============================================================================ */

static inline void lcd_cmd(struct mt62xx_fb_info *par, u16 cmd)
{
	writew(cmd, par->lcd_base + LCD_PARALLEL0_A0_LOW);
}

static inline void lcd_data(struct mt62xx_fb_info *par, u16 data)
{
	writew(data, par->lcd_base + LCD_PARALLEL0_A0_HIGH);
}

static inline void lcd_pixel_byte(struct mt62xx_fb_info *par, u8 byte)
{
	writeb(byte, par->lcd_base + LCD_PARALLEL0_A0_HIGH);
}

/* ============================================================================
 * Clock Gate: write to DRVPDN_MCUSYS_PDN_CON0_CLR to enable LCD clocks
 * ============================================================================ */

static void lcd_clock_enable(struct mt62xx_fb_info *par)
{
	u32 before, after;

	before = readl_relaxed(par->config_base + DRVPDN_MCUSYS_PDN_CON0);
	writel_relaxed(DRVPDN_CON0_LCD_GATE_BITS,
		       par->config_base + DRVPDN_MCUSYS_PDN_CON0_CLR);
	udelay(CLOCK_STABLE_DELAY_US);
	after = readl_relaxed(par->config_base + DRVPDN_MCUSYS_PDN_CON0);

	dev_info(&par->pdev->dev, "clock: PDN_CON0 0x%08x -> 0x%08x\n",
		 before, after);
}

/* ============================================================================
 * LCD Controller Soft Reset
 * Toggle LCD_START register: 0 -> 1 -> 0
 * ============================================================================ */

static void lcd_soft_reset(struct mt62xx_fb_info *par)
{
	dev_info(&par->pdev->dev, "reset: LCD_START toggle (0->1->0)\n");
	writew(0, par->lcd_base + LCD_START);
	writew(LCD_START_RESET, par->lcd_base + LCD_START);
	writew(0, par->lcd_base + LCD_START);
	udelay(SOFT_RESET_DELAY_US);
}

/* ============================================================================
 * Panel Init -- matches working devmem sequence EXACTLY:
 *   SLPOUT -> delay -> DISPON -> delay -> PDW -> MADCTL -> COLMOD
 * No gamma, no power registers -- the panel retains MAUI's settings.
 * ============================================================================ */

static void lcd_panel_init(struct mt62xx_fb_info *par)
{
	u32 par0_read, pdw_after;

	/* Read PARALLEL0_CONFIG -- verify MAUI left correct timing */
	par0_read = readl_relaxed(par->lcd_base + LCD_PARALLEL0_CONFIG);
	dev_info(&par->pdev->dev, "panel: PARALLEL0_CONFIG read = 0x%08x\n",
		 par0_read);

	/* Soft reset */
	lcd_soft_reset(par);

	/* SLPOUT (exit sleep mode) */
	dev_info(&par->pdev->dev, "panel: SLPOUT (exit sleep)\n");
	lcd_cmd(par, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(SLEEP_EXIT_DELAY_MS);

	/* DISPON (display on) */
	dev_info(&par->pdev->dev, "panel: DISPON (display on)\n");
	lcd_cmd(par, MIPI_DCS_SET_DISPLAY_ON);
	msleep(DISPLAY_ON_DELAY_MS);

	/* Set PARALLEL_PDW to MAUI's working value */
	writel_relaxed(PAR_PDW_MAUI_DEFAULT, par->lcd_base + LCD_PARALLEL_PDW);
	pdw_after = readl_relaxed(par->lcd_base + LCD_PARALLEL_PDW);
	dev_info(&par->pdev->dev, "panel: PARALLEL_PDW = 0x%08x\n", pdw_after);

	/* MADCTL (memory access control) -- normal orientation */
	lcd_cmd(par, MIPI_DCS_SET_ADDRESS_MODE);
	lcd_data(par, MADCTL_NORMAL);

	/* COLMOD (pixel format) -- 16-bit RGB565 */
	lcd_cmd(par, MIPI_DCS_SET_PIXEL_FORMAT);
	lcd_data(par, COLMOD_16BIT_RGB565);

	par->panel_initialized = true;
	dev_info(&par->pdev->dev, "panel: init complete\n");
}

/* ============================================================================
 * Region Write -- CASET/RASET use 16-bit, pixel data uses 8-bit
 * Reads pixels from the MAUI framebuffer (ioremap'd memory)
 * ============================================================================ */

static void lcd_send_region(struct mt62xx_fb_info *par, u32 x0, u32 y0,
			    u32 x1, u32 y1)
{
	u32 x, y;
	u16 *fb = (u16 *)par->fb_virt;

	/* CASET (set column address) */
	lcd_cmd(par, MIPI_DCS_SET_COLUMN_ADDRESS);
	lcd_data(par, (x0 >> 8) & 0xFF);
	lcd_data(par, x0 & 0xFF);
	lcd_data(par, (x1 >> 8) & 0xFF);
	lcd_data(par, x1 & 0xFF);

	/* RASET (set page/row address) */
	lcd_cmd(par, MIPI_DCS_SET_PAGE_ADDRESS);
	lcd_data(par, (y0 >> 8) & 0xFF);
	lcd_data(par, y0 & 0xFF);
	lcd_data(par, (y1 >> 8) & 0xFF);
	lcd_data(par, y1 & 0xFF);

	/* RAMWR (start memory write) */
	lcd_cmd(par, MIPI_DCS_WRITE_MEMORY_START);

	/* Pixel data -- 8-bit writes (high byte, low byte per pixel)
	 * Read from kmalloc'd framebuffer (regular kernel memory) */
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			u16 pixel = fb[y * par->width + x];
			lcd_pixel_byte(par, (pixel >> 8) & 0xFF);
			lcd_pixel_byte(par, pixel & 0xFF);
		}
	}
}

/* ============================================================================
 * Backlight (ISINK current sink + PMU clock gate)
 *
 * ISINK channels 1-3 drive the backlight LEDs.
 * PMU clock gate must be enabled first for ISINK to function.
 * ============================================================================ */

static void lcd_backlight_on(struct mt62xx_fb_info *par)
{
	u16 v;
	const u32 isink_channel_offsets[BACKLIGHT_NUM_CHANNELS] = {
		ISINK1_CON0, ISINK2_CON0, ISINK3_CON0
	};
	int i;

	/* Enable ISINK clock via PMU */
	writew(PMU_ISINK_CLK_BIT, par->pmu_base + PMU_ISINK_CLK_SET);
	udelay(BACKLIGHT_STABLE_US);

	/* Enable each ISINK channel with max brightness step */
	for (i = 0; i < BACKLIGHT_NUM_CHANNELS; i++) {
		v = readw(par->isink_base + isink_channel_offsets[i]);
		v &= ISINK_CH_DISABLE_MASK;
		v |= ISINK_CH_ENABLE_VAL;
		writew(v, par->isink_base + isink_channel_offsets[i]);
	}

	dev_info(&par->pdev->dev,
		 "backlight: ISINK 0x%04x 0x%04x 0x%04x\n",
		 readw(par->isink_base + ISINK1_CON0),
		 readw(par->isink_base + ISINK2_CON0),
		 readw(par->isink_base + ISINK3_CON0));
}

static void lcd_backlight_off(struct mt62xx_fb_info *par)
{
	u16 v;
	const u32 isink_channel_offsets[BACKLIGHT_NUM_CHANNELS] = {
		ISINK1_CON0, ISINK2_CON0, ISINK3_CON0
	};
	int i;

	/* Disable each ISINK channel (clear enable bit) */
	for (i = 0; i < BACKLIGHT_NUM_CHANNELS; i++) {
		v = readw(par->isink_base + isink_channel_offsets[i]);
		v &= ~ISINK_EN_BIT;
		writew(v, par->isink_base + isink_channel_offsets[i]);
	}
}

/* ============================================================================
 * Periodic Update Work
 *
 * Deferred IO does NOT work on no-MMU (uClinux) systems because it relies
 * on page dirty tracking via the MMU, which doesn't exist. fbcon writes
 * to our buffer but the deferred IO callback never fires.
 *
 * Instead, we use a periodic delayed_work that unconditionally pushes
 * the entire framebuffer to the panel every HZ / fps jiffies.
 * ============================================================================ */

static void mt62xx_fb_update_work(struct work_struct *work)
{
	struct mt62xx_fb_info *par = container_of(
		to_delayed_work(work), struct mt62xx_fb_info, update_work);

	if (!par->panel_initialized)
		return;

	lcd_send_region(par, 0, 0, par->width - 1, par->height - 1);

	/* Reschedule ourselves for the next frame using system_wq */
	queue_delayed_work(system_wq, &par->update_work,
			     par->fps > 0 ? HZ / par->fps : HZ / DEFAULT_FPS);
}

/* ============================================================================
 * FB Ops
 * ============================================================================ */

static int mt62xx_fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                               u_int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 cr, cg, cb, value;

	if (regno >= 16)
		return -EINVAL;

	cr = (red >> (16 - info->var.red.length)) &
	     ((1 << info->var.red.length) - 1);
	cg = (green >> (16 - info->var.green.length)) &
	     ((1 << info->var.green.length) - 1);
	cb = (blue >> (16 - info->var.blue.length)) &
	     ((1 << info->var.blue.length) - 1);

	value = (cr << info->var.red.offset) |
		(cg << info->var.green.offset) |
		(cb << info->var.blue.offset);

	pal[regno] = value;
	return 0;
}

static int mt62xx_fb_blank(int blank, struct fb_info *info)
{
	struct mt62xx_fb_info *par = info->par;

	switch (blank) {
	case FB_BLANK_UNBLANK:
		lcd_backlight_on(par);
		if (par->panel_initialized) {
			lcd_cmd(par, MIPI_DCS_SET_DISPLAY_ON);
			msleep(DISPLAY_BLANK_DELAY_MS);
		}
		break;
	default:
		if (par->panel_initialized) {
			lcd_cmd(par, MIPI_DCS_SET_DISPLAY_OFF);
			msleep(DISPLAY_BLANK_DELAY_MS);
		}
		lcd_backlight_off(par);
		break;
	}
	return 0;
}

static struct fb_ops mt62xx_fb_ops = {
	.owner          = THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_fillrect    = sys_fillrect,
	.fb_copyarea    = sys_copyarea,
	.fb_imageblit   = sys_imageblit,
	.fb_setcolreg   = mt62xx_fb_setcolreg,
	.fb_blank       = mt62xx_fb_blank,
};

/* ============================================================================
 * Probe
 * ============================================================================ */

static int mt62xx_fb_probe(struct platform_device *pdev)
{
	struct mt62xx_fb_info *par;
	struct fb_info *info;
	struct resource *res;
	int ret;

	/* Clamp parameters to sane values */
	if (fb_width <= 0 || fb_width > 1080)
		fb_width = DEFAULT_WIDTH;
	if (fb_height <= 0 || fb_height > 1920)
		fb_height = DEFAULT_HEIGHT;
	if (fb_fps <= 0 || fb_fps > 60)
		fb_fps = DEFAULT_FPS;

	dev_info(&pdev->dev, "probe: start (%dx%d @ %dfps, fb=%d bytes)\n",
		 fb_width, fb_height, fb_fps,
		 LCD_FB_SIZE(fb_width, fb_height));

	info = framebuffer_alloc(sizeof(*par), &pdev->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->info = info;
	par->pdev = pdev;
	par->panel_initialized = false;
	par->width = fb_width;
	par->height = fb_height;
	par->fps = fb_fps;
	par->fb_size = LCD_FB_SIZE(fb_width, fb_height);

	/* Allocate framebuffer from kernel heap.
	 * kmalloc with GFP_DMA ensures physically contiguous low memory.
	 * For 150KB this uses one order-6 buddy allocation (256KB) with
	 * the tail trimmed -- minimal fragmentation impact. */
	par->fb_virt = kmalloc(par->fb_size, GFP_KERNEL | GFP_DMA);
	if (!par->fb_virt) {
		dev_err(&pdev->dev, "probe: failed to allocate %lu bytes for framebuffer\n",
			par->fb_size);
		ret = -ENOMEM;
		goto err_fb;
	}
	par->fb_phys = virt_to_phys(par->fb_virt);

	dev_info(&pdev->dev, "probe: fb phys=0x%08lx virt=%p size=%lu\n",
		 par->fb_phys, par->fb_virt, par->fb_size);

	/* Map LCD controller registers */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcd-regs");
	if (!res) {
		ret = -ENODEV;
		goto err_fb_free;
	}
	par->lcd_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(par->lcd_base)) {
		ret = PTR_ERR(par->lcd_base);
		goto err_fb_free;
	}

	/* Map GPIO registers (for future pin mux configuration) */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpio-regs");
	if (!res) {
		ret = -ENODEV;
		goto err_fb_free;
	}
	par->gpio_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(par->gpio_base)) {
		ret = PTR_ERR(par->gpio_base);
		goto err_fb_free;
	}

	/* Map clock gating / PDN registers */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config-regs");
	if (!res) {
		ret = -ENODEV;
		goto err_fb_free;
	}
	par->config_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(par->config_base)) {
		ret = PTR_ERR(par->config_base);
		goto err_fb_free;
	}

	/* Map ISINK registers (backlight LED driver) */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "isink-regs");
	if (!res) {
		ret = -ENODEV;
		goto err_fb_free;
	}
	par->isink_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(par->isink_base)) {
		ret = PTR_ERR(par->isink_base);
		goto err_fb_free;
	}

	/* Map PMU / ANA_CFGSYS registers (backlight clock gating) */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pmu-regs");
	if (!res) {
		ret = -ENODEV;
		goto err_fb_free;
	}
	par->pmu_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(par->pmu_base)) {
		ret = PTR_ERR(par->pmu_base);
		goto err_fb_free;
	}

	dev_info(&pdev->dev, "probe: all registers mapped\n");

	/* === Hardware init: EXACT devmem sequence === */

	/* Step 1: Enable clocks */
	lcd_clock_enable(par);

	/* Step 2-7: Panel init (reads PAR0, soft reset, SLPOUT, DISPON,
	 * PDW, MADCTL, COLMOD) */
	lcd_panel_init(par);

	/* Step 8: Backlight */
	lcd_backlight_on(par);

	/* Step 9: Clear to black and push to panel */
	memset(par->fb_virt, 0, par->fb_size);
	dev_info(&pdev->dev, "probe: pushing initial frame\n");
	lcd_send_region(par, 0, 0, par->width - 1, par->height - 1);
	dev_info(&pdev->dev, "probe: frame pushed\n");

	/* Setup fb_info */
	strcpy(info->fix.id, "mt62xx-fb");
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.smem_start = par->fb_phys;
	info->fix.smem_len = par->fb_size;
	info->fix.line_length = par->width * LCD_BPP / 8;

	info->var.xres = par->width;
	info->var.yres = par->height;
	info->var.xres_virtual = par->width;
	info->var.yres_virtual = par->height;
	info->var.bits_per_pixel = LCD_BPP;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode = FB_VMODE_NONINTERLACED;
	info->var.red.offset = 11;   info->var.red.length = 5;
	info->var.green.offset = 5;  info->var.green.length = 6;
	info->var.blue.offset = 0;   info->var.blue.length = 5;

	info->fbops = &mt62xx_fb_ops;
	info->screen_base = par->fb_virt;
	info->screen_size = par->fb_size;
	info->pseudo_palette = devm_kmalloc(&pdev->dev, sizeof(u32) * 16,
					    GFP_KERNEL);
	if (!info->pseudo_palette) {
		ret = -ENOMEM;
		goto err_fb_free;
	}

	info->flags = FBINFO_DEFAULT | FBINFO_VIRTFB;

	/* Register framebuffer BEFORE starting workqueue so fbcon can bind */
	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "probe: register_fb failed: %d\n", ret);
		goto err_fb_free;
	}

	platform_set_drvdata(pdev, par);
	dev_info(&pdev->dev, "probe: fb%d registered (%dx%d @ %dfps)\n",
		 info->node, par->width, par->height, par->fps);

	/* Start periodic updates using system_wq (no dedicated thread).
	 * Using system_wq avoids allocating a kernel thread (~8KB stack)
	 * which would add memory pressure on this low memory system. */
	INIT_DELAYED_WORK(&par->update_work, mt62xx_fb_update_work);
	queue_delayed_work(system_wq, &par->update_work,
			     par->fps > 0 ? HZ / par->fps : HZ / DEFAULT_FPS);

	dev_info(&pdev->dev, "probe: SUCCESS -- periodic update started\n");
	return 0;

err_fb_free:
	kfree(par->fb_virt);
err_fb:
	framebuffer_release(info);
	dev_err(&pdev->dev, "probe: FAILED %d\n", ret);
	return ret;
}

static int mt62xx_fb_remove(struct platform_device *pdev)
{
	struct mt62xx_fb_info *par = platform_get_drvdata(pdev);
	struct fb_info *info = par->info;

	/* Stop periodic updates (uses system_wq, no destroy needed) */
	cancel_delayed_work_sync(&par->update_work);

	unregister_framebuffer(info);
	lcd_backlight_off(par);
	if (par->panel_initialized) {
		lcd_cmd(par, MIPI_DCS_SET_DISPLAY_OFF);
		msleep(DISPLAY_BLANK_DELAY_MS);
		lcd_cmd(par, MIPI_DCS_ENTER_SLEEP_MODE);
		msleep(SLEEP_EXIT_DELAY_MS);
	}

	kfree(par->fb_virt);
	framebuffer_release(info);
	return 0;
}

static struct platform_driver mt62xx_fb_driver = {
	.probe  = mt62xx_fb_probe,
	.remove = mt62xx_fb_remove,
	.driver = { .name = DRIVER_NAME },
};

module_platform_driver(mt62xx_fb_driver);

MODULE_AUTHOR("gvl610");
MODULE_DESCRIPTION("MT62XX LCD Framebuffer Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
