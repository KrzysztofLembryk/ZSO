#include "tapedev.h"
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>

#define TAPEDEV_MAX_DEVICES 256
#define BASE_MINOR 0
#define MINORS_COUNT 8
#define TAPEDEV_NAME "tapedev"

struct tapedev_dev {
    struct gendisk *gd;
	// struct pci_dev *pdev;
	// int idx;
	// struct device *dev;
	// void __iomem *bar;
	// spinlock_t slock;
	// struct list_head buffers_free;
	// struct list_head buffers_running;
	// wait_queue_head_t free_wq;
	// wait_queue_head_t idle_wq;
}; 

// static struct tapedev_device *tapedev_devices[TAPEDEV_MAX_DEVICES];
// static DEFINE_MUTEX(tapedev_devices_lock);
// static struct class tapedev_class = {
// 	.name = "tapedev",
// };

// place worth looking at in linux src is ps3disk.c, ps3vram.c
// for queue_limits exmpl: zram_drv.c
static struct tapedev_dev dev; 
static dev_t tapedev_major;

// good link with blk dev: https://olegkutkov.me/2020/02/10/linux-block-device-driver/

static int create_tape_device(struct tapedev_dev *dev)
{
	int err = 0;
	// Queue limits structure: 
	// defines the hardware and software constraints for a block device's request 
	// queue. It specifies properties like block sizes, alignment, maximum I/O size, 
	// and other limits that the block layer and drivers must respect when handling 
	// I/O requests
	struct queue_limits lim = {
		.logical_block_size		= 512, // TODO - should be based on some tape type
		/*
			* To ensure that we always get PAGE_SIZE aligned and
			* n*PAGE_SIZED sized I/O requests.
			*/
		.physical_block_size		= PAGE_SIZE,
		.io_min				= PAGE_SIZE,
		.io_opt				= PAGE_SIZE,
	};

	dev->gd = blk_alloc_disk(&lim, NUMA_NO_NODE);

	if (IS_ERR(dev->gd)) 
	{
		err = PTR_ERR(dev->gd);
		pr_err("%s :: blk_alloc_disk failed with error: %d\n", __func__, err);
		return err;
	}

	return 0;
}

static int init_tapedev(void)
{
    int err = 0;

	err = register_blkdev(0, TAPEDEV_NAME);
	if (err <= 0) 
	{
		pr_err("%s:%u: register_blkdev failed %d\n", __func__,
		       __LINE__, err);
		goto fail;
	}

    tapedev_major = err;

	pr_info("%s:%u: registered block device major %d\n", __func__,
		__LINE__, tapedev_major);

	err = create_tape_device(&dev);

	if (err < 0)
	{
		pr_err("%s:%u: create_tape_device failed %d\n", __func__,
		       __LINE__, err);
		goto out_blkdev_cleanup;
	}

	// add_disk()
    return 0;

out_blkdev_cleanup:
	unregister_blkdev(tapedev_major, TAPEDEV_NAME);
fail:
	return err;
}

static void cleanup_tapedev(void)
{
    unregister_blkdev(tapedev_major, TAPEDEV_NAME);
}


module_init(init_tapedev);
module_exit(cleanup_tapedev);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krzysztof Lembryk");
MODULE_DESCRIPTION("Driver for tapedevice");