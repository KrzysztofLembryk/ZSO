#ifndef TAPEDEV_IOW_IOR_H
#define TAPEDEV_IOW_IOR_H

#include "tapedev_defs.h"

// iowrite32/ioread32 are atomic - TODO: must check if there are no data races 

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

static inline void section_iow(struct tapedev_device *dev, uint32_t section_offset, uint32_t reg, uint32_t val)
{
	iowrite32(val, dev->bar + section_offset + reg);
	// printk(KERN_ALERT "tapedev_iow :: tapedev %03x <- %08x\n", reg, val);
}

static inline uint32_t section_ior(struct tapedev_device *dev, uint32_t section_offset, uint32_t reg)
{
	uint32_t res = ioread32(dev->bar + section_offset + reg);
	// printk(KERN_ALERT "tapedev_ior :: tapedev %03x -> %08x (res)\n", reg, res);
	return res;
}

#endif // TAPEDEV_IOW_IOR_H