#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

struct workqueue_irq_dev {
    struct gpio_desc *button;
    int irq;
    struct work_struct work;
};

static unsigned long busy_counter = 50000000;
module_param(busy_counter, ulong, 0644);

static void work_handler(struct work_struct *work)
{
    volatile unsigned long i;
    for (i=0; i<busy_counter; i++)
        cpu_relax();
}

static irqreturn_t workqueue_irq_handler(int irq, void *dev_id)
{
    struct workqueue_irq_dev *priv = dev_id;

    if (!schedule_work(&priv->work))
        pr_debug("work already pending\n");
    return IRQ_HANDLED;
}

static int workqueue_irq_probe(struct platform_device *pdev)
{
    struct workqueue_irq_dev *priv;
    int ret;

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);

    if (!priv)
        return -ENOMEM;
    
    platform_set_drvdata(pdev, priv);
    priv->button = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);

    if (IS_ERR(priv->button))
        return dev_err_probe(&pdev->dev, PTR_ERR(priv->button), "Failed to get button GPIO\n");
    
    priv->irq = gpiod_to_irq(priv->button);
    if (priv->irq < 0)
        return priv->irq;

    INIT_WORK(&priv->work, work_handler);
    ret = devm_request_irq(&pdev->dev,
                            priv->irq,
                            workqueue_irq_handler,
                            IRQF_TRIGGER_FALLING,
                            dev_name(&pdev->dev),
                            priv);    

    if (ret)
        return dev_err_probe(&pdev->dev, ret, "Failed to request IRQ %d\n", priv->irq);
    
    dev_info(&pdev->dev, "Request IRQ %d successfully\n", priv->irq);
    return 0;
}

static void workqueue_irq_remove(struct platform_device *pdev)
{
    struct workqueue_irq_dev *priv = platform_get_drvdata(pdev);
    cancel_work_sync(&priv->work);
}

static const struct of_device_id workqueue_irq_of_match[] = {
    { 
        .compatible = "kmwook,irq", 
    },
    {}
};

MODULE_DEVICE_TABLE(of, workqueue_irq_of_match);

static struct platform_driver workqueue_irq_driver = {
    .probe = workqueue_irq_probe,
    .remove = workqueue_irq_remove,
    .driver = {
        .name = "workqueue-irq",
        .of_match_table = workqueue_irq_of_match,
    },
};

module_platform_driver(workqueue_irq_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kmwook");
MODULE_DESCRIPTION("A simple platform driver that demonstrates a workqueue interrupt handler");
