#include "tapedev_defs.h"

const struct section_cmd NO_CMD = {
	.cmd = TAPEDEV_CMD_NONE,
	.is_ioctl = false,
	.nents = 0,
	.dir = DMA_NONE
};
