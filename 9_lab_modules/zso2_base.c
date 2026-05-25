#include <linux/module.h>
#include <linux/kernel.h>

void base_fun(void);

MODULE_LICENSE("GPL");

void base_fun(void)
{
	printk(KERN_EMERG "base1: called base_fun\n");
}

static int
base_init_module(void)
{
	printk(KERN_INFO "Loaded base\n");
	return 0;
}

static void
base_cleanup_module(void)
{
	printk(KERN_INFO "Removed base\n");
}

module_init(base_init_module);
module_exit(base_cleanup_module);

EXPORT_SYMBOL(base_fun);
