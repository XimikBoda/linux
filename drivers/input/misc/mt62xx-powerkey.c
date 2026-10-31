/*
 * MT62XX Power Button Driver (A-DIE EINT0, polled)
 *
 * The power button on the Nokia 225 is connected to the Analog Die (A-DIE)
 * external interrupt 0 (ADIE_EINT_PWRKEY_INDEX). It does NOT appear in the
 * keypad matrix registers.
 *
 * Behavior: Each press emits Ctrl+C to interrupt the current shell command.
 * One Ctrl+C per press (not spammed on hold).
 *
 * The D-DIE D2D interrupt bridge causes an interrupt storm when enabled,
 * so we poll the A-DIE EINT status register at 100ms intervals instead.
 *
 * Register map:
 *   A-DIE CIRQ base:     0xA0760000
 *   ADIE_EINT_STATUS0:   0xA0760300  (bit 0 = power key)
 *   ADIE_EINT_INTACK0:   0xA0760308  (write 1 to ack/clear latched bit)
 *   ADIE_EINT_SENS_CLR0: 0xA0760340  (write 1 to clear edge-sensitive bit)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#define DRIVER_NAME "mt62xx-powerkey"

#define ADIE_EINT_STATUS0   0x0300
#define ADIE_EINT_INTACK0   0x0308
#define ADIE_EINT_SENS_CLR0 0x0340
#define PWRKEY_STATUS_BIT   0x0001
#define POLL_INTERVAL_MS    100

struct mt62xx_powerkey {
	void __iomem *adie_base;
	struct input_dev *input;
	struct delayed_work poll_work;
};

static void emit_ctrl_c(struct mt62xx_powerkey *pk)
{
	input_report_key(pk->input, KEY_LEFTCTRL, 1);
	input_sync(pk->input);

	input_report_key(pk->input, KEY_C, 1);
	input_sync(pk->input);

	input_report_key(pk->input, KEY_C, 0);
	input_sync(pk->input);

	input_report_key(pk->input, KEY_LEFTCTRL, 0);
	input_sync(pk->input);
}

static void mt62xx_powerkey_poll(struct work_struct *work)
{
	struct mt62xx_powerkey *pk = container_of(
		to_delayed_work(work), struct mt62xx_powerkey, poll_work);
	u16 status;

	status = readw_relaxed(pk->adie_base + ADIE_EINT_STATUS0);

	if (status & PWRKEY_STATUS_BIT) {
		/* ACK clears the latched bit — one Ctrl+C per press edge */
		writew_relaxed(PWRKEY_STATUS_BIT, pk->adie_base + ADIE_EINT_INTACK0);
		emit_ctrl_c(pk);
	}

	queue_delayed_work(system_wq, &pk->poll_work,
			     msecs_to_jiffies(POLL_INTERVAL_MS));
}

static int mt62xx_powerkey_probe(struct platform_device *pdev)
{
	struct mt62xx_powerkey *pk;
	struct input_dev *input;
	struct resource *res;
	int ret;

	pk = devm_kzalloc(&pdev->dev, sizeof(*pk), GFP_KERNEL);
	if (!pk)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "adie-regs");
	if (!res) {
		dev_err(&pdev->dev, "no adie-regs resource\n");
		return -ENODEV;
	}

	pk->adie_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pk->adie_base))
		return PTR_ERR(pk->adie_base);

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "mt62xx-powerkey";
	input->phys = "mt62xx-powerkey/input0";
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0002;
	input->id.version = 0x0100;

	set_bit(EV_KEY, input->evbit);
	set_bit(KEY_LEFTCTRL, input->keybit);
	set_bit(KEY_C, input->keybit);

	ret = input_register_device(input);
	if (ret) {
		dev_err(&pdev->dev, "failed to register input device: %d\n", ret);
		return ret;
	}

	pk->input = input;

	/* Configure EINT0 as level-sensitive (clear edge-sensitive bit) */
	writew_relaxed(0x0001, pk->adie_base + ADIE_EINT_SENS_CLR0);

	INIT_DELAYED_WORK(&pk->poll_work, mt62xx_powerkey_poll);
	queue_delayed_work(system_wq, &pk->poll_work,
			     msecs_to_jiffies(POLL_INTERVAL_MS));

	platform_set_drvdata(pdev, pk);

	dev_info(&pdev->dev, "probe SUCCESS (polling A-DIE EINT0, press=Ctrl+C)\n");

	return 0;
}

static int mt62xx_powerkey_remove(struct platform_device *pdev)
{
	struct mt62xx_powerkey *pk = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&pk->poll_work);

	return 0;
}

static struct platform_driver mt62xx_powerkey_driver = {
	.probe  = mt62xx_powerkey_probe,
	.remove = mt62xx_powerkey_remove,
	.driver = { .name = DRIVER_NAME },
};

module_platform_driver(mt62xx_powerkey_driver);

MODULE_AUTHOR("gvl610");
MODULE_DESCRIPTION("MT62XX A-DIE Power Button Driver (polling, press=Ctrl+C)");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);