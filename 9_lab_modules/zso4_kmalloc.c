#include <linux/module.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static char *msg = "Hello world";
module_param(msg, charp, 0);
MODULE_PARM_DESC(msg,"My message");

int repeats = 1;
module_param(repeats, int, 0);
MODULE_PARM_DESC(repeats,"Number of repeats of message");

#define MAX_SIZE 128*1024

static char *buf = NULL;

static int kmalloc_init_module(void)
{
	int i;
	int len;
	int bufsize;

	printk(KERN_WARNING "Init: %s*%d\n", msg, repeats);

	len = strlen(msg);

	bufsize = len*repeats + 1;

	if (bufsize > MAX_SIZE)
	{
		printk(KERN_WARNING "Too many bytes %d, max is %d\n", bufsize, MAX_SIZE);
		return -EINVAL;
	}

	if (bufsize <= 0)
	{
		printk(KERN_WARNING "Wrong size of buffer bytes %d\n", bufsize);
		return -EINVAL;
	}

	buf = kmalloc(bufsize, GFP_KERNEL);

	if (buf == NULL)
	{
		printk(KERN_WARNING "Failed to allocate %d bytes\n", bufsize);
		return -ENOMEM;
	}

	/* Add trailing NULL, just in case. */
	buf[0] = '\0';

	for (i = 0; i < repeats; ++i)
	{
		strcat(buf, msg);
	}
	printk(KERN_INFO "Message: %s\n", buf);

	return 0;
}

static void kmalloc_cleanup_module(void)
{
	printk(KERN_WARNING "Exit: %s\n", buf);
	kfree(buf);
	return;
}

module_init(kmalloc_init_module);
module_exit(kmalloc_cleanup_module);
