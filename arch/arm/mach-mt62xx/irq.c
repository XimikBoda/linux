#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <asm/exception.h>
#include <asm/mach/irq.h>

#define CIRQ_BASE 0xA0060000

#define IRQ_MASK_SET0   (CIRQ_BASE + 0x0020)
#define IRQ_MASK_SET1   (CIRQ_BASE + 0x0024)
#define IRQ_MASK_CLR0   (CIRQ_BASE + 0x0040)
#define IRQ_MASK_CLR1   (CIRQ_BASE + 0x0044)

#define IRQ_ASTA0       (CIRQ_BASE + 0x0120)
#define IRQ_ASTA1       (CIRQ_BASE + 0x0124)
#define IRQ_EOI0        (CIRQ_BASE + 0x0160)
#define IRQ_EOI1        (CIRQ_BASE + 0x0164)
#define IRQ_STA2        (CIRQ_BASE + 0x01A0)
#define IRQ_EOI2        (CIRQ_BASE + 0x01A8)


static void mt62xx_irq_mask(struct irq_data *d)
{
    unsigned int irq = d->irq;
    if (irq < 32)
        __raw_writel(1 << irq, (void *)IRQ_MASK_SET0);
    else
        __raw_writel(1 << (irq - 32), (void *)IRQ_MASK_SET1);
}

static void mt62xx_irq_unmask(struct irq_data *d)
{
    unsigned int irq = d->irq;
    if (irq < 32)
        __raw_writel(1 << irq, (void *)IRQ_MASK_CLR0);
    else
        __raw_writel(1 << (irq - 32), (void *)IRQ_MASK_CLR1);
}

static void mt62xx_irq_ack(struct irq_data *d)
{
    unsigned int irq = d->irq;
    if (irq < 32)
        __raw_writel(1 << irq, (void *)IRQ_EOI0);
    else
        __raw_writel(1 << (irq - 32), (void *)IRQ_EOI1);
}

void mt62xx_handle_irq(struct pt_regs *regs)
{
    u32 irq_bin = __raw_readl((void *)IRQ_STA2);

    if (irq_bin & (1 << 8))
        return;

    int irq = irq_bin & 0xFF;

    generic_handle_irq(irq);

    __raw_writel(irq, (void *)IRQ_EOI2);
    __raw_readl((void *)IRQ_EOI2);
}

static struct irq_chip mt62xx_irq_chip = {
    .name       = "MT62XX-CIRQ",
    .irq_mask   = mt62xx_irq_mask,
    .irq_unmask = mt62xx_irq_unmask,
    .irq_ack    = mt62xx_irq_ack,
};

void __init mt62xx_init_irq(void)
{
    int  i;

    __raw_writel(0xFFFFFFFF, (void *)IRQ_MASK_SET0);
    __raw_writel(0xFFFFFFFF, (void *)IRQ_MASK_SET1);

    __raw_writel(0xFFFFFFFF, (void *)IRQ_EOI0);
    __raw_writel(0xFFFFFFFF, (void *)IRQ_EOI1);

    for (i = 0; i < NR_IRQS; i++) {
        irq_set_chip_and_handler(i, &mt62xx_irq_chip, handle_level_irq);
        irq_clear_status_flags(i, IRQ_NOREQUEST | IRQ_NOPROBE);
    }

    set_handle_irq(mt62xx_handle_irq);
}
