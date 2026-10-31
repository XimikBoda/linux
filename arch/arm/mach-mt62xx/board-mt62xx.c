#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/serial_8250.h>
#include <linux/platform_device.h>
#include <asm/system_misc.h>
#include <asm/mach/arch.h>

extern void mt62xx_init_irq(void);
extern void mt62xx_timer_init(void);

struct pt_regs;
extern void mt62xx_handle_irq(struct pt_regs *regs);

#define MACH_TYPE_MT62XX 33333

#define WDT_BASE       ((void __iomem *)0xA0030000)
#define WDT_MODE       (WDT_BASE + 0x00)
#define WDT_LENGTH     (WDT_BASE + 0x04)
#define WDT_RESTART    (WDT_BASE + 0x08)

#define WDT_MODE_KEY       0x2200
#define WDT_MODE_ENABLE    0x0001
#define WDT_RESTART_KEY    0x1971


#define UART_BASE   0xA0080000
#define RTC_BASE    0xA0710000
#define USB_BASE    0xA0900000

/* LCD controller and related registers */
#define LCD_BASE    0xA0450000
#define GPIO_BASE   0xA0020000
#define CONFIG_BASE 0xA0010000
#define MIXED_BASE  0xA0170000
#define ISINK_BASE  0xA0700000  /* ISINK current sink (backlight LED driver) */
#define PMU_BASE    0xA0730000  /* ANA_CFGSYS (backlight clock gating) */


static struct plat_serial8250_port mt62xx_uart_data[] = {
    {
        .mapbase    = UART_BASE,
        .membase    = (void __iomem *)UART_BASE,
        .irq        = IRQ_UART0_LINE,
        .uartclk    = 13000000,
        .iotype     = UPIO_MEM32,
        .regshift   = 2,
        .type       = PORT_16550A,
        .flags      = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_FIXED_TYPE,
    },
    { },
};

static struct platform_device mt62xx_uart_device = {
    .name       = "serial8250",
    .id         = PLAT8250_DEV_PLATFORM,
    .dev        = {
        .platform_data = mt62xx_uart_data,
    },
};

static struct resource mt62xx_rtc_resources[] = {
    [0] = {
        .start = RTC_BASE,
        .end   = RTC_BASE + 0x0078,
        .flags = IORESOURCE_MEM,
    }
};

static struct platform_device mt62xx_rtc_device = {
    .name           = "mt62xx-rtc",
    .id             = -1,
    .num_resources  = ARRAY_SIZE(mt62xx_rtc_resources),
    .resource       = mt62xx_rtc_resources,
};

static struct resource mt62xx_usb_resources[] = {
    [0] = {
        .start  = USB_BASE,
        .end    = USB_BASE + 0xFFF,
        .flags  = IORESOURCE_MEM,
    },
    [1] = {
        .start  = IRQ_USB_LINE,
        .end    = IRQ_USB_LINE,
        .flags  = IORESOURCE_IRQ,
    },
};

static struct platform_device mt62xx_usb_device = {
    .name           = "mt62xx-udc",
    .id             = -1,
    .num_resources  = ARRAY_SIZE(mt62xx_usb_resources),
    .resource       = mt62xx_usb_resources,
};

/* LCD framebuffer platform device */
static struct resource mt62xx_lcd_resources[] = {
    {
        .start  = LCD_BASE,
        .end    = LCD_BASE + 0xFFF,
        .flags  = IORESOURCE_MEM,
        .name   = "lcd-regs",
    },
    {
        .start  = GPIO_BASE,
        .end    = GPIO_BASE + 0xFFF,
        .flags  = IORESOURCE_MEM,
        .name   = "gpio-regs",
    },
    {
        .start  = CONFIG_BASE,
        .end    = CONFIG_BASE + 0xFFF,
        .flags  = IORESOURCE_MEM,
        .name   = "config-regs",
    },
    {
        .start  = ISINK_BASE,
        .end    = ISINK_BASE + 0xFFF,
        .flags  = IORESOURCE_MEM,
        .name   = "isink-regs",
    },
    {
        .start  = PMU_BASE,
        .end    = PMU_BASE + 0xFFF,
        .flags  = IORESOURCE_MEM,
        .name   = "pmu-regs",
    },
};

static struct platform_device mt62xx_lcd_device = {
    .name           = "mt62xx-fb",
    .id             = -1,
    .num_resources  = ARRAY_SIZE(mt62xx_lcd_resources),
    .resource       = mt62xx_lcd_resources,
};

static void __init mt62xx_init(void)
{
    platform_device_register(&mt62xx_uart_device);
    platform_device_register(&mt62xx_rtc_device);
#ifdef CONFIG_USB_MT62XX
    platform_device_register(&mt62xx_usb_device);
#endif
#ifdef CONFIG_FB_MT62XX
    platform_device_register(&mt62xx_lcd_device);
#endif
}

static void mt62xx_restart(enum reboot_mode mode, const char *cmd) {
    u16 current_mode;

    writew(0x0028, WDT_LENGTH);

    writew(WDT_RESTART_KEY, WDT_RESTART);

    current_mode = readw(WDT_MODE);
    writew(current_mode | WDT_MODE_KEY | WDT_MODE_ENABLE, WDT_MODE);

    while (1) {
        cpu_relax();
    }
}

MACHINE_START(MT62XX, "MediaTek MT62xx")
    .atag_offset    = 0x100,
    .init_irq       = mt62xx_init_irq,
    .init_time      = mt62xx_timer_init,
    .init_machine   = mt62xx_init,
    .restart        = mt62xx_restart,
MACHINE_END
