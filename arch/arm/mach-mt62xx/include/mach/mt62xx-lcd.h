/*
 * MT62XX/MT6260 LCD Controller Register Definitions
 *
 * Used by the mt62xx-fb framebuffer driver.
 */

#ifndef __MACH_MT62XX_LCD_H
#define __MACH_MT62XX_LCD_H

/* Base addresses */
#define MT62XX_LCD_BASE         0xA0450000UL
#define MT62XX_GPIO_BASE        0xA0020000UL
#define MT62XX_CONFIG_BASE      0xA0010000UL
#define MT62XX_MIXED_BASE       0xA0170000UL

/* ============================================================================
 * LCD Controller Registers
 * ============================================================================ */

/* Status and Control */
#define LCD_STA                 (MT62XX_LCD_BASE + 0x0000)
#define LCD_INT_ENABLE          (MT62XX_LCD_BASE + 0x0004)
#define LCD_INT_STATUS          (MT62XX_LCD_BASE + 0x0008)
#define LCD_START               (MT62XX_LCD_BASE + 0x000C)
#define LCD_RSTB                (MT62XX_LCD_BASE + 0x0010)

/* Parallel Interface Configuration */
#define LCD_PARALLEL0_CONFIG    (MT62XX_LCD_BASE + 0x0030)
#define LCD_PARALLEL1_CONFIG    (MT62XX_LCD_BASE + 0x0034)
#define LCD_PARALLEL2_CONFIG    (MT62XX_LCD_BASE + 0x0038)
#define LCD_PARALLEL_PDW        (MT62XX_LCD_BASE + 0x003C)

/* TE / Sync */
#define LCD_CALC_HTT            (MT62XX_LCD_BASE + 0x0044)
#define LCD_SYNC_LCM_SIZE       (MT62XX_LCD_BASE + 0x0048)
#define LCD_SYNC_CNT            (MT62XX_LCD_BASE + 0x004C)
#define LCD_TECON               (MT62XX_LCD_BASE + 0x0050)

/* GMC Bus Master */
#define LCD_GMCCON              (MT62XX_LCD_BASE + 0x0054)

/* ROI */
#define LCD_ROI_CTRL            (MT62XX_LCD_BASE + 0x0080)
#define LCD_ROI_OFFSET          (MT62XX_LCD_BASE + 0x0084)
#define LCD_ROI_CMD_ADDR        (MT62XX_LCD_BASE + 0x0088)
#define LCD_ROI_DATA_ADDR       (MT62XX_LCD_BASE + 0x008C)
#define LCD_ROI_SIZE            (MT62XX_LCD_BASE + 0x0090)
#define LCD_ROI_BG_COLOR        (MT62XX_LCD_BASE + 0x009C)

/* Layer registers: base at 0x00B0, stride 0x30 per layer (4 layers) */
#define LCD_LAYER_BASE          (MT62XX_LCD_BASE + 0x00B0)
#define LCD_LAYER_STRIDE        0x30

/* Per-layer register offsets */
#define LAYER_CTRL              0x00
#define LAYER_SRC_KEY           0x04
#define LAYER_OFFSET            0x08
#define LAYER_BUFF_ADDR         0x0C
#define LAYER_SIZE              0x10
#define LAYER_MEM_OFFSET        0x18
#define LAYER_MEM_PITCH         0x1C

/* Parallel interface A0 addresses (memory-mapped I/O for LCM) */
#define LCD_PARALLEL0_A0_LOW    (MT62XX_LCD_BASE + 0x0F00)
#define LCD_PARALLEL0_A0_HIGH   (MT62XX_LCD_BASE + 0x0F10)

/* Command Queue (64 entries) */
#define LCD_CMD_QUEUE           (MT62XX_LCD_BASE + 0x0C00)

/* Performance */
#define LCD_ULTRA_CON           (MT62XX_LCD_BASE + 0x0240)
#define LCD_CONSUME_RATE        (MT62XX_LCD_BASE + 0x0244)
#define LCD_DBI_ULTRA_TH        (MT62XX_LCD_BASE + 0x0248)
#define LCD_GMC_ULTRA_TH        (MT62XX_LCD_BASE + 0x024C)

/* ============================================================================
 * LCD Register Bit Fields
 * ============================================================================ */

/* Status */
#define STA_RUN                 0x0001
#define STA_WAIT_CMDQ           0x0002
#define STA_WAIT_HTT            0x0008
#define STA_TE_PEND             0x0010
#define STA_BUSY                0x0020

/* Interrupt */
#define INT_COMPLETE            0x0001
#define INT_REG_COMPLETE        0x0002
#define INT_CMD_COMPLETE        0x0004
#define INT_HTT_COMPLETE        0x0010
#define INT_VSYNC_COMPLETE      0x0020

/* Start/Reset */
#define START_BIT               0x8000
#define IF_RESET_BIT            0x0001

/* Layer control */
#define LAYER_OPACITY_EN        0x00000100UL
#define LAYER_SRC_KEY_EN        0x00004000UL
#define LAYER_DATA_SWAP         0x00010000UL
#define LAYER_DITHER_EN         0x00040000UL
#define LAYER_DST_KEY_EN        0x01000000UL
#define LAYER_FORMAT_SHIFT      20
#define LAYER_ROTATE_SHIFT      11
#define LAYER_OPACITY_SHIFT     0

/* ROI control */
#define ROI_CMD_EN              0x00008000UL
#define ROI_LAYER_EN(n)         (1UL << (31 - (n)))
#define ROI_CMD_NUM_SHIFT       8
#define ROI_CMD_NUM_MASK        0x00003F00UL
#define ROI_PERIOD_SHIFT        16
#define ROI_PERIOD_MASK         0x00FF0000UL
#define ROI_FORMAT_MASK         0x000000FFUL

/* Command queue entry */
#define CMD_FLAG                0x00800000UL
#define DATA_FLAG               0x00000000UL

/* TE control */
#define TE_ENABLE               0x0001
#define TE_SYNCEDGE             0x0002
#define TE_SW_TE                0x8000

/* Parallel bus width (LCD_PARALLEL_PDW bits 0-2 for IF0) */
#define BUS_WIDTH_8BIT          0
#define BUS_WIDTH_9BIT          1
#define BUS_WIDTH_16BIT         2
#define BUS_WIDTH_18BIT         3

/* Parallel timing field positions */
#define PAR_W_WAIT_MASK         0x0000003FUL
#define PAR_W_WAIT_SHIFT        0
#define PAR_W_SETUP_MASK        0x00000F00UL
#define PAR_W_SETUP_SHIFT       8
#define PAR_W_HOLD_MASK         0x0000F000UL
#define PAR_W_HOLD_SHIFT        12
#define PAR_R_LATENCY_MASK      0x003F0000UL
#define PAR_R_LATENCY_SHIFT     16
#define PAR_R_SETUP_MASK        0x0F000000UL
#define PAR_R_SETUP_SHIFT       24
#define PAR_R_HOLD_MASK         0xF0000000UL
#define PAR_R_HOLD_SHIFT        28

/* GMC burst */
#define GMC_BURST_4B            0
#define GMC_BURST_16B           2
#define GMC_BURST_32B           3
#define GMC_BURST_64B           4

/* ============================================================================
 * Color Formats
 * ============================================================================ */

enum mt62xx_lcd_format {
    FMT_8BPP_INDEX  = 0,
    FMT_RGB565      = 1,
    FMT_UYVY422     = 2,
    FMT_RGB888      = 3,
    FMT_ARGB8888    = 4,
    FMT_PARGB8888   = 5,
    FMT_XRGB        = 6,
    FMT_ARGB6666    = 7,
    FMT_PARGB6666   = 8
};

enum mt62xx_lcd_rotate {
    ROT_NORMAL      = 0,
    ROT_90          = 1,
    ROT_180         = 2,
    ROT_270         = 3,
    ROT_MIRROR_X    = 4,
    ROT_MIRROR_Y    = 5,
    ROT_90_MIRROR   = 6,
    ROT_270_MIRROR  = 7
};

/* ============================================================================
 * GPIO Registers for LCD Pin Mux
 * ============================================================================ */

#define GPIO_MODE0              (MT62XX_GPIO_BASE + 0x0C00)
#define GPIO_MODE0_SET          (MT62XX_GPIO_BASE + 0x0C04)
#define GPIO_MODE0_CLR          (MT62XX_GPIO_BASE + 0x0C08)
#define GPIO_MODE_OFFSET        0x10

#define GPIO_DIR0               (MT62XX_GPIO_BASE + 0x0000)
#define GPIO_DIR0_SET           (MT62XX_GPIO_BASE + 0x0004)
#define GPIO_DIR0_CLR           (MT62XX_GPIO_BASE + 0x0008)
#define GPIO_DIR_OFFSET         0x10

#define GPIO_DOUT0              (MT62XX_GPIO_BASE + 0x0100)
#define GPIO_DOUT0_SET          (MT62XX_GPIO_BASE + 0x0104)
#define GPIO_DOUT0_CLR          (MT62XX_GPIO_BASE + 0x0108)

#define GPIO_PULL0              (MT62XX_GPIO_BASE + 0x0100)
#define GPIO_PULL_OFFSET        0x10
#define GPIO_PULLSEL0           (MT62XX_GPIO_BASE + 0x0500)
#define GPIO_PULLSEL0_SET       (MT62XX_GPIO_BASE + 0x0504)
#define GPIO_PULLSEL0_CLR       (MT62XX_GPIO_BASE + 0x0508)
#define GPIO_PULLSEL_OFFSET     0x10

#define GPIO_DRV0               (MT62XX_GPIO_BASE + 0x0800)

/* ============================================================================
 * Clock Gating
 * ============================================================================ */

#define DRVPDN_MCUSYS_PDN_CON0      (MT62XX_CONFIG_BASE + 0x0300)
#define DRVPDN_MCUSYS_PDN_CON0_SET  (MT62XX_CONFIG_BASE + 0x0310)
#define DRVPDN_MCUSYS_PDN_CON0_CLR  (MT62XX_CONFIG_BASE + 0x0320)
#define DRVPDN_CON0_SW_LCD          0x00000001UL

/* ============================================================================
 * ISINK Registers (for backlight)
 * ============================================================================ */

#define ISINKS_CON0             (MT62XX_MIXED_BASE + 0x0C00)
#define ISINK0_CON0             (MT62XX_MIXED_BASE + 0x0C10)
#define ISINK1_CON0             (MT62XX_MIXED_BASE + 0x0C20)
#define ISINK2_CON0             (MT62XX_MIXED_BASE + 0x0C30)
#define ISINK3_CON0             (MT62XX_MIXED_BASE + 0x0C40)

#define ISINK_EN_BIT            0x0001
#define ISINK_MODE_BIT          0x0002
#define ISINK_STEP_MASK         0x0070
#define ISINK_STEP_SHIFT        4

#endif /* __MACH_MT62XX_LCD_H */
