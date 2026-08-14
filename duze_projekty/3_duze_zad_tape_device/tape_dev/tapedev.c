#include "tapedev.h"
#include "linux/irqreturn.h"
#include "linux/wait.h"
#include "tapedev_defs.h"
#include "tapedev_sysfs.h"
#include "tapedev_iow_ior.h"

#include "asm-generic/errno-base.h"
#include "linux/blk_types.h"
#include "linux/gfp_types.h"
#include "linux/slab.h"
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mmzone.h>
#include <linux/delay.h>
#include <stdint.h>

#define MAX_DEVICES_TAPEDEV 256
#define BASE_MINOR 0
#define MINORS_COUNT 8
#define TAPEDEV_NAME "tapedev"
#define BAR_ID 0
#define BAR_MAXLEN 0

#define GET_SECTION_ADDR(s_id) ((s_id + 1) * 0x100)
#define PHYSICAL_BLOCK_SIZE 8192
#define BASE_TAPE_SIZE (32 * 8192)
#define SIZE_OF_TAPE(s_type) ((1 << s_type) * BASE_TAPE_SIZE)
#define GET_NBR_OF_SECTORS(s_type, n_tapes) ((SIZE_OF_TAPE(s_type) / 512) * n_tapes)
#define TAPEDEV_IRQ_SECT_X_DONE(i)  (TAPEDEV_IRQ_SECT_0_DONE  + (i))
#define TAPEDEV_IRQ_SECT_X_ERROR(i) (TAPEDEV_IRQ_SECT_0_ERROR + (i))
// Bits 0-7 are the identifier of the command, cmd should have 32 bits.
#define GET_CMD_TYPE(cmd) ((uint32_t)((cmd) & 0xffU))
// Bits 8-31 are used to pass command-specific information.
#define GET_CMD_BODY(cmd) ((uint32_t)((cmd) & 0xffffff00U))

// Blk dev example impl
// https://github.com/CodeImp/sblkdev/blob/master/device.c
// 
// place worth looking at in linux src is ps3disk.c, ps3vram.c
// for queue_limits exmpl: zram_drv.c, really short blk dev impl: nfblock

static dev_t tapedev_major;

// We probably should make a list of queues to store commands for sections
// Global static variables are initialized to NULL, so now we have an array of NULLs
static struct tapedev_device *tapedev_devices[MAX_DEVICES_TAPEDEV]; 

// Needed to exclusively modify tapedev_devices array
static DEFINE_MUTEX(tapedev_devices_lock);

static struct class tapedev_class = {
	.name = "tapedev",
};

// helpers pre-decl
int create_sections(struct tapedev_device *tape_dev, int num_sections);
int add_section_disks(struct tapedev_device *tape_dev, int num_sections);
int handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec);

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
	uint32_t ir_status;
	uint32_t num_sections;
	uint32_t dev_status;

	// TOdo:
	// We must add here an if else statement checking if tapedev already initialized
	// if intiialized we skip checking init_done flag
	// And only check HW_ERROR, TAPEDEV_IRQ_SECT_X_DONE and TAPEDEV_IRQ_SECT_X_ERROR
	// We should do this in a loop and add macros (that make substitution for x with 
	// 0, 1, ... in TAPEDEV_IRQ_SECT_X_DONE) to this loop so that all done 
	// sections are handled in this interrupt, and after all sections are done, and 
	// we save its data or sth we take next command from some queue and make our 
	// tapedev do another command

	
	// saves the interrupt state before taking the spin lock
	spin_lock_irqsave(&dev->s_lock, flags);

	ir_status = tapedev_ior(dev, TAPEDEV_IRQ_STATUS_ADDR);

	// Below if only happens ONCE, when we use probe function for given device. 
	// TODO: move init handling to the separate function
	if (!dev->init_done)
	{
		uint32_t is_init_done = ir_status & (1 << TAPEDEV_IRQ_INIT_DONE);

		if (is_init_done)
		{
			tapedev_iow(dev, TAPEDEV_IRQ_CLEAR_ADDR, (1 << TAPEDEV_IRQ_INIT_DONE));
			pr_info("%s:%u: init done\n", __func__, __LINE__);
			dev->init_done = 1;

			// Some debug printing for now
			dev_status = tapedev_ior(dev, TAPEDEV_STATUS_ADDR);
			num_sections = tapedev_ior(dev, TAPEDEV_SECTIONS_ADDR);

			pr_info("%s:%u: device status: %u\n", __func__, __LINE__, dev_status);
			pr_info("%s:%u: nums sections: %u\n", __func__, __LINE__, num_sections);

			for (uint32_t s_id = 0; s_id < num_sections; s_id++)
			{
				uint32_t n_tapes = section_ior(dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPES_ADDR);
				// Todo: add checking if section type is within allowed range
				uint32_t sec_type = section_ior(dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPE_SIZE_ADDR);
				pr_info("%s:%u: section type: %u, num of tapes: %u\n", __func__, __LINE__, sec_type, n_tapes);
			}

			// TODO: maybe we should check if any HW error happened? But if init was
			// successful, maybe we don't have to do it.
			wake_up(&dev->wq_idle);
			spin_unlock_irqrestore(&dev->s_lock, flags);
			return IRQ_HANDLED;
		}
		else
		{
			// We got interrupt for the device but device is not initialized yet, 
			// sth went wrong, creating this device should fail
			dev->init_done = -1;
			pr_err("Init failed, we got other interrupt before init interrupt\n");
			wake_up(&dev->wq_idle);
			spin_unlock_irqrestore(&dev->s_lock, flags);
			return IRQ_NONE;
		}
	}

	// #############################################################################
	// 	We know for sure our device is INITIALIZED, so we can check other flags
	// #############################################################################

	uint32_t is_hw_error = ir_status & (1 << TAPEDEV_IRQ_HW_ERROR);
	// We always need to clear interrupt flag for given interrupt, so that this 
	// interrupt is not fired endlessly
	if (is_hw_error)
	{
		tapedev_iow(dev, TAPEDEV_IRQ_CLEAR_ADDR, (1 << TAPEDEV_IRQ_HW_ERROR));
		pr_warn("%s:%u: hardware error\n", __func__, __LINE__);
		dev->status = -1;

		spin_unlock_irqrestore(&dev->s_lock, flags);
		return IRQ_NONE;
	}

	num_sections = tapedev_ior(dev, TAPEDEV_SECTIONS_ADDR);
	spin_unlock_irqrestore(&dev->s_lock, flags);
	// #############################################################################
	// 	We know there was NO HARDWARE ERROR in device
	// 	We can check all TAPEDEV_IRQ_SECT_X_DONE and TAPEDEV_IRQ_SECT_X_ERROR 
	// #############################################################################

	// Max allowed number of sections is 8, we should check this
	// We have this stored inside section

	uint32_t section_done = 0; 
	uint32_t section_error = 0;
	uint32_t section_status = 0;
	// We need ejection queue, and once command for given section is done we take 
	// spinlock, check if anyone wants to eject tape, if so we send command to eject 
	// it, set variable in struct, wake up ejector, release spinlock, and end 
	// handling this very section (once eject command is done it will raise an 
	// interrupt and we will come back here)  

	// In below loop we check if any section is DONE if so we can start next command.
	// If given section is IDLE we firstly check if there are any new commands, if 
	// there are we start new command, if not, we do nothing.

	// We need a queue of 32-bit commands for every section
	for (int sec_id = 0; sec_id < num_sections; sec_id++)
	{
		section_done = tapedev_ior(dev, TAPEDEV_IRQ_SECT_X_DONE(sec_id));
		section_error = tapedev_ior(dev, TAPEDEV_IRQ_SECT_X_ERROR(sec_id));
		section_status = section_ior(dev, GET_SECTION_ADDR(sec_id),TAPEDEV_SECT_STATUS_ADDR);

		pr_info("%s:%u: section: %d, done: %d, error: %d, STATUS: %u\n", __func__, __LINE__, sec_id, section_done, section_error, section_status);

		int err = handle_section_interrupt(
				section_done, 
				section_error, 
				section_status, 
				dev->sections[sec_id]
		);

		if (err)
		{
			pr_err("Section handler error: %d\n", err);
		}
		// if section currently working we do nothing
	}


	return IRQ_RETVAL(ir_status);
}

// good link with blk dev: https://olegkutkov.me/2020/02/10/linux-block-device-driver/

static int tapedev_disk_open(struct gendisk *disk, fmode_t mode)
{
    //...
	pr_info("tapedev_disk_open\n");

    return 0;
}

static void tapedev_disk_release(struct gendisk *gd)
{
    //...
	pr_info("tapedev_disk_release\n");
    return;
}


struct tapedev_sect_info {
    uint32_t tapes;
    uint32_t tape_type;
    uint32_t current_tape;
};

// #define cmd_name 	_IOX (type, nr, dataitem)
#define TAPEDEV_IOCTL_GET_INFO             _IOR('~', 0, struct tapedev_sect_info)
#define TAPEDEV_IOCTL_EJECT_TAPE           _IO('~', 1)

// drivers/block/swim.c has floppy_ioctl impl for blk_dev
// We assume that under variable: arg, user supplied ptr to buffer to which he wants
// data to be written
static int tapedev_ioctl(struct block_device *bdev, blk_mode_t mode, unsigned cmd, unsigned long arg)
{
	// bd_disk is gendisk
	struct section *sec = bdev->bd_disk->private_data;
	struct tapedev_device *dev = sec->private_data;

	pr_info("%s:%u: ioctl command [0x%x] received, for device: %d, for section: %u\n\n", __func__, __LINE__, cmd, dev->idx, sec->idx);

	unsigned long flags;

	switch (cmd) {
	case TAPEDEV_IOCTL_GET_INFO: {

		spin_lock_irqsave(&sec->lock, flags);

		struct tapedev_sect_info sec_info = {
			.tapes = sec->n_tapes,
			.tape_type = sec->section_type,
			.current_tape = sec->current_tape
		};

		spin_unlock_irqrestore(&sec->lock, flags);

		pr_info("%s:%u: got TAPEDEV_IOCTL_GET_INFO ioctl cmd. sec_info = {\ntapes = %u,\ntape_type = %u,\ncurrent_tape = %u\n} \n", __func__, __LINE__, sec_info.tapes, sec_info.tape_type, sec_info.current_tape);

		if (copy_to_user((void __user *) arg, (void *) &sec_info, sizeof(struct tapedev_sect_info)))
			return -EFAULT;
		return 0;
	}
	case TAPEDEV_IOCTL_EJECT_TAPE:
	{
		spin_lock_irqsave(&sec->lock, flags);

		uint32_t current_tape = sec->current_tape;

		pr_info("%s:%u: got EJECT_TAPE ioctl cmd. Current tape: %u  \n", __func__, __LINE__, current_tape);

		// No tape, no need to eject
		if (current_tape == 0)
		{
			spin_unlock_irqrestore(&sec->lock, flags);
			return 0;
		}

		// There is some tape inserted, so we set information to eject and wait on 
		// queue
		sec->ejection_cmds += 1;

		while (sec->current_tape) 
		{
			spin_unlock_irqrestore(&sec->lock, flags);
			if (wait_event_interruptible(sec->ioctl_eject_wait_q, !sec->current_tape))
				return -ERESTARTSYS;
			spin_lock_irqsave(&sec->lock, flags);
		}

		// Once current tape is 0, we return success
		return 0;
	}
	default:
		return -ENOTTY;
	}
}

static const struct block_device_operations tapedev_ops = {
	.owner	= THIS_MODULE,
	.open = tapedev_disk_open,
	.release = tapedev_disk_release,
	.ioctl = tapedev_ioctl
};

// Multi queue and request processing functions

static inline int process_request(struct request *rq, unsigned int *nr_bytes)
{
	/*
		struct request has the following interesting us fields:
			- blk_opf_t cmd_flags -- type of request described by the struct 
				request, we allow only REQ_OP_READ and REQ_OP_WRITE;
			- struct request_queue *q -- from it we can get queuedata (our section*),
				also it stores next requests
			- struct bio *bio -- field indicating the first bio structure included 
				in the request 
			- struct request *rq_next -- points to the next struct request structure 
				in the request queue
	
		What is struct bio_vec - a contiguous range of physical memory addresses
		@bv_page:   First page associated with the address range.
		@bv_len:    Number of bytes in the address range.
		@bv_offset: Start of the address range relative to the start of @bv_page.
				in bv_page might be data we dont want, thus we start reading from offset to read only specific data we want from this page
				
				page 
			-------------
			|			|
			| some data	|
			|			|
			-------------  --
			|  offset	|   |
			|			|   |
			| our data	|	| bv_len
			|			|	|
			|			|	|
			-------------  --
			|			|
			| some data |
			|			|
			-------------
	
		All pages within a bio_vec starting from @bv_page are contiguous and
		can simply be iterated.

		Basically one request stores a list of bio, each bio stores a list of 
		bio_vecs, and each bio_vec knows which part of given page we want to 
		read/write
	*/
	int ret = 0;
	struct bio_vec bvec;
	struct req_iterator iter;
	struct section *s = rq->q->queuedata;
	// We should probably use here TAPEDEV_SECT_PTR_SHIFT instead of SECTOR_SHIFT????
	loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
	loff_t dev_size = (s->n_sectors << SECTOR_SHIFT);


	rq_for_each_segment(bvec, rq, iter) {
		// bvec is populated with current memory segment we want to read/write
		// so from bv_page number we get page_addres, and jump to offset we want to
		// read from this page. We read bv_len bytes 
		void *buf = page_address(bvec.bv_page) + bvec.bv_offset;
		unsigned long len = bvec.bv_len;

		pr_warn("%s:%u: %u sectors from %llu\n",
			__func__, __LINE__, bio_sectors(iter.bio),
			iter.bio->bi_iter.bi_sector);

		if ((pos + len) > dev_size)
			len = (unsigned long)(dev_size - pos);

		switch (req_op(rq)) {
		case REQ_OP_READ:
			pr_info("process_request READ");
			memcpy(buf, s->data_cpu + pos, len); /* READ */
			return 0;
		case REQ_OP_WRITE:
			memcpy(s->data_cpu + pos, buf, len); /* WRITE */
			pr_info("process_request WRITE");
			return 0;
		default:
			blk_dump_rq_flags(rq, TAPEDEV_NAME " bad request");
			return BLK_STS_IOERR;
		}

		pos += len;
		*nr_bytes += len;
	}

	return ret;
}

static blk_status_t _queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
	// struct request_queue *q = hctx->queue;
	// struct section *s = q->queuedata;
	// struct tapedev_device *dev = s->private_data;

	unsigned int nr_bytes = 0;
	blk_status_t status = BLK_STS_OK;
	struct request *rq = bd->rq;

	//might_sleep();
	cant_sleep(); /* cannot use any locks that make the thread sleep */

	blk_mq_start_request(rq);

	if (process_request(rq, &nr_bytes))
		status = BLK_STS_IOERR;

	pr_debug("request %llu:%d processed\n", blk_rq_pos(rq), nr_bytes);

	blk_mq_end_request(rq, status);

	return status;
}

static struct blk_mq_ops mq_ops = {
	.queue_rq = _queue_rq,
};

static inline int init_tag_set(struct blk_mq_tag_set *set, void *data)
{
	set->ops = &mq_ops;
	set->nr_hw_queues = 1;
	set->nr_maps = 1;
	set->queue_depth = 128;
	set->numa_node = NUMA_NO_NODE;
	// set->flags = no flags I think are needed;

	set->cmd_size = 0;
	set->driver_data = data;

	return blk_mq_alloc_tag_set(set);
}


// static int create_parent_dev(
// 	struct tapedev_device *tape_dev,
// 	uint32_t n_sections,
// 	int device_id
// )
// {
// 	// Currently dont know whats the point of creating /dev/tapedevX since all 
// 	// operations will be done at SECTION devices, and this device will just exist
// 	int err;
// 	uint32_t total_nbr_of_sectors = 0;
// 	struct gendisk *gdisk = blk_alloc_disk(NULL, NUMA_NO_NODE);

// 	if (IS_ERR_OR_NULL(gdisk)) 
// 	{
// 		err = gdisk ? PTR_ERR(gdisk) : -ENOMEM;
// 		pr_err("%s:%u :: blk_alloc_disk failed with error: %d\n", __func__, __LINE__,err);
// 		return err;
// 	}

// 	gdisk->fops = &tapedev_ops;
// 	gdisk->private_data = tape_dev;

// 	snprintf(gdisk->disk_name, 15, "tapedev%d", tape_dev->idx);

// 	for (u32 s_id = 1; s_id <= n_sections; s_id++) 
// 	{
//         u32 n_tapes = section_ior(tape_dev, GET_SECTION_ADDR(s_id), TAPEDEV_SECT_TAPES_ADDR);
//         u32 sec_type = section_ior(tape_dev, GET_SECTION_ADDR(s_id), TAPEDEV_SECT_TAPE_SIZE_ADDR);

//         total_nbr_of_sectors += (SIZE_OF_TAPE(sec_type) / 512) * n_tapes;
//     }

//     set_capacity(gdisk, total_nbr_of_sectors);

//     // err = device_add_disk(&tape_dev->pdev->dev, gdisk, NULL);
//     err = add_disk(gdisk);
//     if (err < 0) {
//         put_disk(gdisk);
//         return err;
//     }

//     tape_dev->parent_gdisk = gdisk;
//     return 0;
// }


/* PCI driver.  */

static int tapedev_probe(
	struct pci_dev *pdev,
	const struct pci_device_id *pci_id
)
{
	// When OS discovers that tape device is connect to PCI BUS (and it is since we
	// have modified QEMU installed) it invokes this function and passes pdev struct 
	// which holds all info about the device

	int err;

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
	tape_dev->init_done = 0;
	tape_dev->status = 0;

	// init locks mutexes etc
	spin_lock_init(&tape_dev->s_lock);
	init_waitqueue_head(&tape_dev->wq_free);
	init_waitqueue_head(&tape_dev->wq_idle);

	// lock needed here since we may have many tapedevs added simultaneously
	// We allow many tapedev devices, but every such device we need to store 
	// somewhere, so now we find first free index for our newly created device
	mutex_lock(&tapedev_devices_lock);
	uint32_t i;
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
		goto out_pci_enable;

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
	// --> maxlen specifies the maximum length to map. If we want to get access to 
	// 	the complete BAR without checking for its length first, we pass 0 here.
	if (!(tape_dev->bar = pci_iomap(pdev, BAR_ID, BAR_MAXLEN))) {
		err = -ENOMEM;
		goto out_bar;
	}

	/* 
		Connect the IRQ line. We register our interrupt handler for pdev->irq line, 
		so whenever this interrupt fires tapedev_interrupt_handler is ran
	*/
	if ((err = request_irq(pdev->irq, tapedev_interrupt_handler, IRQF_SHARED, "tapedev", tape_dev)))
		goto out_irq;

	// Once interrupts are enabled, to start device we need to:
	// 1) Clear all interrupts by writing ones to TAPEDEV_IRQ_CLEAR
	// 2) Enable at least the TAPEDEV_IRQ_INIT_DONE and TAPEDEV_IRQ_HW_ERROR 		
	// 		interrupts.
	// 3) Write 1 to TAPEDEV_ENABLE.

	// We clear all interrupts, by setting all bits to 1
	tapedev_iow(tape_dev, TAPEDEV_IRQ_CLEAR_ADDR, 0xffffffff);
	// 0xffffffff in binary consists of only ones, we are xoring here so that 
	// INIT_DONE and HW_ERROR bits are zeroed, meaning these intrpts will be enabled
	// others are disabled
	tapedev_iow(
		tape_dev, 
		TAPEDEV_IRQ_MASK_ADDR, 
		(0xffffffff ^ (1 << TAPEDEV_IRQ_INIT_DONE)) ^ (1 << TAPEDEV_IRQ_HW_ERROR)
	);

	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 1);

	// After enabling device we NEED TO WAIT for it to SUCCESSFULLY INIT, before 
	// doing anything
	unsigned long flags;
	spin_lock_irqsave(&tape_dev->s_lock, flags);

	// TODO: we shouldn't return here, it skips cleanup path, we should use goto
	long wait_ret;
	while (!tape_dev->init_done) 
	{
		spin_unlock_irqrestore(&tape_dev->s_lock, flags);
		wait_ret = wait_event_interruptible(tape_dev->wq_idle, tape_dev->init_done);

		if (wait_ret < 0)
		{
			err = -EINTR;
			goto init_fail;
		}

		spin_lock_irqsave(&tape_dev->s_lock, flags);
	}

	if (tape_dev->init_done < 0)
	{
		pr_err("Device: %u init Failed\n", tape_dev->idx);
		spin_unlock_irqrestore(&tape_dev->s_lock, flags);
		err = -ENODEV;
		goto init_fail;
	}

	if (tape_dev->status < 0)
	{
		pr_err("Device %u encountered hw error: %d\n", tape_dev->idx, tape_dev->status);
		spin_unlock_irqrestore(&tape_dev->s_lock, flags);
		err = -EIO;
		goto init_fail;
	}

	spin_unlock_irqrestore(&tape_dev->s_lock, flags);

	// After enabling device we need to read num_sections data from it
	uint32_t num_sections = tapedev_ior(tape_dev, TAPEDEV_SECTIONS_ADDR);

	if (num_sections <= 0 || num_sections > 8)
	{
		pr_err("%s:%u: provided num_sections (%d) is not in [1,8] interval\n", __func__, __LINE__, num_sections);
		err = -EINVAL;
		goto out_bad_num_sec;
	}

	tape_dev->n_sections = num_sections;
	// Section ids are counted from 0, however to use Section Registers we count them
	// from 1, thus we will count them from 0, but specific functions that will 
	// communicate with section registers will add 1 to section id
	tape_dev->sections = kzalloc(sizeof(struct section*) * num_sections, GFP_KERNEL);

	if (IS_ERR_OR_NULL(tape_dev->sections))
	{
		err = -ENOMEM;
		pr_err("%s:%u: sections = kzalloc failed, err: %d\n", __func__, __LINE__, err);
		goto sections_alloc_fail;
	}

	// err = create_parent_dev(tape_dev, num_sections, tape_dev->idx);

	// if (err < 0)
	// {
	// 	pr_warn("%s:%u: create_parent_dev failed, err: %d\n", __func__, __LINE__, err);
	// 	goto free_sections;
	// }


	err = create_sections(tape_dev, num_sections);
	if (err) goto free_sections;

	err = add_section_disks(tape_dev, num_sections);
	if (err) goto free_sections;

	// now we can enable section interrupts
	// TODO: Or maybe we can check how many sections dev has before creating sections
	// ?
	uint32_t mask = (0xffffffff ^ (1 << TAPEDEV_IRQ_INIT_DONE)) ^ (1 << TAPEDEV_IRQ_HW_ERROR);

	// Now we enable interrupts for this device's sections
	for (int sec_id = 0; i < num_sections; i++)
	{
		mask = mask ^ (1 << TAPEDEV_IRQ_SECT_X_DONE(sec_id));
		mask = mask ^ (1 << TAPEDEV_IRQ_SECT_X_ERROR(sec_id));
	}
	tapedev_iow(
		tape_dev, 
		TAPEDEV_IRQ_MASK_ADDR, 
		mask	
	);

	// TODO: add blk dev impl

	return 0;
	// put_disk(tape_dev->parent_gdisk);
free_sections:
	kfree(tape_dev->sections);
sections_alloc_fail:
init_fail:
out_bad_num_sec:
	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 0);
out_irq:
	pci_iounmap(pdev, tape_dev->bar);
out_bar:
	pci_release_regions(pdev);
out_regions:
out_dma_mask:
	pci_disable_device(pdev);
out_pci_enable:
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

	pr_info("%s:%u: removing tape device: '%d'\n", __func__, __LINE__, tape_dev->idx);

	tapedev_iow(tape_dev, TAPEDEV_ENABLE_ADDR, 0);
	free_irq(pdev->irq, tape_dev);

	for (int s_id = 1; s_id <= tape_dev->n_sections; s_id++)
	{
		sysfs_remove_group(&disk_to_dev(tape_dev->sections[s_id]->gdisk)->kobj, &tape_attr_group);
		del_gendisk(tape_dev->sections[s_id]->gdisk);
		put_disk(tape_dev->sections[s_id]->gdisk);
		kfree(tape_dev->sections[s_id]);
	}
	kfree(tape_dev->sections);
	// blk dev free
	pci_iounmap(pdev, tape_dev->bar);

	// (docs) Drivers should call pci_release_region() AFTER calling 
	// pci_disable_device(). The idea is to prevent two devices colliding on the 
	// same address range
	pci_disable_device(pdev);
	pci_release_regions(pdev);

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

	pr_info("%s:%u: registered block device major %d\n", __func__, __LINE__, tapedev_major);

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
	pr_info("%s:%u: cleaning up tape device\n", __func__, __LINE__);
	pci_unregister_driver(&tapedev_pci_driver);
	class_unregister(&tapedev_class);
	unregister_blkdev(tapedev_major, TAPEDEV_NAME);
}
module_init(init_tapedev);
module_exit(cleanup_tapedev);


// Each tape type is a separate section with gdisk which is our block device, 
// tape_types":[0,1,2,3,4],"tapes":[50,40,30,20,10]}
// Each minor of this gdisk is a SECTION, each section will have file /dev/tapedevXsN
static int create_section(
	struct section **new_section,
	uint32_t section_id,
	uint32_t sec_type, 
	uint32_t n_tapes,
	uint32_t device_id,
	int *first_minor,
	struct tapedev_device *tape_dev
)
{
	struct queue_limits lim = {
		.logical_block_size		= SECTOR_SIZE, 
		/*
			* To ensure that we always get PAGE_SIZE aligned and
			* n*PAGE_SIZED sized I/O requests.
			*/
		.physical_block_size		= PHYSICAL_BLOCK_SIZE,
		// .io_min				= PAGE_SIZE,
		// .io_opt				= PAGE_SIZE,
	};

    int err;
	struct section *sec = kzalloc(sizeof(struct section), GFP_KERNEL);

	if (!sec)
	{
		pr_err("%s:%u: kzalloc failed\n", __func__, __LINE__);
		err = -ENOMEM;
		goto fail;
	}

	spin_lock_init(&sec->lock);

	sec->idx = section_id;
	sec->n_tapes = n_tapes;
	sec->section_type = sec_type;
	sec->n_sectors = GET_NBR_OF_SECTORS(sec_type, n_tapes);
	sec->current_tape = 0;
	sec->ejection_cmds = 0;

	init_waitqueue_head(&sec->ioctl_eject_wait_q);
	init_waitqueue_head(&sec->cmd_wait_q);

	sec->curr_cmd = TAPEDEV_CMD_NONE;
	sec->next_cmd = TAPEDEV_CMD_NONE;
	sec->private_data = tape_dev;
	sec->status = 0;
	// s->data_buf = kzalloc(SIZE_OF_TAPE(s->section_type) * s->n_sectors, GFP_KERNEL);

	// TODO: currently for each section we have only one such dma buff, but probably
	// we will need many for one section? Nah probably not, section is one continous
	// chunk of memory
	if (!(sec->data_cpu = dma_alloc_coherent(&tape_dev->pdev->dev,
			PAGE_SIZE,
			&sec->data_dma, GFP_KERNEL))) 
	{
		pr_err("%s:%u: Failed to dma_alloc_coherent\n", __func__, __LINE__);
		err = -ENOMEM;
		goto free_alloc_s;
	}

	// we set private driver_data to s
	err = init_tag_set(&sec->tag_set, sec);
	if (err) {
		pr_err("%s:%u: Failed to allocate tag set\n", __func__, __LINE__);
		goto free_dma_alloc;
	}

	// inside blk_mq_alloc_disk we set queuedata (which is private data) to s
	sec->gdisk = blk_mq_alloc_disk(&sec->tag_set, &lim, sec);
	if (IS_ERR_OR_NULL(sec->gdisk)) 
	{
		err = sec->gdisk ? PTR_ERR(sec->gdisk) : -ENOMEM;
		pr_err("%s :: blk_mq_alloc_disk failed with error: %d\n", __func__, err);
		goto free_tag_set;
	}

	// s->gdisk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	// if (IS_ERR_OR_NULL(s->gdisk)) 
	// {
	// 	err = s->gdisk ? PTR_ERR(s->gdisk) : -ENOMEM;
	// 	pr_err("%s :: blk_alloc_disk failed with error: %d\n", __func__, err);
	// 	goto free_alloc_s;
	// }

	// Should not be done by us as gendisk struct says
	// s->gdisk->major = tapedev_major;
	// s->gdisk->first_minor = *first_minor;
	// *first_minor += n_tapes;
	// s->gdisk->minors = n_tapes;
	sec->gdisk->fops = &tapedev_ops;
	sec->gdisk->private_data = sec;

	// disk_name shows up in /proc/partitions and sysfs and /dev/
	snprintf(sec->gdisk->disk_name, 15, "tapedev%us%u", device_id, section_id);

	// We should set capacity of the disk by using set_capacity, to this function 
	// we pass HOW MANY 512 byte SECTORS our disk has; 8192 / 512 = 16, so by using 
	// TAPEDEV_SECT_TAPE_SIZE we can calculate how many sectors our tape has
	// So this probably mean that each section in our tapedev needs to be a separate 
	// gendisk, minors in these gendisk will be our tapes i.e. we get 50 tapes, we 
	// need 50 minors.
	// Each tape has SIZE_OF_TAPE so every tape will have SIZE_OF_TAPE / 512 sectors
	set_capacity(sec->gdisk, GET_NBR_OF_SECTORS(sec_type, n_tapes));


	*new_section = sec;

	// We dont add_disk here yet, we will do that once all of them are ready

	return 0;

free_tag_set:
	blk_mq_free_tag_set(&sec->tag_set);
free_dma_alloc:
	dma_free_coherent(&tape_dev->pdev->dev, PAGE_SIZE, sec->data_cpu, sec->data_dma);
free_alloc_s:
	kfree(sec);
fail:
	return err;
}

int create_sections(struct tapedev_device *tape_dev, int num_sections)
{
	int err;
	int first_minor = 0;
	uint32_t s_id;

	// Create all sections for this device
	for (s_id = 0; s_id < num_sections; s_id++)
	{
		uint32_t n_tapes = section_ior(tape_dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPES_ADDR);
		// Todo: add checking if section type is within allowed range
		uint32_t sec_type = section_ior(tape_dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPE_SIZE_ADDR);

		pr_info("create_sections :: s_id: %u, n_tapes: %u, sec_type: %u\n", s_id, n_tapes, sec_type);

		err = create_section(&(tape_dev->sections[s_id]), s_id, sec_type, n_tapes, tape_dev->idx, &first_minor, tape_dev);

		if (err < 0)
		{
			pr_err("%s:%u: failed to create_section for id: %d, err: %d\n", __func__, __LINE__, s_id, err);

			// We must deallocate all previous ones
			// TODO: add helper function for this
			for (int i = 0; i < s_id; i++)
			{
				// put_disk decrements gendisk refcount, if it reaches 0 gendisk is 
				// freed, since we've just allocated gendisk struct there might at 
				// most 1 ref to it thus put_disk will free dev.gendisk
				// We didnt use add_disk yet so we dont need to use del_gendisk
				// del_gendisk(tape_dev->sections[i]->gdisk);

				blk_mq_free_tag_set(&tape_dev->sections[i]->tag_set);
				dma_free_coherent(&tape_dev->pdev->dev, PAGE_SIZE, tape_dev->sections[i]->data_cpu, tape_dev->sections[i]->data_dma);
				put_disk(tape_dev->sections[i]->gdisk);
				kfree(tape_dev->sections[i]);
			}
			return err;
		}
	}
	return 0;
}

int add_section_disks(struct tapedev_device *tape_dev, int num_sections)
{
	int err, err_add, err_sysfs;
	uint32_t s_id;
	// Adding all section disks
	for (s_id = 0; s_id < num_sections; s_id++)
	{
		// err = device_add_disk(&tape_dev->pdev->dev, tape_dev->sections[s_id]->gdisk, NULL);
		// pr_info("%s:%u: adding disk for section: %d \n", __func__, __LINE__, s_id);
		err_add = add_disk(tape_dev->sections[s_id]->gdisk);

		err_sysfs = sysfs_create_group(
			&disk_to_dev(tape_dev->sections[s_id]->gdisk)->kobj, 
			&tape_attr_group
		);

		if (err_add < 0 || err_sysfs < 0)
		{
			if (err_add < 0)
			{
				pr_err("%s:%u: add_disk failed for device: %d, s_id: %d, err: '%d'\n", __func__, __LINE__, tape_dev->idx, s_id, err_add);
				err = err_add;
			}
			else if (err_sysfs < 0)
			{
				pr_err("%s:%u: sysfs_create_group failed for device: %d, s_id: %d, err: '%d'\n", __func__, __LINE__, tape_dev->idx, s_id, err_add);
				err = err_sysfs;
			}
			
			for (int i = 0; i < num_sections; i++)
			{
				struct section *s = tape_dev->sections[i];
				// where add_disk was successful we need to call del_gendisk
				if (i < s_id)
					del_gendisk(tape_dev->sections[i]->gdisk);
				// for the rest put_disk and kfree is enough

				blk_mq_free_tag_set(&s->tag_set);
				dma_free_coherent(&tape_dev->pdev->dev, PAGE_SIZE, s->data_cpu, s->data_dma);
				put_disk(s->gdisk);
				kfree(s);
			}
			return err;	
		}
	}
	return 0;
}


int handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec)
{
	if (section_error)
	{
		// We need to clear the interrupt, so that it doesn't fire endlessly
		tapedev_iow(sec->private_data, TAPEDEV_IRQ_CLEAR_ADDR, 
			(1 << TAPEDEV_IRQ_SECT_X_ERROR(sec->idx))
		);

		pr_err("%s:%u: section: %d, we got error for current command: %u\n", __func__, __LINE__, sec->idx, sec->curr_cmd);
		// TODO: IDK how we should handle these things yet
		switch (section_status)
		{
			case TAPEDEV_SECT_STATUS_ERR_INVALID_CMD:
				pr_warn("%s:%u: section: %d, error: ERR_INVALID_CMD\n", __func__, __LINE__, sec->idx);

				/* Invalid command received */

				break;

			case TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE:

				pr_warn("%s:%u: section: %d, error: ERR_TAPE_ACTIVE\n", __func__, __LINE__, sec->idx);
				/* Tape is currently active/busy */

				break;

			case TAPEDEV_SECT_STATUS_ERR_NO_TAPE:

				pr_warn("%s:%u: section: %d, error: ERR_NO_TAPE\n", __func__, __LINE__, sec->idx);
				/* No tape present */

				break;

			case TAPEDEV_SECT_STATUS_ERR_RESET:

				pr_warn("%s:%u: section: %d, error: ERR_RESET\n", __func__, __LINE__, sec->idx);
				/* Device was reset */

				break;

			case TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO:

				pr_warn("%s:%u: section: %d, error: ERR_INVALID_TAPE_NO\n", __func__, __LINE__, sec->idx);
				/* Invalid tape number specified */

				break;

			case TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS:

				pr_warn("%s:%u: section: %d, error: ERR_INVALID_FFWD_POS\n", __func__, __LINE__, sec->idx);
				/* Invalid fast-forward position */

				break;

			case TAPEDEV_SECT_STATUS_ERR_READ_PAST_END:

				pr_warn("%s:%u: section: %d, error: ERR_READ_PAST_END\n", __func__, __LINE__, sec->idx);
				/* Attempted to read past end of tape */

				break;

			case TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END:

				pr_warn("%s:%u: section: %d, error: ERR_WRITE_PAST_END\n", __func__, __LINE__, sec->idx);
				/* Attempted to write past end of tape */

				break;

			case TAPEDEV_SECT_STATUS_ERR_IO:

				pr_warn("%s:%u: section: %d, error: ERR_IO\n", __func__, __LINE__, sec->idx);
				/* General I/O error */

				break;

			case TAPEDEV_SECT_STATUS_ERR_PGTABLE:

				pr_warn("%s:%u: section: %d, error: ERR_PGTABLE\n", __func__, __LINE__, sec->idx);
				/* Page table error */

				break;

			default:

				pr_warn("%s:%u: section: %d, error: Unknown\n", __func__, __LINE__, sec->idx);
				/* Unknown status code */

				break;

		}
	}

	if (section_status == TAPEDEV_SECT_STATUS_WORKING)
		return 0;

	// Section finished a command, we need to clear interrupt flag
	// TODO: add helper function for clearing DONE and ERROR flags, we will pass 
	// there just sec and function will do the rest
	if (section_done)
		tapedev_iow(sec->private_data, TAPEDEV_IRQ_CLEAR_ADDR, 
			(1 << TAPEDEV_IRQ_SECT_X_DONE(sec->idx)));

	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

	uint32_t curr_cmd_type = GET_CMD_TYPE(sec->curr_cmd);
	// uint32_t curr_cmd_body = GET_CMD_BODY(sec->curr_cmd);
	sec->curr_cmd = TAPEDEV_CMD_NONE;

	// TODO: check below
	// IDK if section_status has value STATUS_DONE always when section_done is set 
	if (section_status == TAPEDEV_SECT_STATUS_DONE)
	{
		if (curr_cmd_type == TAPEDEV_CMD_EJECT_TAPE)
		{
			// wake up all threads that were waiting for ejection
			// if anyone waited
			sec->current_tape = 0;
			uint32_t tape = section_ior(sec->private_data, GET_SECTION_ADDR(sec->idx), TAPEDEV_SECT_TAPE_NO_ADDR);

			if (tape != 0)
			{
				pr_err("something went wrong, eject_tape was done but  %u tape is still inserted\n", tape);
			}

			if (sec->ejection_cmds)
			{
				sec->ejection_cmds = 0;
				wake_up_all(&sec->ioctl_eject_wait_q);
			}

			// It might happen that both request_handling thread and ioctl thread
			// want to eject, if it happens both current and next are EJECT cmds
			// thus we simply wake up all threads and set next_cmd to NONE
			if (GET_CMD_TYPE(sec->next_cmd) == TAPEDEV_CMD_EJECT_TAPE)
			{
				sec->next_cmd = TAPEDEV_CMD_NONE;
				wake_up(&sec->cmd_wait_q);
			}

		}

		// TODO: handling of other commands

		// After completing current command we check if anyone wants to eject tape
		// If there is no tape to eject we just wake everyone, and go to executing 
		// next command 
		// If there is a tape to eject we send ejection command and set curr_cmd as
		// eject_tape and end handling this section for now
		if (sec->ejection_cmds)
		{
			if (sec->current_tape)
			{
				// We send command to eject and return
				sec->curr_cmd = TAPEDEV_CMD_EJECT_TAPE;
				return 0;
			}
			else
			{
				// there is no tape inserted, we just wake up ioctl threads
				wake_up(&sec->cmd_wait_q);
			}

		}

		// After handling current cmd we handle next command
		// To handle next_command we just need to send it to the tapedevice and set
		// it as current_cmd

		uint32_t next_cmd_type = GET_CMD_TYPE(sec->next_cmd);
		// uint32_t next_cmd_body = GET_CMD_BODY(sec->next_cmd);

		if (next_cmd_type == TAPEDEV_CMD_NONE)
		{
			// TODO: add gotos that restores spinlock and returns correct value
			spin_unlock_irqrestore(&sec->lock, flags);
			return 0;
		}

		sec->curr_cmd = sec->next_cmd;
		sec->next_cmd = TAPEDEV_CMD_NONE;

		// TODO: send command to tapedev

		spin_unlock_irqrestore(&sec->lock, flags);
		return 0;
		// After command is done we check if anyone wanted to eject tape, and we send
		// command to eject it before other commands, and free all threads waiting 
		// for ejection
	}
	else if (section_status == TAPEDEV_SECT_STATUS_IDLE)
	{
		// Here there is no previous command's result we need to take care of.
		// We can just start a new one if there is any.
	}
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krzysztof Lembryk");
MODULE_DESCRIPTION("Driver for tapedevice");
