#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <asm/mach/time.h>

#define GPT_BASE            0xA00C0000
#define GPTIMER1_CON        (GPT_BASE + 0x0000)
#define GPTIMER1_DAT        (GPT_BASE + 0x0004)
#define GPTIMER_STA         (GPT_BASE + 0x0010)
#define GPTIMER1_PRESCALER  (GPT_BASE + 0x0014)

extern void printascii(const char *);

static struct clock_event_device mt62xx_clockevent;

static irqreturn_t mt62xx_timer_interrupt(int irq, void *dev_id)
{
    struct clock_event_device *evt = &mt62xx_clockevent;
    volatile unsigned int sta;

    sta = __raw_readl((void *)GPTIMER_STA);
    evt->event_handler(evt);

    return IRQ_HANDLED;
}

static struct irqaction mt62xx_timer_irq = {
    .name       = "MT6250-GPT1",
    .flags      = IRQF_TIMER | IRQF_IRQPOLL,
    .handler    = mt62xx_timer_interrupt,
};

static struct clock_event_device mt62xx_clockevent = {
    .name       = "mt62xx_gpt1",
    .features   = CLOCK_EVT_FEAT_PERIODIC,
    .rating     = 200,
};

void __init mt62xx_timer_init(void)
{
    __raw_writew(0, (void *)GPTIMER1_CON);       __raw_readw((void *)GPTIMER1_CON);
    __raw_writew(0, (void *)GPTIMER1_PRESCALER); __raw_readw((void *)GPTIMER1_PRESCALER);
    __raw_writew(164, (void *)GPTIMER1_DAT);     __raw_readw((void *)GPTIMER1_DAT);

    setup_irq(IRQ_GPT_LINE, &mt62xx_timer_irq);

    __raw_writew(0xC000, (void *)GPTIMER1_CON);  __raw_readw((void *)GPTIMER1_CON);
    __raw_writew(0xC000, (void *)GPTIMER1_CON);  __raw_readw((void *)GPTIMER1_CON);

    clockevents_config_and_register(&mt62xx_clockevent, 16384, 1, 0xffff);
}
