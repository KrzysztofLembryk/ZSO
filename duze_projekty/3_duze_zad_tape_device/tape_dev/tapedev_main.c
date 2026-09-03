#include "linux/dma-mapping.h"
#include "linux/list.h"
#include "linux/scatterlist.h"
#include "tapedev.h"
#include "linux/blk-mq.h"
#include "linux/irqreturn.h"
#include "linux/stddef.h"
#include "linux/wait.h"
#include "tapedev_defs.h"
#include "tapedev_sysfs.h"
#include "tapedev_iow_ior.h"
#include "tapedev_irq.h"

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


// Blk dev example impl
// https://github.com/CodeImp/sblkdev/blob/master/device.c
// 
// place worth looking at in linux src is ps3disk.c, ps3vram.c
// for queue_limits exmpl: zram_drv.c, really short blk dev impl: nfblock

static dev_t tapedev_major;

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

// TODO: maybe we should move it to tapedev_irq, or leave it here as a main interrupt
// function
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
			pr_info("%s:%u: nbr of sections: %u\n", __func__, __LINE__, num_sections);

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

	uint32_t section_done; 
	uint32_t section_error;
	uint32_t section_status;
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
		struct section *sec = dev->sections[sec_id];
		
		// section_done might have big value since we get exact bit that was set,
		// thus we just check if value is greater than 0, if yes we section is done 

		section_done = (ir_status & (1 << TAPEDEV_IRQ_SECT_X_DONE(sec_id))) > 0;
		section_error = (ir_status & (1 << TAPEDEV_IRQ_SECT_X_ERROR(sec_id))) > 0;
		section_status = section_read_from(TAPEDEV_SECT_STATUS_ADDR, sec);

		// if (sec_id == 1)
		// 	pr_warn("%s:%u: section: %u, section_done: %u, section_error: %u, section_status: %u \n", __func__, __LINE__, sec_id, section_done, section_error, section_status);

		int err = 0;
		if (section_done || section_error)
		{
			err = handle_section_interrupt(
					section_done, 
					section_error, 
					section_status, 
					sec				
			);
		}

		if (err)
		{
			pr_err("Section handler error: %d\n", err);
		}
		// if section currently working we do nothing
	}


	// return IRQ_RETVAL(ir_status);
	return IRQ_HANDLED;
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

// #define ioctl_cmd_name 	_IOX (type, nr, dataitem)
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
		struct lst_node *node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node) 
		{
			return -ENOMEM;
		}

		spin_lock_irqsave(&sec->lock, flags);

		uint32_t current_tape = sec->current_tape;

		pr_info("%s:%u: got EJECT_TAPE ioctl cmd. Current tape: %u  (if 0 no tape inserted)\n", __func__, __LINE__, current_tape);


		node->cmd = (struct section_cmd) {
			.cmd = TAPEDEV_CMD_EJECT_TAPE,
			.is_ioctl = true
		};
		sec->ejection_cmds += 1;
		// We must send command to the section ONLY when command list is empty  
		// otherwise our command will be done in near future, we just add it at the
		// end of the queue
		if (list_empty(&sec->cmd_queue_head))
			section_send_cmd(TAPEDEV_CMD_EJECT_TAPE, sec);

		list_add_tail(&node->lst_link, &sec->cmd_queue_head);

		while (!sec->ioctl_cmd_done) 
		{
			spin_unlock_irqrestore(&sec->lock, flags);
			if (wait_event_interruptible(sec->ioctl_eject_wait_q, sec->ioctl_cmd_done))
				return -ERESTARTSYS;
			spin_lock_irqsave(&sec->lock, flags);
		}

		sec->ejection_cmds--;
		sec->ioctl_cmd_done = false;

		// TODO: this is prone to some data races - we ejected tape but before we are
		// 	able to acquire lock, comes request that inserts tape, so current tape is
		// 	not 0, thus we cannot check current_tape value, we will check 
		// 	ioctl_status, this is less likely to fail
		// ----> probably we should create a queue of results for ioctl commands
		// 	and simply remove first node from it and check status
		// ----> for now below should probably work
		if (sec->ioctl_status == TAPEDEV_SECT_STATUS_ERR_NO_TAPE)
		{
			pr_warn("%s:%u: EJECT_TAPE ioctl cmd wanted to eject when there is NO TAPE inserted \n", __func__, __LINE__);
			sec->ioctl_status = 0;
			spin_unlock_irqrestore(&sec->lock, flags);
			return -EIO;
		}

		sec->ioctl_status = 0;
		// Once current tape is 0, we return success
		spin_unlock_irqrestore(&sec->lock, flags);
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

/*
	Depending on the cmd type:
		- arg1 is for 8-31 bits, arg2 NOT PRESENT 
		- arg1 is for 23-31 bits, arg2 PRESENT
		- arg2 is only for 8-22 bits (arg2 is number of blocks to READ/WRITE)
*/
static inline uint32_t create_tapedev_cmd(uint32_t cmd_type, uint32_t arg1, uint32_t arg2)
{
	uint32_t cmd;

	switch (cmd_type) 
	{
		case TAPEDEV_CMD_TAKE_TAPE:
			pr_warn("%s:%u: creating  TAPEDEV_CMD_TAKE_TAPE\n", __func__, __LINE__);
			// arg1, bits 8-31, can only have first 24 bits non zero, but we don't 
			// need to apply mask, since we are shifting to the left and if there are
			// more non-zero bits, they will be discarded; so we just shift to the 
			// left
			cmd = arg1 << 8;
			cmd = cmd | TAPEDEV_CMD_TAKE_TAPE;
		break;
		
		case TAPEDEV_CMD_EJECT_TAPE:
			pr_warn("%s:%u: creating TAPEDEV_CMD_EJECT_TAPE\n", __func__, __LINE__);
			cmd = TAPEDEV_CMD_EJECT_TAPE;
		break;

		case TAPEDEV_CMD_REWIND:
			pr_warn("%s:%u: creating TAPEDEV_CMD_REWIND\n", __func__, __LINE__);
			cmd = TAPEDEV_CMD_REWIND;
		break;

		case TAPEDEV_CMD_FAST_FWD:
			pr_warn("%s:%u: creating TAPEDEV_CMD_FAST_FWD\n", __func__, __LINE__);
			cmd = arg1 << 8;
			cmd = cmd | TAPEDEV_CMD_FAST_FWD;
		break;

		case TAPEDEV_CMD_READ:
		case TAPEDEV_CMD_WRITE:
			pr_warn("%s:%u: creating TAPEDEV_CMD_READ/WRITE\n", __func__, __LINE__);
			// arg1 is offset counted in blocks, bits 23-31, should have only 9 bits
			// thus as a safety check we allow it to have only first nine bits not 0
			cmd = (arg1 & 0x1ff) << 23;
			// arg2, bits 8-22, should have only 15 bits so we mask it
			cmd = cmd | ((arg2 & 0x7fff) << 8);
			cmd = cmd | cmd_type;
		break;		

		default:
			// error,
			pr_err("%s:%u: unsupported command: %u\n", __func__, __LINE__, cmd_type);
			cmd = TAPEDEV_CMD_UNSUPPORTED;
		break;
	}

	return cmd;
}

static int enqueue_new_cmd(uint32_t cmd, struct list_head *cmd_lst_head)
{
	struct lst_node *new_lst_node = kzalloc(sizeof(*new_lst_node), GFP_KERNEL);

	if (!new_lst_node) 
	{
		pr_err("%s:%u: failed to alloc node for cmd\n", __func__, __LINE__);
		return -ENOMEM;
	}
	new_lst_node->cmd = (struct section_cmd) {
			.cmd = cmd,
			.is_ioctl = false
	};
	list_add_tail(&new_lst_node->lst_link, cmd_lst_head);

	return 0;
}

static void _free_enqueued_cmds(struct list_head *cmd_lst_head)
{
	while (!list_empty(cmd_lst_head))
	{
		struct lst_node *node = list_first_entry(cmd_lst_head, struct lst_node, lst_link);

		list_del(&node->lst_link);
		kfree(node);
	}
}

static int do_scatter_gather(struct request *req, struct section *sec, int write, struct list_head *cmd_lst_head)
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

	pr_warn("%s:%u: Doing scatter gather\n", __func__, __LINE__);

	struct tapedev_device *dev = sec->private_data; 
	uint64_t *pgt_buf = sec->cpu_dma_buf;
	uint32_t section_blk_type = section_read_from(TAPEDEV_SECT_TAPE_BLOCKSIZE_ADDR, sec);
	uint32_t section_blk_size = sec->blk_size;

	pr_warn("%s:%u: DOING SCATTER GATHER section: %u, section size: %u, blk_type: %u, blk_size: %u, one tape has: %u sectors \n", __func__, __LINE__, sec->idx, SIZE_OF_SECTION(sec->section_type, sec->n_tapes), section_blk_type, section_blk_size, (SIZE_OF_TAPE(sec->section_type) / 512));
	// TODO:
	// Even kmalloc inside queue_rq is BAD --> what we should do is preallocate sg
	// 	array inside requests private memory,
	// 	we should create a current request context which will be a state machine
	// 	and here instead of creating all cmds and then starting our task we will
	// 	set state machine to eject and irq handler will handle the rest and 
	// 	transition to the next steps

	// TODO: maybe this sg should be inside section struct, but if it was we would
	// 	need lock here, but on the other hand our section can do one request at a 
	// 	time, so no-one will create more requests until we call blk_mq_end_request
	// mtip32xx.c - mtip_hw_submit_io 
	struct scatterlist *main_sg = kzalloc((sizeof(*main_sg) * MAX_SG_PGT_ENTRIES), GFP_KERNEL);
	if (!main_sg)
	{
		return BLK_STS_IOERR;
	}
	sg_init_table(main_sg, MAX_SG_PGT_ENTRIES);

	int n_collapsed_segments = blk_rq_map_sg(req, main_sg);
	// nents might be less than n_collapsed_segments, if its 0 it means error
	int nents = dma_map_sg(
		&dev->pdev->dev, 
		main_sg, 
		n_collapsed_segments, 
		write ? DMA_TO_DEVICE : DMA_FROM_DEVICE
	);

	if (!nents)
		return BLK_STS_IOERR;

	pr_warn("%s:%u: final nbr of nents: %d\n", __func__, __LINE__, nents);
	struct scatterlist *sg;
	int i;
	uint32_t total_blocks = 0;

	// mtip32xx.c - fill_command_sg
	for_each_sg(main_sg, sg, nents, i)
	{
		dma_addr_t dma_addr = sg_dma_address(sg);
		uint32_t dma_len = sg_dma_len(sg);
		uint32_t nbr_of_blocks = dma_len / section_blk_size;

		if (!IS_ALIGNED(dma_addr, 512)) 
		{
			pr_err("%s:%u: dma_addr is not 512 byte aligned\n", __func__, __LINE__);
		}
		if (dma_len % section_blk_size != 0)
		{
			pr_err("%s:%u: dma_len is not multiple of block size\n", __func__, __LINE__);
		}
		// TODO: conitnue with this approach then see if works and change to better
		// 	state machine with pre-allocation so that in queue_rq there is no alloc
		pgt_buf[i] = (dma_addr  >> 9);
		pgt_buf[i] = pgt_buf[i] << 32;
		pgt_buf[i] = pgt_buf[i] | ((uint64_t)nbr_of_blocks);
		total_blocks += nbr_of_blocks;
	}

	pr_warn("%s:%u: total blocks we will read/write: %u\n", __func__, __LINE__, total_blocks);
	uint32_t cmd = create_tapedev_cmd(write ? TAPEDEV_CMD_WRITE : TAPEDEV_CMD_READ, 0, total_blocks);

	if (enqueue_new_cmd(cmd, cmd_lst_head))
	{
		_free_enqueued_cmds(cmd_lst_head);
		dma_unmap_sg(
			&dev->pdev->dev, 
			main_sg, 
			n_collapsed_segments, 
			write ? DMA_TO_DEVICE : DMA_FROM_DEVICE
		);
		return BLK_STS_IOERR;
	}

	uint32_t nodes_in_lst = list_count_nodes(cmd_lst_head);
	pr_warn("%s:%u: nodes in cmd qeueu AFTER sg: %u \n", __func__, __LINE__, nodes_in_lst);
	// // after shifting there should be no 1 bits in high range
	// u64 high_addr = (hw_mem_addr >> 9) & 0xffffffff00000000;
	// u64 low_addr = (hw_mem_addr >> 9) & 0xffffffffULL;

	pr_warn("%s:%u: scheduling read/write command\n", __func__, __LINE__);
	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

	if (list_empty(&sec->cmd_queue_head))
	{
		// We schedule commands if section cmd queue is emmpty 
		struct lst_node *node = list_first_entry(cmd_lst_head, struct lst_node, lst_link);
		pr_warn("%s:%u: scheduling cmd: %u \n", __func__, __LINE__, GET_CMD_TYPE(node->cmd.cmd));
		section_send_cmd(node->cmd.cmd, sec);
	}

	uint32_t cmd_queue_size = list_count_nodes(&sec->cmd_queue_head);
	pr_warn("%s:%u: cmd_queue_size: %u \n", __func__, __LINE__, cmd_queue_size);
	// otherwise we just add new commands
	list_splice_tail_init(cmd_lst_head, &sec->cmd_queue_head);

	cmd_queue_size = list_count_nodes(&sec->cmd_queue_head);
	pr_warn("%s:%u: cmd_queue_size AFTER list_splice_tail_init: %u \n", __func__, __LINE__, cmd_queue_size);

	spin_unlock_irqrestore(&sec->lock, flags);

	return BLK_STS_OK;
}


static int create_start_pos_setup_cmds(u64 start_sector, struct list_head *cmd_lst_head, struct section *sec)
{
	// For reading section_type, n_tapes, idx, WE DON'T NEED A LOCK of sec
	int err = 0;
	pr_warn("%s:%u: Starting creating start pos setup cmds\n", __func__, __LINE__);
	// Each sector is 512 bytes, each tape has (SIZE_OF_TAPE(s_type) / 512) sectors
	// - Firstly we need to find out which tape has our sector 
	// - Then fastforward our tape by correct number of sectors to start reading/
	// 		writing at start_sector
	// TODO: if we change blocksize do sectors also change?
	// uint32_t n_tapes = sec->n_tapes;
	uint32_t all_512byte_sectors = GET_NBR_OF_SECTORS(sec->section_type, sec->n_tapes);

	// We count sectors from 0
	if (start_sector >= all_512byte_sectors)
	{
		pr_err("%s:%u: start_sector: %llu >= %u all_sectors available in this section\n", __func__, __LINE__, start_sector, all_512byte_sectors);
		err = -EINVAL;
		goto ret;
	}

	// TODO: change so that it works with other blocksizes
	// For now we will operate on 512 blocks
	// how many 512 byte sectors one tape has
	uint32_t tape_512byte_sectors = (SIZE_OF_TAPE(sec->section_type) / 512);

	// i.e. if tapes have 200 sectors, and our start_sector is 198 we will get 0 
	// from below division and that's correct since we want tape of number 0
	uint32_t tape_nbr = start_sector / tape_512byte_sectors;
	// i.e. if start_sector is 403, we get tape 2, and we should start from 
	// 	403 - 2 * 200 = 3 sector within tape 2
	uint32_t start_sector_within_tape = start_sector - tape_nbr * tape_512byte_sectors;

	// We count tapes starting from 1
	tape_nbr++;

	pr_warn("%s:%u: section: %u, tape_sectors: %u, wanted tape_nbr: %u, start_sector: %llu, start_sector_within_tape: %u\n", __func__, __LINE__, sec->idx, tape_512byte_sectors, tape_nbr, start_sector, start_sector_within_tape);

	uint32_t cmd;

	cmd = create_tapedev_cmd(TAPEDEV_CMD_EJECT_TAPE, 0, 0);
	if (enqueue_new_cmd(cmd, cmd_lst_head))
	{
		err = -ENOMEM;
		goto free_cmd_queue;
	}

	cmd = create_tapedev_cmd(TAPEDEV_CMD_TAKE_TAPE, tape_nbr, 0);
	if (enqueue_new_cmd(cmd, cmd_lst_head))
	{
		err = -ENOMEM;
		goto free_cmd_queue;
	}

	cmd = create_tapedev_cmd(TAPEDEV_CMD_REWIND, 0, 0);
	if (enqueue_new_cmd(cmd, cmd_lst_head))
	{
		err = -ENOMEM;
		goto free_cmd_queue;
	}

	cmd = create_tapedev_cmd(TAPEDEV_CMD_FAST_FWD, start_sector_within_tape, 0);
	if (enqueue_new_cmd(cmd, cmd_lst_head))
	{
		err = -ENOMEM;
		goto free_cmd_queue;
	}

	pr_warn("%s:%u: Moving to correct pos ENDED\n", __func__, __LINE__);
	goto ret;

free_cmd_queue:
	_free_enqueued_cmds(cmd_lst_head);
ret:
	return err;
}

static inline int submit_request_sg(struct request *req, struct section *sec)
{
	// TODO:
	// !!!!!!!!!!!!!!!!! CHECK MORE IN DEPTH !!!!!!!!!!!!!!!!!!!!!
	// blk_rq_map_sg - maybe we should change workflow to use blk_rq_map_sg, and 
	// 	then operate and calculate stuff using these scatter gather entities
	// !!!!!!!!!!!!!!!!! !!!!!!!!!!!!!!!!!!! !!!!!!!!!!!!!!!!!!!!!
	pr_warn("%s:%u: START submit_request_sg\n", __func__, __LINE__);

	int err = BLK_STS_OK;
	// struct bio_vec bvec;
	// struct req_iterator iter;
	// TODO: We should probably use here TAPEDEV_SECT_PTR_SHIFT instead of SECTOR_SHIFT????
	// loff_t pos = blk_rq_pos(req) << TAPEDEV_SECT_PTR_SHIFT;
	// loff_t dev_size = (s->n_sectors << TAPEDEV_SECT_PTR_SHIFT);


	// Plan:
	// 1) Inside section we need to store list of commands
	// 2) Here in submit_request_sg we will create a list of commands that
	// 		will set correct tape fastforward it etc and add it to the list
	// 3) after that (while still having a lock) we will iterate over biovecs using
	// 		blk_rq_map_sg to create scatter gather list
	// 4) Then we will iterate over created scatter gather list and create read/
	// 		write commands while simultaneously checking if we are still on correct
	// 		tape, if not we will add command to the list of commands and then add 
	// 		commands that change tapes
	// 5) Once all commands are added we can safely return from submit_request_sg 
	// 		without any waiting in here, since we cannot wait in this context, kernel
	// 		throws some warnings/errors when we do
	// 6) We need to remember req so that later we can do blk_mq_end_request(req, 
	// 		status);
	// 7) Before calling blk_mq_end_request() we shouldn't get other queue_rq call

	// TODO: check this, why is it like that etc.
	// start sector is always in 512byte sectors (we set that value in queue_lim)
	// i.e.
	// | ----------- 0	  | sector 0
	// | 				  | 
	// | ----------- 511  |
	// | ----------- 512  | sector 1
	// | 				  | 
	// | ----------- 1023 |
	uint64_t start_sector = blk_rq_pos(req); 
	// To get POSITION in our file from the sector, we multiple it by 512 = 2^9
	// uint64_t pos = start_sector << 9;
	uint64_t sectors_to_read = blk_rq_sectors(req);

	struct list_head cmd_lst_head;
	INIT_LIST_HEAD(&cmd_lst_head);
	// TODO: ADD SANITY CHECKS EVERYWHERE whether given addresses are 512byte aligned

	pr_warn("%s:%u: start_sector: %llu, sectors_to_read: %llu\n", __func__, __LINE__, start_sector, sectors_to_read);

	// We must create sequence of commands and blk_mq_tag_setadd the to the cmd queue
	// After set_section_start_pos if we had a lock we still have it
	if (create_start_pos_setup_cmds(start_sector, &cmd_lst_head, sec))
	{
		err = BLK_STS_IOERR;
		goto ret;
	}

	uint32_t nodes_in_lst = list_count_nodes(&cmd_lst_head);
	pr_warn("%s:%u: nodes in cmd qeueu AFTER pos setup: %u \n", __func__, __LINE__, nodes_in_lst);


	switch (req_op(req)) 
	{
	case REQ_OP_WRITE:
		pr_warn("%s:%u: WRITE %llu sectors starting at %llu\n",
			__func__, __LINE__, sectors_to_read, start_sector);

		err = do_scatter_gather(req, sec, 1, &cmd_lst_head);
		break;
	case REQ_OP_READ:
		pr_warn("%s:%u: READ %llu sectors starting at %llu\n",
			__func__, __LINE__, sectors_to_read, start_sector);

		err = do_scatter_gather(req, sec, 0, &cmd_lst_head);
		break;
	default:
		pr_warn("%s:%u: submitted bad request, supported requests are READ and WRITE\n", __func__, __LINE__);
		err = BLK_STS_NOTSUPP;
		goto ret;
	}

	// We will add scatter gather table, add commands and remember req
	// if needed we will start commands execution if sec->cmd_lst empty
	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

	sec->req = req;

	spin_unlock_irqrestore(&sec->lock, flags);
ret:
	return err;
}

static inline int process_request(struct request *req, struct section *sec)
{
	switch (req_op(req)) {
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		pr_warn("%s:%u: got READ/WRITE request\n", __func__, __LINE__);
		return submit_request_sg(req, sec);
	default:
		pr_err("%s:%u :: unsupported request type: %d\n", __func__, __LINE__, req_op(req));
		return BLK_STS_NOTSUPP;
	}
}

static blk_status_t tapedev_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
	struct request_queue *q = hctx->queue;
	struct section *sec = q->queuedata;
	// struct tapedev_device *dev = sec->private_data;
	// unsigned int nr_bytes = 0;
	blk_status_t status = BLK_STS_OK;
	struct request *req = bd->rq;

	// might_sleep();
	// cant_sleep(); /* cannot use any locks that make the thread sleep */

	pr_warn("%s:%u: starting request for section: %u\n", __func__, __LINE__, sec->idx);
	blk_mq_start_request(req);

	status = process_request(req, sec);

	if (status)
		blk_mq_end_request(req, status);

	pr_warn("%s:%u: ENDED request creation and scheduling for section: %u\n", __func__, __LINE__, sec->idx);
	return status;
}

static struct blk_mq_ops mq_ops = {
	.queue_rq = tapedev_queue_rq,
};


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
	u64 req_dma_mask = dma_get_required_mask(&pdev->dev);
	pr_info("tapedev_probe - req_dma_mask: %llu\n", req_dma_mask);

	// Our device has only 32-bit registers, so DMA mask needs to be 32 bit, thanks
	// to that address returned by dma_alloc_coherent should be valid 32bit address 
	// aligned to 512byte boundary stored in 64bit variable, meaning after shifting 
	// 9 bits to the right we should get correct 32bit address
	if ((err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(41))))
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

	pr_warn("%s:%u: device enabled, returned from waiting\n", __func__, __LINE__);
	// After enabling device we need to read num_sections data from it
	uint32_t num_sections = tapedev_ior(tape_dev, TAPEDEV_SECTIONS_ADDR);
	pr_warn("%s:%u: device n_sections AFTER successfu; enabling the device: %u \n", __func__, __LINE__, num_sections);

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

	err = create_sections(tape_dev, num_sections);
	if (err) goto free_sections;

	pr_warn("%s:%u: after create_sections\n", __func__, __LINE__);

	pr_warn("%s:%u: enabling sections interrupts\n", __func__, __LINE__);
	uint32_t irq_mask = (0xffffffff ^ (1 << TAPEDEV_IRQ_INIT_DONE)) ^ (1 << TAPEDEV_IRQ_HW_ERROR);

	// TODO: add helper function: _enable_dev_interrupts

	// Now we enable interrupts for this device's sections
	for (int sec_id = 0; i < num_sections; i++)
	{
		irq_mask = irq_mask ^ (1 << TAPEDEV_IRQ_SECT_X_DONE(sec_id));
		irq_mask = irq_mask ^ (1 << TAPEDEV_IRQ_SECT_X_ERROR(sec_id));
	}

	pr_warn("%s:%u: before enabling intrpts for this device's sections\n", __func__, __LINE__);
	tapedev_iow(
		tape_dev, 
		TAPEDEV_IRQ_MASK_ADDR, 
		irq_mask	
	);
	pr_warn("%s:%u: after enabling intrpts for this device's sections\n", __func__, __LINE__);

	// TODO: add helper function: _set_dma_hw_buff

	// And now for each section we need to set its dma address, they need to be 
	// aligned to 512 byte boundary
	// Explanation of alignment (stackoverflow): 4-alignment simply means that the 
	// pointer, when considered as a numeric address, is a multiple of 4. If the 
	// pointer is not a multiple of the required alignment, then it is unaligned. 
	pr_warn("%s:%u: setting section buffer ptr addresses\n", __func__, __LINE__);
	for (int sec_id = 0; sec_id < num_sections; sec_id++)
	{
		// dma_addr is 64, it is 512 byte aligned, so bits 0-8 are zeroed, 
		// we want bits 9-40
		// So we firstly shift dma_addr 9 times to the right, then zero bits 32-63 
		// then cast to uint32
		uint32_t dma_hw_buf_addr = 
			(uint32_t)((tape_dev->sections[sec_id]->dma_addr >> 9) & 0xffffffffULL);

		section_iow(tape_dev, GET_SECTION_ADDR(sec_id), 	
			TAPEDEV_SECT_BUFFER_PTR_ADDR, dma_hw_buf_addr);
		
		tape_dev->sections[sec_id]->current_tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, tape_dev->sections[sec_id]);

		pr_warn("%s:%u: section: %u, current tape: %u\n", __func__, __LINE__, sec_id, tape_dev->sections[sec_id]->current_tape);
	}
	pr_warn("%s:%u: ended setting buffer ptr addresses\n", __func__, __LINE__);

	pr_warn("%s:%u: BEFORE add_section_disks\n", __func__, __LINE__);
	err = add_section_disks(tape_dev, num_sections);
	if (err) goto free_sections;

	pr_warn("%s:%u: after add_section_disks\n", __func__, __LINE__);

	return 0;
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

	for (int s_id = 0; s_id < tape_dev->n_sections; s_id++)
	{
		sysfs_remove_group(&disk_to_dev(tape_dev->sections[s_id]->gdisk)->kobj, &tape_attr_group);
		del_gendisk(tape_dev->sections[s_id]->gdisk);
		put_disk(tape_dev->sections[s_id]->gdisk);
		kfree(tape_dev->sections[s_id]);
		// TODO: inside section we also initialized some stuff like lists etc.
		// they probably need to be deleted too
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
	// I assume that blksize (512, 1024, 2048, 4096, 8192) is set in stone at 
	// tapedevice start, thus based on this value we can setup queue_limits so that
	// block layer can serve us correctly aligned data
	uint32_t blksize = GET_BLOCK_SIZE(section_ior(tape_dev, GET_SECTION_ADDR(section_id), TAPEDEV_SECT_TAPE_BLOCKSIZE_ADDR));

	// Knowing how many blocks we can read/write per one command and knowing block
	// size we can calculate how many 512byte sectors at most we can handle
	// max_hw_sectors is always in 512-byte units
	uint32_t max_nbr_of_sectors = (MAX_BLOCKS_PER_ONE_CMD * blksize) / 512;

	struct queue_limits lim = {
		.logical_block_size	= blksize, // so that we read/write in blksize blocks
		.physical_block_size = blksize, 
		.io_min = blksize,
		.max_hw_sectors = max_nbr_of_sectors,
		.max_segments = 512, // so that at most we have 512 entries in pg table
		.dma_alignment = 511, // so that dma addresses are 512 byte aligned
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
	sec->blk_size = blksize;
	sec->n_sectors = GET_NBR_OF_SECTORS(sec_type, n_tapes);
	sec->current_tape = 0;
	sec->ejection_cmds = 0;

	init_waitqueue_head(&sec->ioctl_eject_wait_q);
	init_waitqueue_head(&sec->cmd_wait_q);

	sec->curr_cmd = NO_CMD;
	sec->next_cmd = NO_CMD;
	sec->ioctl_cmd_done = false;
	sec->req = NULL;
	INIT_LIST_HEAD(&sec->cmd_queue_head);
	sec->private_data = tape_dev;
	sec->status = 0;
	sec->ioctl_status = 0;
	// s->data_buf = kzalloc(SIZE_OF_TAPE(s->section_type) * s->n_sectors, GFP_KERNEL);

	// dma_alloc_coherent - allocates a memory region accessible simultaneously by 
	// both the CPU and hardware, of size DMA_BUF_SIZE. Cpu (so our programme) must
	// access it via cpu_dma_buf. We must send/set dma_addr to hardware so that it 
	// can also access our dma memory 
	// cpu_dma_buf and dma_addr are already 512 byte aligned thanks to 
	// dma_alloc_coherent
	if (!(sec->cpu_dma_buf = dma_alloc_coherent(&tape_dev->pdev->dev,
			TAPEDEV_BUF_PGTABLE_SIZE,
			&sec->dma_addr, GFP_KERNEL))) 
	{
		pr_err("%s:%u: Failed to dma_alloc_coherent\n", __func__, __LINE__);
		err = -ENOMEM;
		goto free_alloc_s;
	}

	if (!IS_ALIGNED(sec->dma_addr, 512)) 
	{
		pr_err("%s:%u: page table DMA address is not 512-byte aligned\n", __func__, __LINE__);
		err = -EINVAL;
		goto free_dma_alloc;
	}

	// To be 512 byte aligned our first 9 bits (0 - 8) need to be zeroed, so that 
	// in our address = 2^i + 2^(i+1) + ..., i >= 9, since 2^9 = 512, and if i >= 9
	// all addresses will be divisible by 512 so aligned to 512 byte boundary

	// err = init_tag_set(&sec->tag_set, sec);

	// Each section device can process only ONE request at the time, thus we set 
	// queue_depth as 1
	err = blk_mq_alloc_sq_tag_set(&sec->tag_set, &mq_ops, 1, 0);
	if (err) {
		pr_err("%s:%u: Failed to allocate tag set\n", __func__, __LINE__);
		goto free_dma_alloc;
	}

	// inside blk_mq_alloc_disk we set queuedata (which is private data) to sec
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
	// Each tape has SIZE_OF_TAPE so every tape will have SIZE_OF_TAPE / 512 sectors
	set_capacity(sec->gdisk, GET_NBR_OF_SECTORS(sec_type, n_tapes));


	*new_section = sec;

	// We dont add_disk here yet, we will do that once all of them are ready

	return 0;

free_tag_set:
	blk_mq_free_tag_set(&sec->tag_set);
free_dma_alloc:
	dma_free_coherent(&tape_dev->pdev->dev, TAPEDEV_BUF_PGTABLE_SIZE, sec->cpu_dma_buf, sec->dma_addr);
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
				dma_free_coherent(&tape_dev->pdev->dev, PAGE_SIZE, tape_dev->sections[i]->cpu_dma_buf, tape_dev->sections[i]->dma_addr);
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
		pr_warn("%s:%u: adding section: %u\n", __func__, __LINE__,  s_id);
		// err = device_add_disk(&tape_dev->pdev->dev, tape_dev->sections[s_id]->gdisk, NULL);
		// pr_info("%s:%u: adding disk for section: %d \n", __func__, __LINE__, s_id);
		// Once we expose block devices with add_disk(), block layer issues a read of
		// sector 0 to look for a partition table or filesystem signature. This is 
		// why before running tests or anything and after insmod I got READ commands
		err_add = add_disk(tape_dev->sections[s_id]->gdisk);

		pr_warn("%s:%u: creating sysfs group\n", __func__, __LINE__);
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
				dma_free_coherent(&tape_dev->pdev->dev, PAGE_SIZE, s->cpu_dma_buf, s->dma_addr);
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
