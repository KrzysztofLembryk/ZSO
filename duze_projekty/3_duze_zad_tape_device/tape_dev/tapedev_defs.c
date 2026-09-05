#include "tapedev_defs.h"

const struct req_state NULL_REQ_STATE = {
	.cmd = TAPEDEV_CMD_NONE,
	.is_ioctl = false,
	.nents = 0,
	.dir = DMA_NONE
};
