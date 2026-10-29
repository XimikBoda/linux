/*
 * MT62XX/MT6260 LCD Controller Register Definitions
 *
 * All registers are defined as OFFSETS from their respective base addresses,
 * which are provided via platform device resources (or device tree later).
 *
 * Required base pointers from platform device:
 *   lcd_base:    LCD controller (parallel/serial interface, ROI, layers)
 *   gpio_base:   GPIO mode, direction, drive, pull registers (for pin mux)
 *   config_base: Clock gating / power-down control
 *   isink_base:  ISINK current sink (backlight LED driver)
 *   pmu_base:    ANA_CFGSYS (backlight clock gating)
 */

#ifndef __VIDEO_MT62XX_LCD_H
#define __VIDEO_MT62XX_LCD_H

/* =========================================================================
 * LCD Controller Register Offsets (from lcd_base)
 * ========================================================================= */

/* Status and control */
#define LCD_STA                 0x0000
#define LCD_INT_ENABLE          0x0004
#define LCD_INT_STATUS          0x0008
#define LCD_START               0x000C
#define LCD_RSTB                0x0010

/* Parallel interface configuration */
#define LCD_PARALLEL0_CONFIG    0x0030
#define LCD_PARALLEL1_CONFIG    0x0034
#define LCD_PARALLEL2_CONFIG    0x0038
#define LCD_PARALLEL_PDW        0x003C

/* TE / sync */
#define LCD_CALC_HTT            0x0044
#define LCD_SYNC_LCM_SIZE       0x0048
#define LCD_SYNC_CNT            0x004C
#define LCD_TECON               0x0050

/* GMC bus master */
#define LCD_GMCCON              0x0054

/* ROI */
#define LCD_ROI_CTRL            0x0080
#define LCD_ROI_OFFSET          0x0084
#define LCD_ROI_CMD_ADDR        0x0088
#define LCD_ROI_DATA_ADDR       0x008C
#define LCD_ROI_SIZE            0x0090
#define LCD_ROI_BG_COLOR        0x009C

/* Layer registers: base at offset 0x00B0, stride 0x30 per layer (up to 4 layers) */
#define LCD_LAYER_BASE          0x00B0
#define LCD_LAYER_STRIDE        0x30

/* Per-layer register offsets (add to LCD_LAYER_BASE + n*LCD_LAYER_STRIDE) */
#define LAYER_CTRL              0x00
#define LAYER_SRC_KEY           0x04
#define LAYER_OFFSET            0x08
#define LAYER_BUFF_ADDR         0x0C
#define LAYER_SIZE              0x10
#define LAYER_MEM_OFFSET        0x18
#define LAYER_MEM_PITCH         0x1C

/* Parallel interface A0 addresses (direct memory-mapped I/O to LCM) */
#define LCD_PARALLEL0_A0_LOW    0x0F00
#define LCD_PARALLEL0_A0_HIGH   0x0F10

/* Command queue (64 entries) */
#define LCD_CMD_QUEUE           0x0C00

/* Performance / ultra-low-power */
#define LCD_ULTRA_CON           0x0240
#define LCD_CONSUME_RATE        0x0244
#define LCD_DBI_ULTRA_TH        0x0248
#define LCD_GMC_ULTRA_TH        0x024C

/* =========================================================================
 * GPIO Register Offsets (from gpio_base)
 * ========================================================================= */

#define GPIO_MODE0              0x0C00
#define GPIO_MODE0_SET          0x0C04
#define GPIO_MODE0_CLR          0x0C08
#define GPIO_MODE_OFFSET        0x10

#define GPIO_DIR0               0x0000
#define GPIO_DIR0_SET           0x0004
#define GPIO_DIR0_CLR           0x0008
#define GPIO_DIR_OFFSET         0x10

#define GPIO_DOUT0              0x0100
#define GPIO_DOUT0_SET          0x0104
#define GPIO_DOUT0_CLR          0x0108

#define GPIO_PULL0              0x0100
#define GPIO_PULL_OFFSET        0x10
#define GPIO_PULLSEL0           0x0500
#define GPIO_PULLSEL0_SET       0x0504
#define GPIO_PULLSEL0_CLR       0x0508
#define GPIO_PULLSEL_OFFSET     0x10

#define GPIO_DRV0               0x0800

/* =========================================================================
 * Clock Gating Register Offsets (from config_base)
 * ========================================================================= */

#define DRVPDN_MCUSYS_PDN_CON0      0x0300
#define DRVPDN_MCUSYS_PDN_CON0_SET  0x0310
#define DRVPDN_MCUSYS_PDN_CON0_CLR  0x0320

/* Bit definitions for DRVPDN_MCUSYS_PDN_CON0 */
#define DRVPDN_CON0_SW_LCD          0x00000001UL
#define DRVPDN_CON0_SW_SLCD         0x00000080UL

/* Combined LCD + Serial LCD clock gate bits */
#define DRVPDN_CON0_LCD_GATE_BITS   (DRVPDN_CON0_SW_LCD | DRVPDN_CON0_SW_SLCD)

/* =========================================================================
 * ISINK Register Offsets (from isink_base, for backlight LED driver)
 * ========================================================================= */

#define ISINKS_CON0             0x0C00
#define ISINK0_CON0             0x0C10
#define ISINK1_CON0             0x0C20
#define ISINK2_CON0             0x0C30
#define ISINK3_CON0             0x0C40

#define ISINK_EN_BIT            0x0001
#define ISINK_MODE_BIT          0x0002
#define ISINK_STEP_MASK         0x0070
#define ISINK_STEP_SHIFT        4

/* ISINK current step value for backlight (step 7 = max brightness) */
#define ISINK_STEP_BACKLIGHT    (0x07 << ISINK_STEP_SHIFT)

/* Full ISINK channel enable value: step=7 + enable + mode */
#define ISINK_CH_ENABLE_VAL     (ISINK_STEP_BACKLIGHT | ISINK_EN_BIT)
#define ISINK_CH_DISABLE_MASK   ~(ISINK_STEP_MASK | ISINK_MODE_BIT | ISINK_EN_BIT)

/* =========================================================================
 * PMU / ANA_CFGSYS Register Offsets (from pmu_base)
 * ========================================================================= */

/* Backlight clock enable register */
#define PMU_ISINK_CLK_SET       0x000C
#define PMU_ISINK_CLK_BIT       0x0010

/* =========================================================================
 * LCD Register Bit Fields
 * ========================================================================= */

/* Status register (LCD_STA) */
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

/* Start/Reset control bits (for LCD_START register) */
#define LCD_START_RUN           0x8000
#define LCD_START_RESET         0x0001

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

/* Command queue entry flags */
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

/* Parallel timing field positions (for LCD_PARALLEL0_CONFIG) */
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

/* GMC burst sizes */
#define GMC_BURST_4B            0
#define GMC_BURST_16B           2
#define GMC_BURST_32B           3
#define GMC_BURST_64B           4

/* =========================================================================
 * Parallel Interface Default Values (from MAUI working state)
 *
 * PAR_TIMING_VALUE = 0x26195004:
 *   W_WAIT=4, W_SETUP=0, W_HOLD=5, R_LATENCY=25, R_SETUP=6, R_HOLD=2
 * PAR_PDW_VALUE = 0x00020000:
 *   Bus width 0 (8-bit), CHW/period = 2
 * ========================================================================= */

#define PAR_TIMING_MAUI_DEFAULT     0x26195004UL
#define PAR_PDW_MAUI_DEFAULT        0x00020000UL

/* =========================================================================
 * MIPI DCS / Standard Panel Commands
 * ========================================================================= */

#define MIPI_DCS_NOP                    0x00
#define MIPI_DCS_SOFT_RESET             0x01
#define MIPI_DCS_GET_DISPLAY_ID         0x04
#define MIPI_DCS_ENTER_SLEEP_MODE       0x10    /* SLPIN */
#define MIPI_DCS_EXIT_SLEEP_MODE        0x11    /* SLPOUT */
#define MIPI_DCS_ENTER_PARTIAL_MODE     0x12
#define MIPI_DCS_ENTER_NORMAL_MODE      0x13
#define MIPI_DCS_EXIT_INVERT_MODE       0x20
#define MIPI_DCS_ENTER_INVERT_MODE      0x21
#define MIPI_DCS_SET_DISPLAY_OFF        0x28    /* DISPOFF */
#define MIPI_DCS_SET_DISPLAY_ON         0x29    /* DISPON */
#define MIPI_DCS_SET_COLUMN_ADDRESS     0x2A    /* CASET */
#define MIPI_DCS_SET_PAGE_ADDRESS       0x2B    /* RASET */
#define MIPI_DCS_WRITE_MEMORY_START     0x2C    /* RAMWR */
#define MIPI_DCS_READ_MEMORY_START      0x2E    /* RAMRD */
#define MIPI_DCS_SET_ADDRESS_MODE       0x36    /* MADCTL */
#define MIPI_DCS_SET_PIXEL_FORMAT       0x3A    /* COLMOD */

/* Pixel format values for MIPI_DCS_SET_PIXEL_FORMAT (COLMOD) */
#define COLMOD_12BIT_RGB444     0x33
#define COLMOD_16BIT_RGB565     0x55
#define COLMOD_18BIT_RGB666     0x66
#define COLMOD_24BIT_RGB888     0x77

/* Address mode (MADCTL) bits */
#define MADCTL_MY               0x80    /* Row address order */
#define MADCTL_MX               0x40    /* Column address order */
#define MADCTL_MV               0x20    /* Row/Column exchange */
#define MADCTL_ML               0x10    /* Vertical refresh order */
#define MADCTL_BGR              0x08    /* RGB/BGR order */
#define MADCTL_MH               0x04    /* Horizontal refresh order */

/* =========================================================================
 * Color Formats (LCD layer format field values)
 * ========================================================================= */

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

#endif /* __VIDEO_MT62XX_LCD_H */
