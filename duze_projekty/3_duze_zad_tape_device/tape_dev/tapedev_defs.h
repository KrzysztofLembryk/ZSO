#ifndef TAPEDEV_DEFS_H
#define TAPEDEV_DEFS_H

#include <linux/types.h>
#include <linux/blk-mq.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>

// I would add it to tapedev.h but probably it will be replaced with default .h file
#define TAPEDEV_CMD_NONE				0x06

struct section_cmd
{
	uint32_t cmd;
	bool is_ioctl;
};

// To create section object use create_section function
struct section
{
	uint32_t idx;
	uint32_t n_tapes;
	// 0 to 4, to calc size of tape for this section use SIZE_OF_TAPE macro
	uint32_t section_type; 	
	sector_t n_sectors;
	// If 0 - no tape inserted in tapedevice
	uint32_t current_tape;
	// Number of ejection commands waiting to be executed, the same number of threads
	// are waiting on ejection_queue
	uint32_t ejection_cmds;
	wait_queue_head_t ioctl_eject_wait_q;
	wait_queue_head_t cmd_wait_q;
	struct section_cmd curr_cmd;
	struct section_cmd next_cmd;
	bool cmd_done;
	int status;
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
	struct request *req; 
	// private_data must be tapedev_device
	void *private_data;
};

struct tapedev_device {
	uint32_t idx;
	struct pci_dev *pdev;
	void __iomem *bar;
	spinlock_t s_lock;
	uint32_t n_sections;
	// struct gendisk *parent_gdisk;
	// An array of sections, from 1 to 8 sections
	struct section **sections;
	// struct list_head buffers_free;
	// struct list_head buffers_running;
	wait_queue_head_t wq_free;
	wait_queue_head_t wq_idle;
	int init_done;	/* 0 - not done, 1 - done, -1 - failed*/
	int status; 	/* 0 - ok, negative number - error */
}; 

#endif // TAPEDEV_DEFS_H