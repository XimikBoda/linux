#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT62XX_RTC_TC_SEC    0x14
#define MT62XX_RTC_TC_MIN    0x18
#define MT62XX_RTC_TC_HOU    0x1C
#define MT62XX_RTC_TC_DOM    0x20
#define MT62XX_RTC_TC_DOW    0x24
#define MT62XX_RTC_TC_MTH    0x28
#define MT62XX_RTC_TC_YEA    0x2C



struct mt62xx_rtc {
    void __iomem *base;
};

static int mt62xx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
    struct mt62xx_rtc *rtc = dev_get_drvdata(dev);

    tm->tm_sec  = readw(rtc->base + MT62XX_RTC_TC_SEC);
    tm->tm_min  = readw(rtc->base + MT62XX_RTC_TC_MIN);
    tm->tm_hour = readw(rtc->base + MT62XX_RTC_TC_HOU);
    tm->tm_mday = readw(rtc->base + MT62XX_RTC_TC_DOM);
    tm->tm_mon  = readw(rtc->base + MT62XX_RTC_TC_MTH) - 1;
    tm->tm_year = readw(rtc->base + MT62XX_RTC_TC_YEA) + 100;

    return 0;
}

static int mt62xx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
    struct mt62xx_rtc *rtc = dev_get_drvdata(dev);

    //todo

    return 0;
}

static const struct rtc_class_ops mt62xx_rtc_ops = {
    .read_time = mt62xx_rtc_read_time,
    .set_time  = mt62xx_rtc_set_time,
};

static int mt62xx_rtc_probe(struct platform_device *pdev)
{
    struct mt62xx_rtc *rtc;
    struct rtc_device *rtc_dev;
    struct resource *res;

    rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
    if (!rtc) return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    rtc->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(rtc->base)) return PTR_ERR(rtc->base);

    platform_set_drvdata(pdev, rtc);

    rtc_dev = devm_rtc_device_register(&pdev->dev, pdev->name,
                                       &mt62xx_rtc_ops, THIS_MODULE);
    if (IS_ERR(rtc_dev)) return PTR_ERR(rtc_dev);

    return 0;
}

static struct platform_driver mt62xx_rtc_driver = {
    .probe = mt62xx_rtc_probe,
    .driver = {
        .name = "mt62xx-rtc",
    },
};
module_platform_driver(mt62xx_rtc_driver);
MODULE_LICENSE("GPL");