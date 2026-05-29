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
#include <linux/pci.h>

#define TAPEDEV_MAX_DEVICES 256
#define BASE_MINOR 0
#define MINORS_COUNT 8
#define TAPEDEV_NAME "tapedev"

// Our workflow will be like that, in init we only allocate major nbr, create class 
// and init PCI -- adding DISKS for our blkd dev will happen inside PROBE function

struct tapedev_dev {
    struct gendisk *gdisk;
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
// for queue_limits exmpl: zram_drv.c, really short blk dev impl: nfblock
static struct tapedev_dev dev; 
static dev_t tapedev_major;

static struct class tapedev_class = {
	.name = "tapedev",
};

// #################################################################################
// ############################## HANDLING INTERRUPTS ##############################
// #################################################################################

// 1) For kernel to allow us to use interrupts we need to invoke request_irq 
// function, to do so we need to implement interrupt handler:
// int request_irq(
// 	unsigned int irq, -- interrupt number being requested
// 	irqreturn_t (*handler)(int, void *, struct pt_regs *), -- ptr to handling func
// 	unsigned long flags, -- options related to interrupt management
// 	const char *dev_name, 
// 	void *dev_id -- often points to our device struct: dev
// );

// static irqreturn_t tapedev_interrupt_handler(int irq, void *ptr_dev)
// {

// }

// good link with blk dev: https://olegkutkov.me/2020/02/10/linux-block-device-driver/

static int tapedev_disk_open(struct gendisk *disk, fmode_t mode)
{
    //...

    return 0;
}

static void tapedev_disk_release(struct gendisk *gd)
{
    //...

    return;
}

static const struct block_device_operations tapedev_ops = {
	.owner	= THIS_MODULE,
	.open = tapedev_disk_open,
	.release = tapedev_disk_release,
};

// static int alloc_gdisk_for_tapedev(struct tapedev_dev *dev)
// {
// 	int err = 0;
// 	// Queue limits structure: 
// 	// defines the hardware and software constraints for a block device's request 
// 	// queue. It specifies properties like block sizes, alignment, maximum I/O size, 
// 	// and other limits that the block layer and drivers must respect when handling 
// 	// I/O requests
// 	// struct queue_limits lim = {
// 	// 	.logical_block_size		= 512, // TODO - should be based on some tape type
// 	// 	/*
// 	// 		* To ensure that we always get PAGE_SIZE aligned and
// 	// 		* n*PAGE_SIZED sized I/O requests.
// 	// 		*/
// 	// 	// .physical_block_size		= PAGE_SIZE,
// 	// 	// .io_min				= PAGE_SIZE,
// 	// 	// .io_opt				= PAGE_SIZE,
// 	// };

// 	// dev->gdisk = blk_alloc_disk(&lim, NUMA_NO_NODE);
// 	dev->gdisk = blk_alloc_disk(NULL, NUMA_NO_NODE);

// 	if (dev->gdisk == NULL)
// 	{
// 		pr_err("%s :: blk_alloc_disk returned NULL\n", __func__);
// 		return -1;
// 	}

// 	if (IS_ERR(dev->gdisk)) 
// 	{
// 		err = PTR_ERR(dev->gdisk);
// 		pr_err("%s :: blk_alloc_disk failed with error: %d\n", __func__, err);
// 		return err;
// 	}

// 	return 0;
// }



static struct pci_device_id tapedev_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TAPEDEV, PCI_DEVICE_ID_TAPEDEV) },
	{ 0 }
};

static struct pci_driver tapedev_pci_driver = {
	.name = "tapedev",
	.id_table = tapedev_pci_ids,
	// .probe = adlerdev_probe,
	// .remove = adlerdev_remove,
	// .suspend = adlerdev_suspend,
	// .resume = adlerdev_resume,
};

// static int blk_dev_init(void)
// {
//     int err = 0;

// 	pr_info("%s:%u: before alloc_gdisk_for_tapedev, err: %d\n", __func__, __LINE__, err);
// 	err = alloc_gdisk_for_tapedev(&dev);

// 	if (dev.gdisk == NULL)
// 	{
// 		pr_info("%s:%u: after alloc_gdisk_for_tapedev dev.gdisk == NULL\n", __func__, __LINE__);
// 		return -1;
// 	}

// 	pr_info("%s:%u: after alloc_gdisk_for_tapedev, err: %d\n", __func__, __LINE__, err);

// 	if (err < 0)
// 	{
// 		pr_err("%s:%u: create_tape_device failed %d\n", __func__,
// 		       __LINE__, err);
// 		goto out_blkdev_cleanup;
// 	}
// 	dev.gdisk->major = tapedev_major;
// 	dev.gdisk->first_minor = 0;
// 	dev.gdisk->minors = 1;
// 	dev.gdisk->fops = &tapedev_ops;
// 	dev.gdisk->private_data = &dev;
// 	snprintf(dev.gdisk->disk_name, 10, "tapedev%d", 1);

// 	pr_info("%s:%u: before add_disk, err: %d\n", __func__, __LINE__, err);

// 	err = add_disk(dev.gdisk);

// 	pr_info("%s:%u: after add_disk, err: %d\n", __func__, __LINE__, err);

// 	if (err < 0)
// 	{
// 		pr_err("%s:%u: add_disk failed: %d\n", __func__,
// 		       __LINE__, err);
// 		goto out_add_disk_cleanup;
// 	}

// 	pr_info("Added device: %s\n", dev.gdisk->disk_name);

// 	return 0;

// out_add_disk_cleanup:
// 	// put_disk decrements gendisk refcount, if it reaches 0 gendisk is freed, since
// 	// we've just allocated gendisk struct there might at most 1 ref to it thus
// 	// put_disk will free dev.gendisk
// 	put_disk(dev.gdisk);
// out_blkdev_cleanup:
// 	unregister_blkdev(tapedev_major, TAPEDEV_NAME);

// 	return err;
// }

static int init_tapedev(void)
{
	int err;

	err = register_blkdev(0, TAPEDEV_NAME);
	if (err <= 0) 
	{
		pr_err("%s:%u: unable to get major nbr, register_blkdev failed %d\n", __func__, __LINE__, err);
		goto fail;
	}

    tapedev_major = err;

	pr_info("%s:%u: registered block device major %d\n", __func__,
		__LINE__, tapedev_major);

	if ((err = class_register(&tapedev_class)))
	{
		pr_err("%s:%u: class_register failed, err:  %d\n", __func__, __LINE__, err);
		goto blkdev_cleanup;
	}
	
	if ((err = pci_register_driver(&tapedev_pci_driver)))
	{
		pr_err("%s:%u: pci_register_driver failed, err:  %d\n", __func__, __LINE__, err);
		goto err_pci;
	}
	return 0;

err_pci:
	class_unregister(&tapedev_class);
blkdev_cleanup:
	unregister_blkdev(tapedev_major, TAPEDEV_NAME);
fail:
	return err;
}

static void cleanup_tapedev(void)
{
	class_unregister(&tapedev_class);
	unregister_blkdev(tapedev_major, TAPEDEV_NAME);

	// del_gendisk(dev.gdisk);
	// put_disk(dev.gdisk);
}
module_init(init_tapedev);
module_exit(cleanup_tapedev);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krzysztof Lembryk");
MODULE_DESCRIPTION("Driver for tapedevice");