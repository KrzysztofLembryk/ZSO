#include "tapedev.h"
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

#define MAX_DEVICES_TAPEDEV 256
#define BASE_MINOR 0
#define MINORS_COUNT 8
#define TAPEDEV_NAME "tapedev"
#define BAR_ID 0
#define BAR_MAXLEN 0

#define GET_SECTION_ADDR(s_id) ((s_id) * 0x100)
#define PHYSICAL_BLOCK_SIZE 8192
#define BASE_TAPE_SIZE (32 * 8192)
#define SIZE_OF_TAPE(s_type) ((1 << s_type) * BASE_TAPE_SIZE)
#define GET_NBR_OF_SECTORS(s_type, n_tapes) ((SIZE_OF_TAPE(s_type) / 512) * n_tapes)

// Blk dev example impl
// https://github.com/CodeImp/sblkdev/blob/master/device.c
// 
// place worth looking at in linux src is ps3disk.c, ps3vram.c
// for queue_limits exmpl: zram_drv.c, really short blk dev impl: nfblock

static dev_t tapedev_major;

// Global static variables are initialized to NULL, so now we have array of NULLs
static struct tapedev_device *tapedev_devices[MAX_DEVICES_TAPEDEV]; 

// Needed to exclusively modify tapedev_devices array
static DEFINE_MUTEX(tapedev_devices_lock);

static struct class tapedev_class = {
	.name = "tapedev",
};

// helpers pre-decl
int create_sections(struct tapedev_device *tape_dev, int num_sections);
int add_section_disks(struct tapedev_device *tape_dev, int num_sections);

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
	
	// saves the interrupt state before taking the spin lock
	spin_lock_irqsave(&dev->s_lock, flags);

	// We check if initialization of device ended by checking interrupt status value 
	// in our device and if it is TAPEDEV_IRQ_INIT_DONE 
	ir_status = tapedev_ior(dev, TAPEDEV_IRQ_STATUS_ADDR);

	uint32_t is_init_done = ir_status & (1 << TAPEDEV_IRQ_INIT_DONE);
	uint32_t is_hardware_error = ir_status & (1 << TAPEDEV_IRQ_HW_ERROR);

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

	for (uint32_t s_id = 1; s_id <= num_sections; s_id++)
	{
		uint32_t n_tapes = section_ior(dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPES_ADDR);
		// Todo: add checking if section type is within allowed range
		uint32_t sec_type = section_ior(dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPE_SIZE_ADDR);
		pr_warn("%s:%u: section type: %u, num of tapes: %u\n", __func__, __LINE__, sec_type, n_tapes);
	}


	pr_info("Init successfully done, we set this information in our tape_dev and free waiting thread\n");
	dev->init_status = 1;
	wake_up(&dev->wq_idle);
	spin_unlock_irqrestore(&dev->s_lock, flags);

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

#define TAPEDEV_IOCTL_GET_INFO             _IOR('~', 0, struct tapedev_sect_info)

static int tapedev_ioctl(struct block_device *bdev, blk_mode_t mode, unsigned cmd, unsigned long arg)
{
	// bd_disk is gendisk
	struct tapedev_device *dev = bdev->bd_disk->private_data;

	pr_info("%s:%u: contol command [0x%x] received, for device: %d\n\n", __func__, __LINE__, cmd, dev->idx);

	switch (cmd) {
	case TAPEDEV_IOCTL_GET_INFO:
		return 0;
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

// 

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
	s->n_sectors = GET_NBR_OF_SECTORS(sec_type, n_tapes);
	s->private_data = tape_dev;
	// s->data_buf = kzalloc(SIZE_OF_TAPE(s->section_type) * s->n_sectors, GFP_KERNEL);

	// TODO: currently for each section we have only one such dma buff, but probably
	// 	we will need many for one section?
	if (!(s->data_cpu = dma_alloc_coherent(&tape_dev->pdev->dev,
			PAGE_SIZE,
			&s->data_dma, GFP_KERNEL))) 
	{
		pr_err("%s:%u: Failed to dma_alloc_coherent\n", __func__, __LINE__);
		err = -ENOMEM;
		goto free_alloc_s;
	}

	// we set private driver_data to s
	err = init_tag_set(&s->tag_set, s);
	if (err) {
		pr_err("%s:%u: Failed to allocate tag set\n", __func__, __LINE__);
		goto free_dma_alloc;
	}


	// inside blk_mq_alloc_disk we set queuedata to s
	s->gdisk = blk_mq_alloc_disk(&s->tag_set, &lim, s);
	if (IS_ERR_OR_NULL(s->gdisk)) 
	{
		err = s->gdisk ? PTR_ERR(s->gdisk) : -ENOMEM;
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
	s->gdisk->fops = &tapedev_ops;
	// s or maybe tape_dev??
	s->gdisk->private_data = s;

	// disk_name shows up in /proc/partitions and sysfs and /dev/
	snprintf(s->gdisk->disk_name, 15, "tapedev%us%u", device_id, section_id - 1);

	// We should set capacity of the disk by using set_capacity, to this function 
	// we pass HOW MANY 512 byte SECTORS our disk has; 8192 / 512 = 16, so by using 
	// TAPEDEV_SECT_TAPE_SIZE we can calculate how many sectors our tape has
	// So this probably mean that each section in our tapedev needs to be a separate 
	// gendisk, minors in these gendisk will be our tapes i.e. we get 50 tapes, we 
	// need 50 minors.
	// Each tape has SIZE_OF_TAPE so every tape will have SIZE_OF_TAPE / 512 sectors
	set_capacity(s->gdisk, GET_NBR_OF_SECTORS(sec_type, n_tapes));


	*new_section = s;

	// We dont add_disk here yet, we will do that once all of them are ready

	return 0;

free_tag_set:
	blk_mq_free_tag_set(&s->tag_set);
free_dma_alloc:
	dma_free_coherent(&tape_dev->pdev->dev, PAGE_SIZE, s->data_cpu, s->data_dma);
free_alloc_s:
	kfree(s);
fail:
	return err;
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
	tape_dev->init_status = 0;

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
	// --> maxlen specifies the maximum length to map. If you want to get access to 
	// 	the complete BAR without checking for its length first, pass 0 here.
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
	while (!tape_dev->init_status) 
	{
		spin_unlock_irqrestore(&tape_dev->s_lock, flags);
		if (wait_event_interruptible(tape_dev->wq_idle, tape_dev->init_status))
			return -ERESTARTSYS;
		spin_lock_irqsave(&tape_dev->s_lock, flags);
	}

	if (tape_dev->init_status < 0)
	{
		pr_err("Device init Failed\n");
		spin_unlock_irqrestore(&tape_dev->s_lock, flags);
		// TODO: add goto that frees resources
		return -1;
	}

	spin_unlock_irqrestore(&tape_dev->s_lock, flags);

	// After enabling device we need to read num_sections data from it
	uint32_t num_sections = tapedev_ior(tape_dev, TAPEDEV_SECTIONS_ADDR);

	if (num_sections <= 0 || num_sections > 8)
	{
		pr_warn("%s:%u: provided num_sections (%d) is not in [1,8] interval\n", __func__, __LINE__, num_sections);
		err = -EINVAL;
		goto out_bad_num_sec;
	}

	tape_dev->n_sections = num_sections;
	// We count sections from id = 1, so we will have space for section at 0 idx but 
	// it will never be allocated but, I chose this instead of remembering to always 
	// subtract 1 from section_id
	tape_dev->sections = kzalloc(sizeof(struct section*) * (num_sections + 1), GFP_KERNEL);

	if (IS_ERR_OR_NULL(tape_dev->sections))
	{
		err = -ENOMEM;
		pr_warn("%s:%u: sections = kzalloc failed, err: %d\n", __func__, __LINE__, err);
		goto sections_alloc_fail;
	}

	// err = create_parent_dev(tape_dev, num_sections, tape_dev->idx);

	// if (err < 0)
	// {
	// 	pr_warn("%s:%u: create_parent_dev failed, err: %d\n", __func__, __LINE__, err);
	// 	goto free_sections;
	// }


	err = create_sections(tape_dev, num_sections);
	if (err) goto free_parent_dev;

	err = add_section_disks(tape_dev, num_sections);
	if (err) goto free_parent_dev;

	// TODO: add blk dev impl

	return 0;
free_parent_dev:
	// put_disk(tape_dev->parent_gdisk);
// free_sections:
	kfree(tape_dev->sections);
sections_alloc_fail:
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



int create_sections(struct tapedev_device *tape_dev, int num_sections)
{
	int err;
	int first_minor = 0;
	uint32_t s_id;

	// Create all sections for this device
	for (s_id = 1; s_id <= num_sections; s_id++)
	{
		uint32_t n_tapes = section_ior(tape_dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPES_ADDR);
		// Todo: add checking if section type is within allowed range
		uint32_t sec_type = section_ior(tape_dev, GET_SECTION_ADDR(s_id),TAPEDEV_SECT_TAPE_SIZE_ADDR);

		pr_info("create_sections :: s_id: %u, n_tapes: %u, sec_type: %u\n", s_id, n_tapes, sec_type);

		err = create_section(&(tape_dev->sections[s_id]), s_id, sec_type, n_tapes, tape_dev->idx, &first_minor, tape_dev);

		if (err < 0)
		{
			pr_err("%s:%u: failed to create_section for id: %d, err: %d\n", __func__, __LINE__, s_id, err);
			for (int i = 1; i <= s_id; i++)
			{
				// put_disk decrements gendisk refcount, if it reaches 0 gendisk is 
				// freed, since we've just allocated gendisk struct there might at 
				// most 1 ref to it thus put_disk will free dev.gendisk
				// We didnt use add_disk yet so we dont need to use del_gendisk
				// del_gendisk(tape_dev->sections[i]->gdisk);
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
	for (s_id = 1; s_id <= num_sections; s_id++)
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
			
			for (int i = 1; i <= num_sections; i++)
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



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krzysztof Lembryk");
MODULE_DESCRIPTION("Driver for tapedevice");
