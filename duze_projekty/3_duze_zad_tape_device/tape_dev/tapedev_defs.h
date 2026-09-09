#ifndef TAPEDEV_DEFS_H
#define TAPEDEV_DEFS_H

#include <linux/types.h>
#include <linux/blk-mq.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>

// I would add it to tapedev.h but probably it will be replaced with default .h file
#define TAPEDEV_CMD_NONE				0x06
#define TAPEDEV_CMD_UNSUPPORTED			0xffffffff
#define TAPEDEV_SECT_FATAL_ERROR		0x8A
#define IOCTL_STATUS_OK					0

#define MAX_DEVICES_TAPEDEV 256
#define MAX_SG_PGT_ENTRIES 1024
// From task description we have 8-22 bits for number of blocks to read, thus we have
// 15 bits to encode this number, max number we can encode is 0x7fff (all 15 bits 1)
#define MAX_BLOCKS_PER_ONE_CMD 0x7fff 
#define BASE_MINOR 0
#define MINORS_COUNT 8
#define TAPEDEV_NAME "tapedev"
#define BAR_ID 0
#define BAR_MAXLEN 0
#define NO_TAPE 0 
#define NO_ARG 0	/* for create_tapedev_cmd function */

#define PHYSICAL_BLOCK_SIZE 8192
#define BASE_TAPE_SIZE (32 * 8192)

#define GET_BLOCK_SIZE(blk_type) ((1 << blk_type) * 512)
#define GET_SECTION_ADDR(s_id) ((s_id + 1) * 0x100)
#define SIZE_OF_TAPE(s_type) ((1 << s_type) * BASE_TAPE_SIZE)
#define GET_TOTAL_NBR_OF_512B_SECTORS(s_type, n_tapes) ((SIZE_OF_TAPE(s_type) / 512) * n_tapes)
#define SIZE_OF_SECTION_IN_BYTES(s_type, n_tapes) (SIZE_OF_TAPE(s_type) * n_tapes)
#define GET_NBR_OF_BLOCKS_IN_TAPE(s_type, blk_size) (SIZE_OF_TAPE(s_type) / blk_size)

#define TAPEDEV_IRQ_SECT_X_DONE(i)  (TAPEDEV_IRQ_SECT_0_DONE  + (i))
#define TAPEDEV_IRQ_SECT_X_ERROR(i) (TAPEDEV_IRQ_SECT_0_ERROR + (i))

// Bits 0-7 are the identifier of the command, cmd should have 32 bits.
#define GET_CMD_TYPE(cmd) ((uint32_t)((cmd) & 0xffU))
// Bits 8-31 are used to pass command-specific information.
#define GET_CMD_BODY(cmd) ((uint32_t)((cmd) & 0xffffff00U))
// State must be of type struct req_state
#define IS_NULL_REQ_STATE(state) (GET_CMD_TYPE(state.cmd) == TAPEDEV_CMD_NONE)

struct rewind_data
{
	bool do_rewind;
	uint64_t overflow_blocks;
	uint64_t inserted_blocks;
	uint64_t old_pgt_entry;
	int idx;
};

extern const struct rewind_data NO_REWIND; 

struct req_state
{
	uint32_t cmd;
	bool is_ioctl;
	bool is_being_executed;

	// Current idx needed to know when there are no more entities to read in request
	int sg_idx;
	// Needed to unmap dmap
	int original_nents;
	// Number of elements in pgt_buf
	int nents;

	bool is_write;
	enum dma_data_direction data_direction;

	uint32_t tape_nbr;
	uint32_t prev_tape_nbr;

	// Used in fast forward cmds, we forward tape to the starting block
	uint32_t start_block_within_tape;
	uint32_t total_blocks_in_tape;
	// Used to monitor how many blocks are left in current tape, thanks to that we 
	// know when to take next tape
	uint32_t left_blocks_in_tape;
	// Used for setting correct offset in READ/WRITE commands 
	uint32_t device_pgt_offset;

	// Used to check if request completed, if so, we can end it and free resources
	bool completed;

	// Used to rewind pgt_buf when total_blocks_seen >= 512, otherwise our offset
	// would be truncated, since we have only bits 23-31 to store offset in a cmd
	struct rewind_data rewind_state;
};

// Add at the end with - list_add_tail(&node->link, &section->cmd_queue)
// Check empty - list_empty(&section->cmd_queue)
// Remove:
// 	node = list_first_entry(&section->cmd_queue, struct your_node_type, link)
// 	list_del(&node->link)
struct lst_node
{
	struct req_state cmd;
	struct list_head lst_link;
};

extern const struct req_state NULL_REQ_STATE; 

// To create section object use create_section function
struct section
{
	uint32_t idx;
	uint32_t n_tapes;
	// 0 to 4, to calc size of tape for this section use SIZE_OF_TAPE macro
	uint32_t section_type; 	
	uint32_t blk_size;
	wait_queue_head_t ioctl_eject_wait_q;
	bool ioctl_cmd_done;
	int status;
	int ioctl_status;
	uint32_t curr_tape;
	/*
		gendisk is kernel's representation of of an individual DISK DEVICE
	*/
	struct gendisk *gdisk;
	spinlock_t lock; /* For mutual exclusion */
	
	// dma address the hardware should use
	dma_addr_t dma_addr;
	// data_cpu is our dma buff to which we will write/read scatter gather data
	void *cpu_dma_buf;
	struct blk_mq_tag_set tag_set; /* multi queue */
	struct req_state req_state;
	struct request *req; 
	struct scatterlist *sg_arr;
	struct list_head ioctl_cmd_queue_head;
	// private_data must be tapedev_device
	void *private_data;
};

struct tapedev_device {
	uint32_t idx;
	struct pci_dev *pdev;
	void __iomem *bar;
	spinlock_t s_lock;
	uint32_t n_sections;
	// An array of sections, from 1 to 8 sections
	struct section **sections;
	wait_queue_head_t wq_free;
	wait_queue_head_t wq_idle;
	int init_done;	/* 0 - not done, 1 - done, -1 - failed*/
	int status; 	/* 0 - ok, negative number - error */
}; 

#endif // TAPEDEV_DEFS_H