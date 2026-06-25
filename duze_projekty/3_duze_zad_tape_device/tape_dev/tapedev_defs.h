#ifndef TAPEDEV_DEFS_H
#define TAPEDEV_DEFS_H

#include <linux/types.h>
#include <linux/blk-mq.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>

struct section
{
	uint32_t idx;
	uint32_t n_tapes;
	// 0 to 4, to calc size of tape for this section use SIZE_OF_TAPE macro
	uint32_t section_type; 	
	sector_t n_sectors;
	uint32_t current_tape;
	/*
		gendisk is kernel's representation of of an individual DISK DEVICE
	*/
	struct gendisk *gdisk;
	spinlock_t lock; /* For mutual exclusion */
	// struct request_queue *queue; /* The device request queue */
	
	dma_addr_t data_dma;
	void *data_cpu;
	struct blk_mq_tag_set tag_set; /* multi queue */

	void *private_data;
};

// We will make an array of tapedev_devices
struct tapedev_device {
	uint32_t idx;
	struct pci_dev *pdev;
	void __iomem *bar;
	spinlock_t s_lock;
	// An array of sections, from 1 to 8 sections
	uint32_t n_sections;
	// struct gendisk *parent_gdisk;
	struct section **sections;
	// struct list_head buffers_free;
	// struct list_head buffers_running;
	wait_queue_head_t wq_free;
	wait_queue_head_t wq_idle;
	int init_status;
}; 

#endif // TAPEDEV_DEFS_H