#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

static int gpio = 17;
module_param(gpio, int, 0444);

MODULE_PARM_DESC(gpio, "GPIO number");

static int irq_number = -1;

static int __init bad_irq_init(void)
{
    irq_number = gpio_to_irq(gpio);

    if (irq_number < 0) {
        pr_err("Failed to get IRQ number for GPIO %d\n", gpio);
        return irq_number;
    }
    
    pr_info("Mapped GPIO%d to IRQ%d\n", gpio, irq_number);
    
    return 0;
}

static void __exit bad_irq_exit(void)
{
    pr_info("Exiting bad_irq module\n");
}

module_init(bad_irq_init);
module_exit(bad_irq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kmwook");
MODULE_DESCRIPTION("A simple Linux kernel module to demonstrate bad IRQ handling");
