#include "tapedev.h"
#include "asm-generic/errno-base.h"
#include "linux/gfp_types.h"
#include "linux/slab.h"
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

#define GET_SECTION_ADDR(s_id) ((s_id) * 0x100)
#define BASE_TAPE_SIZE (32 * 8192)
#define SIZE_OF_TAPE(s_type) ((1 << s_type) * BASE_TAPE_SIZE)

static dev_t tapedev_major;
// Our workflow will be like that, in init we only allocate major nbr, create class 
// and init PCI -- adding DISKS for our blkd dev will happen inside PROBE function

struct section
{
	int idx;
	uint32_t n_tapes;
	// 0 to 4, to calc size of tape for this section use SIZE_OF_TAPE macro
	uint32_t section_type; 	
	/*
		gendisk is kernel's representation of of an individual DISK DEVICE
	*/
	struct gendisk *gdisk;
	spinlock_t lock; /* For mutual exclusion */
	struct request_queue *queue; /* The device request queue */
};

// We will make an array of tapedev_devices, each will have its own gdisk etc
struct tapedev_device {
	int idx;
	struct device *dev; // used to store device_create(...) return value
	struct pci_dev *pdev;
	void __iomem *bar;
	spinlock_t s_lock;
	// An array of sections, from 1 to 8 sections
	uint32_t n_sections;
	struct section **sections;
	// struct list_head buffers_free;
	// struct list_head buffers_running;
	wait_queue_head_t wq_free;
	wait_queue_head_t wq_idle;
}; 

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

static inline void section_iow(struct tapedev_device *dev, uint32_t offset, uint32_t reg, uint32_t val)
{
	iowrite32(val, dev->bar + offset + reg);
	// printk(KERN_ALERT "tapedev_iow :: tapedev %03x <- %08x\n", reg, val);
}

static inline uint32_t section_ior(struct tapedev_device *dev, uint32_t offset, uint32_t reg)
{
	uint32_t res = ioread32(dev->bar + offset + reg);
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
	uint32_t num_sections;
	uint32_t dev_status;
	

	// saves the interrupt state before taking the spin lock
	spin_lock_irqsave(&dev->s_lock, flags);

	// We check if initialization of device ended by checking interrupt status value 
	// in our device and if it is TAPEDEV_IRQ_INIT_DONE 
	istatus = tapedev_ior(dev, TAPEDEV_IRQ_STATUS_ADDR);

	uint32_t is_init_done = istatus & (1 << TAPEDEV_IRQ_INIT_DONE);
	uint32_t is_hardware_error = istatus & (1 << TAPEDEV_IRQ_HW_ERROR);

	dev_status = tapedev_ior(dev, TAPEDEV_STATUS_ADDR);
	num_sections = tapedev_ior(dev, TAPEDEV_SECTIONS_ADDR);

	// We always need to clear interrupt flag for given interrupt, so that this 
	// interrupt is not fired endlessly
	if (is_init_done)
	{
		tapedev_iow(dev, TAPEDEV_IRQ_CLEAR_ADDR, (1 << TAPEDEV_IRQ_INIT_DONE));
		pr_warn("%s:%u: init done\n", __func__, __LINE__);
	}
	if (is_hardware_error)
	{
		tapedev_iow(dev, TAPEDEV_IRQ_CLEAR_ADDR, (1 << TAPEDEV_IRQ_HW_ERROR));
		pr_warn("%s:%u: hardware error\n", __func__, __LINE__);
	}

	pr_warn("%s:%u: device status: %u\n", __func__, __LINE__, dev_status);
	pr_warn("%s:%u: nums sections: %u\n", __func__, __LINE__, num_sections);

	for (int s_id = 1; s_id <= num_sections; s_id++)
	{
		uint32_t n_tapes = section_ior(dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPES_ADDR);
		uint32_t sec_type = section_ior(dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPE_SIZE_ADDR);
		pr_warn("%s:%u: section type: %u, num of tapes: %u\n", __func__, __LINE__, sec_type, n_tapes);
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

// Each gdisk will be our block device, i.e. for:
// tape_types":[0,1,2,3,4],"tapes":[50,40,30,20,10]}
// We will have one gdisk, which will have file /dev/tapedevX
// Each minor of this gdisk is a SECTION, each section will have file /dev/tapedevXsN
static int create_section(
	struct section **new_section,
	int section_id,
	int sec_type, 
	int n_tapes,
	int device_id,
	int *first_minor
)
{
	// DONT KNOW IF NEEDED
	// struct queue_limits lim = {
	// 	.logical_block_size		= 512, // TODO - should be based on some tape type
	// 	/*
	// 		* To ensure that we always get PAGE_SIZE aligned and
	// 		* n*PAGE_SIZED sized I/O requests.
	// 		*/
	// 	// .physical_block_size		= PAGE_SIZE,
	// 	// .io_min				= PAGE_SIZE,
	// 	// .io_opt				= PAGE_SIZE,
	// };

    int err;
	struct section *s = kzalloc(sizeof(struct section), GFP_KERNEL);

	if (!s)
	{
		pr_err("%s:%u: kzalloc failed\n", __func__, __LINE__);
		err = -ENOMEM;
		goto fail;
	}

	spin_lock_init(&s->lock);
	s->idx = section_id;
	s->section_type = sec_type;
	s->n_tapes = n_tapes;
	s->gdisk = blk_alloc_disk(NULL, NUMA_NO_NODE);

	if (IS_ERR_OR_NULL(s->gdisk)) 
	{
		err = s->gdisk ? PTR_ERR(s->gdisk) : -ENOMEM;
		pr_err("%s :: blk_alloc_disk failed with error: %d\n", __func__, err);
		goto free_alloc_s;
	}

	s->gdisk->major = tapedev_major;
	s->gdisk->first_minor = *first_minor;
	s->gdisk->minors = n_tapes;
	s->gdisk->fops = &tapedev_ops;
	// s or maybe tape_dev??
	s->gdisk->private_data = s;

	// We should set capacity of the disk by using set_capacity, to this function 
	// we pass HOW MANY 512 byte SECTORS our disk has; 8192 / 512 = 16, so by using 
	// TAPEDEV_SECT_TAPE_SIZE we can calculate how many sectors our tape has
	// So this probably mean that each section in our tapedev needs to be a separate 
	// gendisk, minors in these gendisk will be our tapes i.e. we get 50 tapes, we 
	// need 50 minors.
	// Each tape has SIZE_OF_TAPE so every tape will have SIZE_OF_TAPE / 512 sectors
	set_capacity(s->gdisk, (SIZE_OF_TAPE(sec_type) / 512) * n_tapes);
	*first_minor += n_tapes;

	// disk_name shows up in /proc/partitions and sysfs and /dev/
	snprintf(s->gdisk->disk_name, 15, "tapedev%ds%d", device_id, section_id);

	*new_section = s;

	// We dont add_disk here yet, we will do that once all of them are ready

	return 0;

free_alloc_s:
	kfree(s);
fail:
	return err;
}

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
	init_waitqueue_head(&tape_dev->wq_free);
	init_waitqueue_head(&tape_dev->wq_idle);

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
	// INIT_DONE and HW_ERROR bits are zeroed, meaning these intrpts will be enabled
	// others are disabled
	tapedev_iow(tape_dev, TAPEDEV_IRQ_MASK_ADDR, (0xffffffff ^ TAPEDEV_IRQ_INIT_DONE) ^ TAPEDEV_IRQ_HW_ERROR);

	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 1);

	// After enabling device we need to read section data from it
	uint32_t num_sections = tapedev_ior(tape_dev, TAPEDEV_SECTIONS_ADDR);

	if (num_sections <= 0 || num_sections > 8)
	{
		pr_warn("%s:%u: provided num_sections (%d) is not in [1,8] interval\n", __func__, __LINE__, num_sections);
		err = -EINVAL;
		goto out_bad_num_sec;
	}

	tape_dev->n_sections = num_sections;
	// We count sections from id = 1
	tape_dev->sections = kzalloc(sizeof(struct section*) * (num_sections + 1), GFP_KERNEL);
	int first_minor = 0;

	// Create all sections for this device
	for (int s_id = 1; s_id <= num_sections; s_id++)
	{
		int n_tapes = section_ior(tape_dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPES_ADDR);
		int sec_type = section_ior(tape_dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPE_SIZE_ADDR);

		err = create_section(&(tape_dev->sections[s_id]), s_id, sec_type, n_tapes, tape_dev->idx, &first_minor);

		if (err < 0)
		{
			// section at 0 idx is never used
			kfree(tape_dev->sections[0]);
			for (int i = 1; i <= s_id; i++)
			{
				// put_disk decrements gendisk refcount, if it reaches 0 gendisk is 
				// freed, since we've just allocated gendisk struct there might at 
				// most 1 ref to it thus put_disk will free dev.gendisk
				del_gendisk(tape_dev->sections[i]->gdisk);
				put_disk(tape_dev->sections[i]->gdisk);
				kfree(tape_dev->sections[i]);
			}
			goto free_sections;
		}

	}
	// TODO: add blk dev impl

	return 0;
free_sections:
	kfree(tape_dev->sections);
out_bad_num_sec:
	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 0);
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
	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 0);
	free_irq(pdev->irq, tape_dev);

	kfree(tape_dev->sections[0]);
	for (int s_id = 1; s_id <= tape_dev->n_sections; s_id++)
	{
		del_gendisk(tape_dev->sections[s_id]->gdisk);
		put_disk(tape_dev->sections[s_id]->gdisk);
		kfree(tape_dev->sections[s_id]);
	}
	// blk dev free
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



static int init_tapedev(void)
{
	pr_info("init_tapedev\n");
	int err;

	// After register_blkdev, kernel will display TAPEDEV_NAME in /proc/devices  
	// We obtain major number BUT it does not make any disk drives available to the 
	// system
	// We need to register DISKS separately in probe function
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
}
module_init(init_tapedev);
module_exit(cleanup_tapedev);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krzysztof Lembryk");
MODULE_DESCRIPTION("Driver for tapedevice");