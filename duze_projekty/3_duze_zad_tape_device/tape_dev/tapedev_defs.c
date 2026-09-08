#include "tapedev_defs.h"

const struct req_state NULL_REQ_STATE = {
	.cmd = TAPEDEV_CMD_NONE,
	.is_ioctl = false,
	.is_being_executed = false,
	.sg_idx = 0,
	.original_nents = 0,
	.nents = 0,
	.is_write = false,
	.data_direction = DMA_NONE,
	.tape_nbr = 0,
	.prev_tape_nbr = 0,
	.start_block_within_tape = 0,
	.total_blocks_in_tape = 0,
	.left_blocks_in_tape = 0,
	.device_pgt_offset = 0,
	.completed = false,
	.stopped_at_idx = 0,
	.rewind_pgt_buff = false,
};
