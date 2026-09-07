#ifndef TAPEDEV_IOW_IOR_H
#define TAPEDEV_IOW_IOR_H

#include "tapedev_defs.h"
#include "tapedev.h"

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

static inline void section_send_cmd(uint32_t cmd, struct section *sec)
{
	section_iow(sec->private_data, GET_SECTION_ADDR(sec->idx), TAPEDEV_SECT_CMD_ADDR, cmd);
}

static inline uint32_t section_read_from(uint32_t addr, struct section *sec)
{
	return section_ior(sec->private_data, GET_SECTION_ADDR(sec->idx), addr);
}


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

#endif // TAPEDEV_IOW_IOR_H