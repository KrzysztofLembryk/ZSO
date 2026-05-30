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

#define MAX_DEVICES_TAPEDEV 256
#define BASE_MINOR 0
#define MINORS_COUNT 8
#define TAPEDEV_NAME "tapedev"
#define BAR_ID 0
#define BAR_MAXLEN 0

int msg_once = 1;
static dev_t tapedev_major;
// Our workflow will be like that, in init we only allocate major nbr, create class 
// and init PCI -- adding DISKS for our blkd dev will happen inside PROBE function

// We will make an array of tapedev_devices, each will have its own gdisk etc
struct tapedev_device {
	struct device *dev; // used to store device_create(...) return value
	struct gendisk *gdisk;
	struct pci_dev *pdev;
	void __iomem *bar;
	int idx;
	spinlock_t s_lock;
	// void __iomem *bar;
	// spinlock_t slock;
	// struct list_head buffers_free;
	// struct list_head buffers_running;
	// wait_queue_head_t free_wq;
	// wait_queue_head_t idle_wq;
}; 

// static struct tapedev_device *tapedev_devices[MAX_DEVICES_TAPEDEV];
// static DEFINE_MUTEX(tapedev_devices_lock);
// static struct class tapedev_class = {
// 	.name = "tapedev",
// };

// place worth looking at in linux src is ps3disk.c, ps3vram.c
// for queue_limits exmpl: zram_drv.c, really short blk dev impl: nfblock
// Global static variables are initialized to NULL, so now we have array of NULLs
static struct tapedev_device *tapedev_devices[MAX_DEVICES_TAPEDEV]; 
static DEFINE_MUTEX(tapedev_devices_lock);

static struct class tapedev_class = {
	.name = "tapedev",
};

static inline void tapedev_iow(struct tapedev_device *dev, uint32_t reg, uint32_t val)
{
	iowrite32(val, dev->bar + reg);
	// printk(KERN_ALERT "tapedev_iow :: tapedev %03x <- %08x\n", reg, val);
}

static inline uint32_t tapedev_ior(struct tapedev_device *dev, uint32_t reg)
{
	uint32_t res = ioread32(dev->bar + reg);
	// printk(KERN_ALERT "tapedev_ior :: tapedev %03x -> %08x (res)\n", reg, res);
	return res;
}

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

static irqreturn_t tapedev_interrupt_handler(int irq, void *opaque_dev)
{
	// When invoking request_irq we passed tapedev_device*, however internally it was
	// cast to void* and now we get this void* in interrupt handling function 
	// so we need to cast it back
	struct tapedev_device *dev = opaque_dev;
	unsigned long flags;
	uint32_t istatus;
	uint32_t num_sections, loaded_tape_id;
	

	// saves the interrupt state before taking the spin lock
	spin_lock_irqsave(&dev->s_lock, flags);

	// We check if initialization of device ended by checking interrupt status value 
	// in our device and if it is TAPEDEV_IRQ_INIT_DONE 
	istatus = tapedev_ior(dev, TAPEDEV_IRQ_STATUS_ADDR);
	num_sections = tapedev_ior(dev, TAPEDEV_SECTIONS_ADDR);
	loaded_tape_id = tapedev_ior(dev, TAPEDEV_SECT_TAPE_NO_ADDR);

	if (msg_once)
	{
		pr_warn("tapedev_interrupt_handler :: device has: %u sections\n", num_sections);
		pr_warn("tapedev_interrupt_handler :: loaded tape id: %u \n", loaded_tape_id);
		msg_once = 0;

		// TAPEDEV_IRQ_INIT_DONE is bit idx at which information is stored,
		// so we check if its 1 meaning init is indeed done
		if ((istatus & (1 << TAPEDEV_IRQ_INIT_DONE)) == 1)
		{
			pr_warn("tapedev_interrupt_handler :: device started successfully\n");
		}
	}
	spin_unlock_irqrestore(&dev->s_lock, flags);

	return IRQ_RETVAL(istatus);
}

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

/* PCI driver.  */

static int tapedev_probe(
	struct pci_dev *pdev,
	const struct pci_device_id *pci_id
)
{
	// ### Vars declaration ###
	// When OS discovers that tape device is connect to PCI BUS (and it is since we
	// have modified QEMU installed) it invokes this function and passes tape 
	// device's struct (which holds all info about the device) which is pdev
	int err;
	int i;

	// ### Allocate our structure ###
	// So now is the time to allocate our internal device structure in which we will 
	// store all information about this tape device we need for our driver to work
	struct tapedev_device *tape_dev = kzalloc(sizeof *tape_dev, GFP_KERNEL);
	if (!tape_dev) {
		err = -ENOMEM;
		goto fail;
	}

	// pdev structure was created and filled by kernel when it discovered device;
	// by using this function we can modify private_data of pdev to point to our 
	// dev structure, so that whenever we get pdev we can also get our dev struct
	pci_set_drvdata(pdev, tape_dev);
	// In our dev struct we also need to remember pdev for the same reasons
	tape_dev->pdev = pdev;

	// init locks mutexes etc
	spin_lock_init(&tape_dev->s_lock);

	// lock needed here since we may have many tapedevs added simultaneously
	// We allow many tapedev devices, but every such device we need to store 
	// somewhere, so now we find first free index for our newly created device
	mutex_lock(&tapedev_devices_lock);
	for (i = 0; i < MAX_DEVICES_TAPEDEV; i++)
	{
		// given spot is free so we break
		if (tapedev_devices[i] == NULL)
			break;
	}

	// We didnt find free spot after iterating over whole array
	if (i == MAX_DEVICES_TAPEDEV)
	{
		err = -ENOSPC;
		mutex_unlock(&tapedev_devices_lock);
		goto dealloc_tapedev;
	}

	// We successfully found free spot for new device
	tapedev_devices[i] = tape_dev;
	tape_dev->idx = i;
	
	mutex_unlock(&tapedev_devices_lock);

	// Tapedev device was discovered by OS and pdev struct was created but we still
	// need to enable our device; enabling it means its memory regions will be 
	// enabled and fully accessible, it will be powered up if it was in low-power 
	// state.
	// We must enable our device BEFORE accessing its registers/memory
	if ((err = pci_enable_device(pdev)))
		goto out_enable;

	// DMA
	// We inform the DMA subsystem about the address size supported by the device 
	// We configure the DMA address range, which device can use for both streaming 
	// and coherent DMA operations.
	// (coherent - memory that is always visible to both the CPU and device without 
	// 	explicit cache managemen)
	// Our tapedevices support 32 bit registers
	if ((err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))))
		goto out_dma_mask;
	// For a device to perform DMA, we must first enable the device's ability to perform transactions
	pci_set_master(pdev);


	// We reserve access to the device so that no other driver conflicts with us 
	if ((err = pci_request_regions(pdev, "tapedev")))
		goto out_regions;


	// We finally can map the BAR (Base Address Registers), using BAR0 we will 
	// communicate with our device, send commands to it, read config data etc
	// --> we can access BAR by using ioread*() and iowrite*(), these functions hide 
	// 	the details if this is a MMIO or PIO address space and will just work
	// --> maxlen specifies the maximum length to map. If you want to get access to 
	// 	the complete BAR without checking for its length first, pass 0 here.
	if (!(tape_dev->bar = pci_iomap(pdev, BAR_ID, BAR_MAXLEN))) {
		err = -ENOMEM;
		goto out_bar;
	}

	/* Connect the IRQ line.  */
	if ((err = request_irq(pdev->irq, tapedev_interrupt_handler, IRQF_SHARED, "tapedev", tape_dev)))
		goto out_irq;

	// Once interrupts are enabled, to start device we need to:
	// 1) Clear all interrupts by writing to TAPEDEV_IRQ_CLEAR
	// 2) Enable at least the TAPEDEV_IRQ_INIT_DONE and TAPEDEV_IRQ_HW_ERROR 		
	// 		interrupts.
	// 3) Write 1 to TAPEDEV_ENABLE.

	// We clear all interrupts, by setting all bits to 1
	tapedev_iow(tape_dev, TAPEDEV_IRQ_CLEAR_ADDR, 0xffffffff);
	// 0xffffffff in binary consists of only ones, we are xoring here so that 
	// INIT_DONE and HW_ERROR bits are zeroed, meaning they will be enabled
	tapedev_iow(tape_dev, TAPEDEV_IRQ_MASK_ADDR, (0xffffffff ^ TAPEDEV_IRQ_INIT_DONE) ^ TAPEDEV_IRQ_HW_ERROR);

	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 1);

	// TODO: add blk dev impl

	return 0;

out_irq:
	pci_iounmap(pdev, tape_dev->bar);
out_bar:
	pci_release_regions(pdev);
out_regions:
out_dma_mask:
	pci_disable_device(pdev);
out_enable:
	mutex_lock(&tapedev_devices_lock);
	tapedev_devices[i] = NULL;
	mutex_unlock(&tapedev_devices_lock);
dealloc_tapedev:
	kfree(tape_dev);
fail:
	return err;
}


static void tapedev_remove(struct pci_dev *pdev)
{
	struct tapedev_device *tape_dev = pci_get_drvdata(pdev);
	if (tape_dev->dev) {
		device_destroy(&tapedev_class, tapedev_major + tape_dev->idx);
	}
	// blk dev free
	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 0);
	free_irq(pdev->irq, tape_dev);
	pci_iounmap(pdev, tape_dev->bar);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	mutex_lock(&tapedev_devices_lock);
	tapedev_devices[tape_dev->idx] = NULL;
	mutex_unlock(&tapedev_devices_lock);
	kfree(tape_dev);
}

static struct pci_device_id tapedev_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TAPEDEV, PCI_DEVICE_ID_TAPEDEV) },
	{ 0 }
};

static struct pci_driver tapedev_pci_driver = {
	.name = "tapedev",
	.id_table = tapedev_pci_ids,
	.probe = tapedev_probe,
	.remove = tapedev_remove,
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
	pci_unregister_driver(&tapedev_pci_driver);
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