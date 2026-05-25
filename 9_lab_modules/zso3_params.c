#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");

static char *msg = "Hello world";
module_param(msg, charp, 0644);
MODULE_PARM_DESC(msg, "My message");

int num_msgs = 1;
char *msgs[2] = {"msg1" , NULL};
module_param_array(msgs, charp, &num_msgs, 0000);
MODULE_PARM_DESC(msgs, "Array of messages [2]");

static int
params_init_module(void)
{
	int i;
	printk(KERN_WARNING "Init: %s\n", msg);

	for (i = 0; i < num_msgs; ++i)
	{
		printk(KERN_WARNING "Msg[%d]: %s\n", i, msgs[i]);
	}

	return 0;
}

static void
params_cleanup_module(void)
{
	printk(KERN_WARNING "Exit: %s\n", msg);
}

module_init(params_init_module);
module_exit(params_cleanup_module);
