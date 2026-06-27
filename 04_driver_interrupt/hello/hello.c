#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kmwook");
MODULE_DESCRIPTION("Hello Kernel Module");
MODULE_VERSION("1.0");

static int __init hello_init(void)
{
    pr_info("Hello, Kernel Module Loaded!\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("Goodbye, Kernel Module!\n");
}

module_init(hello_init);
module_exit(hello_exit);
