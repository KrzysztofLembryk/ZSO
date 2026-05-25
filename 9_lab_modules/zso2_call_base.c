#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");

extern void base_fun(void);

static int
cb_init_module(void)
{
	printk(KERN_WARNING "call_base: calling base()\n");
	base_fun();
	return 0;
}

static void
cb_cleanup_module(void)
{
}

module_init(cb_init_module);
module_exit(cb_cleanup_module);
