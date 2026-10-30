#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define DRIVER_NAME "mt62xx-msdc"

#define MSDC_CFG        0x0000 
#define MSDC_STA        0x0004 
#define MSDC_INT        0x0008 
#define MSDC_PS         0x000C 
#define MSDC_DAT        0x0010 
#define MSDC_IOCON0     0x0014 
#define MSDC_IOCON1     0x0018 

#define SDC_CFG         0x0020 
#define SDC_CMD         0x0024 
#define SDC_ARG         0x0028 
#define SDC_STA         0x002C 
#define SDC_RESP0       0x0030 
#define SDC_RESP1       0x0034 
#define SDC_RESP2       0x0038 
#define SDC_RESP3       0x003C 
#define SDC_CMDSTA      0x0040 
#define SDC_DATSTA      0x0044 
#define SDC_CSTA        0x0048 
#define SDC_IRQMASK0    0x004C 
#define SDC_IRQMASK1    0x0050 
#define CLKPAD_RED      0x0080 

/* MSDC_CFG */
#define MSDC_CFG_MSDC       BIT(0)
#define MSDC_CFG_RST        BIT(1)
#define MSDC_CFG_NOCRC      BIT(2)
#define MSDC_CFG_STDBY      BIT(5)
#define MSDC_CFG_CRED       BIT(6)
#define MSDC_CFG_SCLKON     BIT(7)
#define MSDC_CFG_INTEN      BIT(16)
#define MSDC_CFG_DMAEN      BIT(17)
#define MSDC_CFG_PINEN      BIT(18)
#define MSDC_CFG_DIRQEN     BIT(19)
#define MSDC_CFG_RCDEN      BIT(20)
#define MSDC_CFG_VDDPD      BIT(21)
#define MSDC_CFG_FIFOTHD_M  GENMASK(27, 24)

/* MSDC_STA */
#define MSDC_STA_BF         BIT(0)  /* FIFO Full */
#define MSDC_STA_BE         BIT(1)  /* FIFO Empty */
#define MSDC_STA_DRQ        BIT(2)
#define MSDC_STA_INT        BIT(3)
#define MSDC_STA_FIFOCNT_M  GENMASK(7, 4)
#define MSDC_STA_FIFOCLR    BIT(14)
#define MSDC_STA_BUSY       BIT(15)

/* MSDC_INT */
#define MSDC_INT_DIRQ       BIT(0)
#define MSDC_INT_PINIRQ     BIT(1)
#define MSDC_INT_SDCMDIRQ   BIT(2)
#define MSDC_INT_SDDATIRQ   BIT(3)
#define MSDC_INT_SDMCIRQ    BIT(4)
#define MSDC_INT_SDR1BIRQ   BIT(6)
#define MSDC_INT_SDIOIRQ    BIT(7)

/* MSDC_PS */
#define MSDC_PS_CDEN        BIT(0)
#define MSDC_PS_PIEN0       BIT(1)
#define MSDC_PS_POEN0       BIT(2)
#define MSDC_PS_PIN0        BIT(3)
#define MSDC_PS_PINCHG      BIT(4)
#define MSDC_PS_CMD         BIT(24)

/* SDC_CFG */
#define SDC_CFG_BLKLEN_M    GENMASK(11, 0)
#define SDC_CFG_SIEN        BIT(16)
#define SDC_CFG_MDLEN       BIT(17)
#define SDC_CFG_SDIO        BIT(19)
#define SDC_CFG_DTOC_M      GENMASK(31, 24)

/* SDC_CMD */
#define SDC_CMD_IDRT            BIT(10)
#define SDC_CMD_DTYPE_SINGLE    (1 << 11)
#define SDC_CMD_DTYPE_MULTI     (2 << 11)
#define SDC_CMD_RW              BIT(13)
#define SDC_CMD_STOP            BIT(14)

#define SDC_CMD_RSPTYP_NONE	0b000
#define SDC_CMD_RSPTYP_R1	0b001
#define SDC_CMD_RSPTYP_R2	0b010
#define SDC_CMD_RSPTYP_R3	0b011
#define SDC_CMD_RSPTYP_R6	0b110
#define SDC_CMD_RSPTYP_R1B	0b111


/* SDC_STA */
#define SDC_STA_BESDCBUSY   BIT(0)
#define SDC_STA_BECMDBUSY   BIT(1)
#define SDC_STA_BEDATBUSY   BIT(2)
#define SDC_STA_FECCMDBUSY  BIT(3)
#define SDC_STA_FEDATBUSY   BIT(4)
#define SDC_STA_WP			BIT(5)

/* SDC_CMDSTA */
#define SDC_CMDSTA_CMDRDY       BIT(0)
#define SDC_CMDSTA_CMDTO        BIT(1)
#define SDC_CMDSTA_RSPCRCERR    BIT(2)

/* SDC_DATSTA */
#define SDC_DATSTA_BLKDONE      BIT(0)
#define SDC_DATSTA_DATTO        BIT(1)
#define SDC_DATSTA_DATCRCERR    GENMASK(9, 2)

struct mt62xx_sd_host {
    struct mmc_host *mmc;
    void __iomem *base;
    void __iomem *cg_base;
    int irq;
    struct mmc_request *mrq;
    struct completion cmd_done;
    struct completion data_done;

	struct sg_mapping_iter miter;
    u32 bytes_xfered;
};

static irqreturn_t mt62xx_msdc_irq(int irq, void *dev_id)
{
    struct mt62xx_sd_host *host = dev_id;
    u32 int_status = readl(host->base + MSDC_INT);

    if (!int_status)
        return IRQ_NONE;

    if (int_status & ~MSDC_INT_DIRQ) 
        pr_info("mt62xx-msdc: IRQ FIRED! status=0x%08x\n", int_status);

    if (int_status & MSDC_INT_SDCMDIRQ) { 
        u32 cmd_status = readl(host->base + SDC_CMDSTA);

        pr_info("mt62xx-msdc: CMDSTA = 0x%08x\n", cmd_status);
        
        if (host->mrq && host->mrq->cmd) {
            if (cmd_status & SDC_CMDSTA_CMDTO) 
                host->mrq->cmd->error = -ETIMEDOUT;
            else if (cmd_status & SDC_CMDSTA_RSPCRCERR) 
                host->mrq->cmd->error = -EILSEQ;
        }
        complete(&host->cmd_done);
    }

    if (int_status & MSDC_INT_PINIRQ) {
        pr_info("mt62xx-msdc: Card inserted or removed!\n");
        //mmc_detect_change(host->mmc, msecs_to_jiffies(200));
    }

    if (int_status & MSDC_INT_DIRQ) {
        if (host->mrq && host->mrq->data) {
            u32 sta = readl(host->base + MSDC_STA);
            
            if (host->mrq->data->flags & MMC_DATA_READ) {
                u32 fifocnt = (sta >> 4) & 0x0F;
                if (sta & MSDC_STA_BF) fifocnt = 8;

                while (fifocnt > 0 && sg_miter_next(&host->miter)) {
                    u32 space_in_sg = host->miter.length;
                    u32 available_in_fifo = fifocnt * 4;
                    u32 bytes_to_copy = min(space_in_sg, available_in_fifo);
                    u32 words_to_read = bytes_to_copy / 4;
                    int i;

                    if (words_to_read == 0) {
                        sg_miter_stop(&host->miter);
                        break;
                    }

                    u32 *dst = (u32 *)host->miter.addr;
                    for (i = 0; i < words_to_read; i++) {
                        dst[i] = readl(host->base + MSDC_DAT);
                    }
                    
                    host->miter.consumed = words_to_read * 4;
                    fifocnt -= words_to_read;
                    host->bytes_xfered += words_to_read * 4;
                    
                    sg_miter_stop(&host->miter);
                }
            } 
            else if (host->mrq->data->flags & MMC_DATA_WRITE) {
                u32 fifocnt = (sta >> 4) & 0x0F;
                u32 free_entries = 8 - fifocnt; 
                
                if (sta & MSDC_STA_BE) free_entries = 8;
                if (sta & MSDC_STA_BF) free_entries = 0;

                while (free_entries > 0 && sg_miter_next(&host->miter)) {
                    u32 data_in_sg = host->miter.length;
                    u32 space_in_fifo = free_entries * 4;
                    u32 bytes_to_copy = min(data_in_sg, space_in_fifo);
                    u32 words_to_write = bytes_to_copy / 4;
                    int i;

                    if (words_to_write == 0) {
                        sg_miter_stop(&host->miter);
                        break;
                    }

                    u32 *src = (u32 *)host->miter.addr;
                    for (i = 0; i < words_to_write; i++) {
                        writel(src[i], host->base + MSDC_DAT);
                    }
                    
                    host->miter.consumed = words_to_write * 4;
                    free_entries -= words_to_write;
                    host->bytes_xfered += words_to_write * 4;
                    
                    sg_miter_stop(&host->miter);
                }
            }
        }
    }

    if (int_status & MSDC_INT_SDDATIRQ) {
        u32 dat_status = readl(host->base + SDC_DATSTA);
        pr_info("mt62xx-msdc: DATA IRQ! DATSTA = 0x%08x\n", dat_status);
        
        if (host->mrq && host->mrq->data && (host->mrq->data->flags & MMC_DATA_READ)) {
            while (sg_miter_next(&host->miter)) {
                u32 sta = readl(host->base + MSDC_STA);
                u32 fifocnt = (sta >> 4) & 0x0F;
                
                if (sta & MSDC_STA_BF) fifocnt = 8;
                
                if (fifocnt == 0) {
                    sg_miter_stop(&host->miter);
                    break;
                }
                
                u32 space_in_sg = host->miter.length;
                u32 available_in_fifo = fifocnt * 4;
                u32 bytes_to_copy = min(space_in_sg, available_in_fifo);
                u32 words_to_read = bytes_to_copy / 4;
                int i;
                
                if (words_to_read == 0) {
                    sg_miter_stop(&host->miter);
                    break;
                }

                u32 *dst = (u32 *)host->miter.addr;
                for (i = 0; i < words_to_read; i++) {
                    dst[i] = readl(host->base + MSDC_DAT);
                }
                
                host->miter.consumed = words_to_read * 4;
                host->bytes_xfered += words_to_read * 4;
                
                sg_miter_stop(&host->miter);
            }
        }

        if (dat_status & SDC_DATSTA_DATTO) {
            if (host->mrq && host->mrq->data)
                host->mrq->data->error = -ETIMEDOUT;
        } else if (dat_status & SDC_DATSTA_DATCRCERR) {
            if (host->mrq && host->mrq->data)
                host->mrq->data->error = -EILSEQ;
        }
        
        if (host->mrq && host->mrq->data) {
            u32 total_bytes = host->mrq->data->blocks * host->mrq->data->blksz;
            
            if (host->bytes_xfered >= total_bytes || host->mrq->data->error) {
                complete(&host->data_done);
            } else {
                pr_info("mt62xx-msdc: Block done. Expecting more data (%u/%u bytes)\n", 
                        host->bytes_xfered, total_bytes);
            }
        }
    }

    return IRQ_HANDLED;
}

static void mt62xx_msdc_setup_data(struct mt62xx_sd_host *host, struct mmc_request *mrq)
{
    struct mmc_data *data = mrq->data;
    u32 sdc_cfg = readl(host->base + SDC_CFG);
    
    sdc_cfg &= ~SDC_CFG_BLKLEN_M;
    sdc_cfg |= (data->blksz & SDC_CFG_BLKLEN_M);
	
	sdc_cfg &= ~SDC_CFG_DTOC_M;
    sdc_cfg |= (0xFF << 24);

    writel(sdc_cfg, host->base + SDC_CFG);

    readl(host->base + SDC_DATSTA);
}

static void mt62xx_msdc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct mt62xx_sd_host *host = mmc_priv(mmc);
    struct mmc_command *cmd = mrq->cmd;
    u32 cmd_val = cmd->opcode & 0x3F;
    u32 rsptyp = 0;
    int i;

    u32 pin_status = readl(host->base + MSDC_PS);
    pr_info("mt62xx-msdc: sending CMD%u (arg=0x%08x) | PIN_STATUS=0x%08x\n", 
            cmd->opcode, cmd->arg, pin_status);

	switch (mmc_resp_type(cmd)) {
		case MMC_RSP_NONE:
			rsptyp = SDC_CMD_RSPTYP_NONE;
			break;
		case MMC_RSP_R1:
			rsptyp = SDC_CMD_RSPTYP_R1;
			break;
		case MMC_RSP_R2:
			rsptyp = SDC_CMD_RSPTYP_R2;
			break;
		case MMC_RSP_R3:
			rsptyp = SDC_CMD_RSPTYP_R3;
			break;
		//case MMC_RSP_R6:
		//	rsptyp = SDC_CMD_RSPTYP_R6;
		//	break;
		case MMC_RSP_R1B:
			rsptyp = SDC_CMD_RSPTYP_R1B;
			break;
	}
    
    cmd_val |= (rsptyp << 7);

    if (mrq->data) {
        if (mrq->data->blocks > 1) {
            cmd_val |= SDC_CMD_DTYPE_MULTI;
        } else {
            cmd_val |= SDC_CMD_DTYPE_SINGLE;
        }
        
        if (mrq->data->flags & MMC_DATA_WRITE) {
            cmd_val |= SDC_CMD_RW;
        } else {
            cmd_val &= ~SDC_CMD_RW;
        }
    }

    host->mrq = mrq;
    reinit_completion(&host->cmd_done);
    if (mrq->data) {
        reinit_completion(&host->data_done);

        host->bytes_xfered = 0;
        if (mrq->data->flags & MMC_DATA_READ) {
            sg_miter_start(&host->miter, mrq->data->sg, mrq->data->sg_len, SG_MITER_TO_SG | SG_MITER_ATOMIC);
        } else if (mrq->data->flags & MMC_DATA_WRITE) {
            sg_miter_start(&host->miter, mrq->data->sg, mrq->data->sg_len, SG_MITER_FROM_SG | SG_MITER_ATOMIC);
        }

        writel(MSDC_STA_FIFOCLR, host->base + MSDC_STA);
        
        u32 cfg = readl(host->base + MSDC_CFG);
        
        cfg &= ~MSDC_CFG_FIFOTHD_M; 
        cfg |= (1 << 24); 
        writel(cfg, host->base + MSDC_CFG);

        mt62xx_msdc_setup_data(host, mrq); 
    }

    writel(0x00000000, host->base + SDC_IRQMASK0); 

    readl(host->base + SDC_CMDSTA);
    readl(host->base + MSDC_INT);
    readl(host->base + SDC_DATSTA); 

    writel(cmd->arg, host->base + SDC_ARG);
    writel(cmd_val, host->base + SDC_CMD);

    if (!wait_for_completion_timeout(&host->cmd_done, msecs_to_jiffies(1000))) {
        cmd->error = -ETIMEDOUT;
    } else {
        if (!cmd->error && (rsptyp != 0)) {
            if (rsptyp == SDC_CMD_RSPTYP_R2) { 
                cmd->resp[0] = readl(host->base + SDC_RESP3);
                cmd->resp[1] = readl(host->base + SDC_RESP2);
                cmd->resp[2] = readl(host->base + SDC_RESP1);
                cmd->resp[3] = readl(host->base + SDC_RESP0);
            } else {
                cmd->resp[0] = readl(host->base + SDC_RESP0);
            }
        }
    }

    if (mrq->data && !cmd->error) {
        pr_info("mt62xx-msdc: DATA transfer requested! blocks=%u, blksz=%u, flags=0x%x\n",
                mrq->data->blocks, mrq->data->blksz, mrq->data->flags);
                
        if (!wait_for_completion_timeout(&host->data_done, msecs_to_jiffies(3000))) {
            pr_err("mt62xx-msdc: DATA timeout!\n");
            mrq->data->error = -ETIMEDOUT;
        } else {
            pr_info("mt62xx-msdc: DATA completion signaled!\n");
        }
    } else if (mrq->data && cmd->error) {
        mrq->data->error = cmd->error;
    }

    if (mrq->data) {
        sg_miter_stop(&host->miter);
        mrq->data->bytes_xfered = host->bytes_xfered;
    }

    writel(0x00000000, host->base + SDC_IRQMASK0);
    mmc_request_done(mmc, mrq);
}

static void mt62xx_msdc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct mt62xx_sd_host *host = mmc_priv(mmc);
    u32 msdc_cfg = readl(host->base + MSDC_CFG);
    u32 sdc_cfg = readl(host->base + SDC_CFG);

    pr_info("mt62xx-msdc: set_ios: clk=%u, width=%u, pwr=%u\n", 
        ios->clock, ios->bus_width, ios->power_mode);

    if (ios->power_mode == MMC_POWER_OFF) {
        pr_info("mt62xx-msdc: IGNORING POWER_OFF to preserve VXP PMIC state!\n");
        //if (host->cg_base)
        //    writel(CG_MSDC_BIT, host->cg_base + CG_SET);
        return;
    } else {
        //if (host->cg_base)
        //    writel(CG_MSDC_BIT, host->cg_base + CG_CLR);
            
        msdc_cfg |= MSDC_CFG_VDDPD;
        msdc_cfg |= MSDC_CFG_MSDC;  
    }

    msdc_cfg &= ~0x0F000000;
    msdc_cfg |= 0x01000000;  

    sdc_cfg &= ~SDC_CFG_SIEN;
    writel(sdc_cfg, host->base + SDC_CFG);

    if (ios->clock) {
        msdc_cfg &= ~0xFF00;
        
        if (ios->clock <= 400000) {
            msdc_cfg |= (61 << 8);
        } else if (ios->clock <= 25000000) {
            msdc_cfg |= (1 << 8);
        } else {
            msdc_cfg |= (0 << 8); 
        }
    }

    msdc_cfg |= MSDC_CFG_INTEN;
    msdc_cfg |= MSDC_CFG_DIRQEN; 
    msdc_cfg |= MSDC_CFG_PINEN;
    writel(msdc_cfg, host->base + MSDC_CFG); 

    if (ios->bus_width == MMC_BUS_WIDTH_4)
        sdc_cfg |= SDC_CFG_MDLEN;
    else
        sdc_cfg &= ~SDC_CFG_MDLEN;
        
    sdc_cfg |= SDC_CFG_SIEN; 
    writel(sdc_cfg, host->base + SDC_CFG);
}

static const struct mmc_host_ops mt62xx_msdc_ops = {
    .request = mt62xx_msdc_request,
    .set_ios = mt62xx_msdc_set_ios,
};

static int mt62xx_msdc_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc;
    struct mt62xx_sd_host *host;
    struct resource *res_msdc;
    struct resource *res_cg;
    u32 iocon1;
    u32 cfg;
    int ret;
    
    mmc = mmc_alloc_host(sizeof(struct mt62xx_sd_host), &pdev->dev);
    if (!mmc)
        return -ENOMEM;

    host = mmc_priv(mmc);
    host->mmc = mmc;

    init_completion(&host->cmd_done);
    init_completion(&host->data_done);

    platform_set_drvdata(pdev, mmc);

    res_msdc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    host->base = devm_ioremap_resource(&pdev->dev, res_msdc);
    if (IS_ERR(host->base)) {
        ret = PTR_ERR(host->base);
        goto err_free_host;
    }

    res_cg = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    host->cg_base = devm_ioremap_resource(&pdev->dev, res_cg);
    if (IS_ERR(host->cg_base)) {
        pr_warn("mt62xx-msdc: Clock Gating config-regs not found, cg_base is NULL!\n");
        host->cg_base = NULL; 
    } else {
        writel((1<<3), host->cg_base + 0x324);
        //writel(0x1000, host->cg_base + 0x310);
        pr_info("mt62xx-msdc: Clock Gating OFF. Controller is AWAKE!\n");
    }

    writel(readl(host->base + MSDC_CFG) | MSDC_CFG_RST, host->base + MSDC_CFG);
    msleep(10);
    
    cfg = readl(host->base + MSDC_CFG);
    cfg &= ~MSDC_CFG_RST;
    cfg &= ~0x0F000000;
    cfg |= MSDC_CFG_VDDPD;
    cfg |= MSDC_CFG_MSDC;

	cfg |= MSDC_CFG_INTEN;       
    cfg |= MSDC_CFG_DIRQEN;      
    cfg |= MSDC_CFG_PINEN; 

    writel(cfg, host->base + MSDC_CFG);
    

    writel(0x01280109, host->base + MSDC_IOCON0); 
    writel(0x00020001, host->base + CLKPAD_RED);  

    iocon1 = readl(host->base + MSDC_IOCON1);
    
    iocon1 &= ~0x40000;
    iocon1 &= ~0x30000; iocon1 |= 0x20000;
    
    iocon1 &= ~0x400;
    iocon1 &= ~0x300;   iocon1 |= 0x200;
    
    iocon1 &= ~0x40;
    iocon1 &= ~0x30;    iocon1 |= 0x20;
    
    writel(iocon1, host->base + MSDC_IOCON1);

    u32 sdc_cfg_init = readl(host->base + SDC_CFG);
    sdc_cfg_init &= ~(1 << 17); 
    writel(sdc_cfg_init, host->base + SDC_CFG);

    writel(7, host->base + MSDC_PS);
    writel(0xFFFFFFFF, host->base + SDC_IRQMASK0);

    host->irq = platform_get_irq(pdev, 0);
    if (host->irq < 0) {
        ret = host->irq;
        goto err_free_host;
    }

    ret = devm_request_irq(&pdev->dev, host->irq, mt62xx_msdc_irq,
               0, DRIVER_NAME, host);
    if (ret)
        goto err_free_host;

    mmc->ops = &mt62xx_msdc_ops;
    mmc->f_min = 400000; 
    mmc->f_max = 24000000;
    mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
    mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_NONREMOVABLE;

	mmc->max_blk_count = 1;
    mmc->max_req_size = 512;
    mmc->max_segs = 1;
    mmc->max_seg_size = 512;

    ret = mmc_add_host(mmc);
    if (ret)
        goto err_free_host;

    pr_info("MT62xx MSDC host probed successfully\n");
    return 0;

err_free_host:
    mmc_free_host(mmc);
    return ret;
}

static int mt62xx_msdc_remove(struct platform_device *pdev)
{
    struct mmc_host *mmc = platform_get_drvdata(pdev);

    mmc_remove_host(mmc);
    mmc_free_host(mmc);
    return 0;
}

static struct platform_driver mt62xx_msdc_driver = {
    .probe      = mt62xx_msdc_probe,
    .remove     = mt62xx_msdc_remove,
    .driver     = {
        .name   = DRIVER_NAME,
    },
};

module_platform_driver(mt62xx_msdc_driver);

MODULE_DESCRIPTION("MediaTek MT62xx SD/MMC Card Interface driver");
MODULE_LICENSE("GPL v2");