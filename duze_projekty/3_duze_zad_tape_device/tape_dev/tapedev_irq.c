#include "tapedev_irq.h"
#include "linux/blk_types.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/wait.h"
#include "tapedev.h"
#include "tapedev_defs.h"
#include "tapedev_iow_ior.h"

void _end_request(struct section *sec, blk_status_t status);
int __handle_section_error(uint32_t section_status, struct section *sec);
int __handle_section_done(uint32_t section_status, struct section *sec);
void __handle_next_cmd(struct section *sec);
void __abort_rest_of_req_cmds(struct section *sec);
void end_req_if_completed(struct section *sec, struct req_state *curr_cmd);
void clear_sec_done_intrpt(struct section* sec);
void clear_sec_err_intrpt(struct section* sec);
int _handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec);

int handle_sections_interrupts(uint32_t ir_status, uint32_t num_sections, struct tapedev_device *dev)
{
	uint32_t section_done; 
	uint32_t section_error;
	uint32_t section_status;
	int err = 0;

	for (int sec_id = 0; sec_id < num_sections; sec_id++)
	{
		struct section *sec = dev->sections[sec_id];
		
		// section_done might have big value since we get exact bit that was set,
		// thus we just check if value is greater than 0, if yes section is done 

		section_done = (ir_status & (1 << TAPEDEV_IRQ_SECT_X_DONE(sec_id))) > 0;
		section_error = (ir_status & (1 << TAPEDEV_IRQ_SECT_X_ERROR(sec_id))) > 0;
		section_status = section_read_from(TAPEDEV_SECT_STATUS_ADDR, sec);

		if (section_done || section_error)
		{
			err = err + _handle_section_interrupt(
					section_done, 
					section_error, 
					section_status, 
					sec				
			);
		}

		if (err)
		{
			pr_err("Section: %d handler error: %d\n", sec_id, err);
		}
	}
	return err;
}


int _handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec)
{
	pr_warn("%s:%u: handling intrpt for section %u \n", __func__, __LINE__, sec->idx);
	int err = 0;

	// -------- CRITICAL SECTION Start --------
	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

	if (section_error)
	{
		clear_sec_err_intrpt(sec);
		err = __handle_section_error(section_status, sec);
	}
	else if (section_done)
	{
		pr_info("%s:%u: section_done, section_id: %u, done: %u, STATUS: %u\n", __func__, __LINE__, sec->idx, section_done, section_status);
		clear_sec_done_intrpt(sec);

		// TODO: if handle_section done failed something went REAALLY wrong, so we 
		// end execution without next steps ???
		err = __handle_section_done(section_status, sec);
		if (err)
			goto release_lock;
	}
	else // section IDLE or working
	{
		sec->status = TAPEDEV_SECT_STATUS_IDLE;
		goto release_lock;
	}

	// After handling curr command we schedule next command if present (section DONE 
	// sets next command if there is one, section ERROR aborts all of the commands, 
	// apart from ioctl commands that are still waiting on the queue)
	__handle_next_cmd(sec);

release_lock:
	spin_unlock_irqrestore(&sec->lock, flags);

	return err;
}

static int get_curr_req(struct req_state **curr_req, struct section *sec) 
{
	if (sec->req != NULL && sec->req_state.is_being_executed)
	{
		*curr_req = &(sec->req_state);
	}
	else
	{
		if (!list_empty(&sec->ioctl_cmd_queue_head))
		{
			struct lst_node *curr_cmd_node = 
				list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);
			*curr_req = &(curr_cmd_node->cmd);
		}
		else
		{
			pr_err("%s:%u: section: %d, cmd ended with ERROR but there is no cmd present\n", __func__, __LINE__, sec->idx);
			return -1;
		}
	}
	return 0;
}

// To use this function you MUST FIRST ACQUIRE LOCK
int __handle_section_error(uint32_t section_status, struct section *sec)
{
	// If we got error we probably should ABORT ALL WAITING COMMANDS
	int err = TAPEDEV_SECT_FATAL_ERROR;
	struct req_state *curr_req;

	if (get_curr_req(&curr_req, sec))
		return -err;

	// TODO:
	// pr_err("%s:%u: section: %d, we got error for current command: %u\n", __func__, __LINE__, sec->idx, curr_cmd.cmd);
	// TODO: IDK how we should handle these things yet
	switch (section_status)
	{
		case TAPEDEV_SECT_STATUS_ERR_INVALID_CMD:
			pr_err("%s:%u: section: %d, error: ERR_INVALID_CMD\n", __func__, __LINE__, sec->idx);

			err = TAPEDEV_SECT_STATUS_ERR_INVALID_CMD;
			break;

		case TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE:

			pr_err("%s:%u: section: %d, error: ERR_TAPE_ACTIVE\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE;
			break;

		case TAPEDEV_SECT_STATUS_ERR_NO_TAPE:

			err = TAPEDEV_SECT_STATUS_ERR_NO_TAPE;
			// TODO: if our request ejects tape and there is NO TAPE, we should 
			// kinda ignore this error and continue with execution, since it doesn't
			// matter, our next command will insert new tape 
			pr_err("%s:%u: section: %d, error: ERR_NO_TAPE\n", __func__, __LINE__, sec->idx);
			/* No tape present */
			// We need to wake up ioctl thread if it issued this command
			if (curr_req->is_ioctl)
			{
				struct lst_node *curr_cmd_node = 
					list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);
				list_del(&curr_cmd_node->lst_link);
				kfree(curr_cmd_node);

				sec->ioctl_cmd_done = true;
				sec->ioctl_status = TAPEDEV_SECT_STATUS_ERR_NO_TAPE;
				wake_up(&sec->ioctl_eject_wait_q);
				sec->status = -err;
				return -err;
			}
			break;
			// else
			// {
			// 	// Otherwise we ignore this error, since when handling request we 
			// 	// always firstly eject tape even if there is no tape inside.
			// 	uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_TAKE_TAPE, curr_req->tape_nbr, NO_ARG);
			// 	curr_req->cmd = cmd;
			// }
			// return -1;
		case TAPEDEV_SECT_STATUS_ERR_RESET:

			pr_err("%s:%u: section: %d, error: ERR_RESET\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_RESET;
			break;

		case TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO:

			pr_err("%s:%u: section: %d, error: ERR_INVALID_TAPE_NO\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO;
			break;

		case TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS:

			pr_err("%s:%u: section: %d, error: ERR_INVALID_FFWD_POS\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS;
			break;

		case TAPEDEV_SECT_STATUS_ERR_READ_PAST_END:

			pr_err("%s:%u: section: %d, error: ERR_READ_PAST_END\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_READ_PAST_END;
			break;

		case TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END:

			pr_err("%s:%u: section: %d, error: ERR_WRITE_PAST_END\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END;
			break;

		case TAPEDEV_SECT_STATUS_ERR_IO:

			pr_err("%s:%u: section: %d, error: ERR_IO\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_IO;
			break;

		case TAPEDEV_SECT_STATUS_ERR_PGTABLE:

			pr_err("%s:%u: section: %d, error: ERR_PGTABLE\n", __func__, __LINE__, sec->idx);
			err = TAPEDEV_SECT_STATUS_ERR_PGTABLE;
			break;

		default:

			pr_err("%s:%u: section: %d, error: Unknown\n", __func__, __LINE__, sec->idx);

			break;
	}
	sec->status = -err;

	// If we encountered an error we end current request with error
	_end_request(sec, BLK_STS_IOERR);

	return -err;
}

static int _rewind_pgt(uint64_t *pgt_buf, int start_idx, int *nents)
{
	if (start_idx > *nents)
		return -1;

	for (int i = start_idx; i < *nents; i++)
	{
		pgt_buf[i - start_idx] = pgt_buf[i];
	}

	// first start_idx elements will be discarded, since we move all of the elements
	// starting with element at start_idx to the LEFT
	*nents = *nents - start_idx;

	pr_warn("%s:%u: REWIND: start_idx: %d, nents_left: %d \n", __func__, __LINE__, start_idx, *nents);
	return 0;
}

static void _handle_read_write(struct req_state *curr_req, struct section *sec)
{
	if (curr_req->sg_idx >= curr_req->nents)
	{
		pr_warn("%s:%u: section: %u completed request \n", __func__, __LINE__, sec->idx);
		curr_req->completed = true;
	}
	else if (curr_req->prev_tape_nbr != curr_req->tape_nbr)
	{
		// This case means that we've just ended read/write, and there is no
		// more space on the tape, so we must change it
		pr_warn("%s:%u: section: %u needs to insert new tape \n", __func__, __LINE__, sec->idx);
		curr_req->prev_tape_nbr = curr_req->tape_nbr;
		uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_EJECT_TAPE, NO_ARG, NO_ARG);
		curr_req->cmd = cmd;
	}
	else
	{
		uint64_t *pgt_buf = sec->cpu_dma_buf;
		uint32_t blocks_in_cmd = 0;

		if (curr_req->rewind_state.do_rewind)
		{
			// When creating READ/WRITE cmd we have only 23-31 bits for 
			// offset in page table counted in BLOCKS, so we can at most 
			// hold offset of 511 blocks, therefore, if we exceed this 
			// number our stored value will be truncated and we will get 
			// incorrect block offset passed to our device.
			// This will happen often since request can span LOADS of blocks.
			// Thanks to rewinding pgt_buf our device block offset will 
			// always be 0.

			if (curr_req->rewind_state.overflow_blocks != 0)
			{
				uint64_t old_addr = 
					(curr_req->rewind_state.old_pgt_entry >> 32) << 9;
				uint64_t new_addr = old_addr + curr_req->rewind_state.inserted_blocks * ((uint64_t)sec->blk_size);
				new_addr = new_addr >> 9;
				uint64_t new_pgt_entry = new_addr;
				new_pgt_entry = new_pgt_entry << 32;
				new_pgt_entry = new_pgt_entry | curr_req->rewind_state.overflow_blocks;

				pgt_buf[curr_req->rewind_state.idx] = new_pgt_entry;
			}
			_rewind_pgt(pgt_buf, curr_req->rewind_state.idx, &(curr_req->nents));

			curr_req->rewind_state = NO_REWIND;
			dma_wmb();
		}

		for (int i = 0; i < curr_req->nents; i++)
		{
			// Nbr of blocks in 64bit pgt_buf elem is at low 32 bits
			uint32_t n_blocks = (uint32_t)(pgt_buf[i] & 0xffffffffULL);

			curr_req->sg_idx = i;
			if (n_blocks == 0)
			{
				pr_err("%s:%u: section: %u got 0 blocks in pgt_buf, i= %d, nents= %u \n", __func__, __LINE__, sec->idx, i, curr_req->nents);
			}

			blocks_in_cmd += n_blocks;

			if (blocks_in_cmd >= curr_req->left_blocks_in_tape)
			{
				uint64_t overflow_blocks = 
					blocks_in_cmd - curr_req->left_blocks_in_tape;
				uint64_t inserted_blocks = n_blocks - overflow_blocks;
				blocks_in_cmd = curr_req->left_blocks_in_tape;

				uint32_t cmd = create_tapedev_cmd(
					curr_req->is_write ? TAPEDEV_CMD_WRITE : TAPEDEV_CMD_READ, 
					curr_req->device_pgt_offset, 
					blocks_in_cmd
				);
				curr_req->cmd = cmd;
				curr_req->left_blocks_in_tape = curr_req->total_blocks_in_tape;
				curr_req->tape_nbr++;
				curr_req->start_block_within_tape = 0;
				// Thanks to rewinding the pgt_buf we always start at 0 block
				// offset in device
				curr_req->device_pgt_offset = 0;
				curr_req->rewind_state.do_rewind = true;
				curr_req->rewind_state.overflow_blocks = overflow_blocks;
				curr_req->rewind_state.inserted_blocks = inserted_blocks;
				if (overflow_blocks == 0)
				{
					// If this is last block to read, i+1 == nents thus
					// sg_idx == nents so we will go into completed=true
					// branch and there will be no rewind.
					// If this is not last block, we will simply rewind our
					// pgt_buf so that i + 1 entry is at 0 position
					curr_req->sg_idx = i + 1;
					curr_req->rewind_state.idx = i + 1;
				}
				else
				{
					// We still have overflow_blocks to read from pgt_buf[i]
					// thus we must stay at this pgt_buf entry, but we must
					// forward dma_addr by inserted_blocks * blk_size, so 
					// that our device can start reading at 0 block offset.
					// This forwarding pgt_buf will be done in rewind branch
					curr_req->sg_idx = i;
					curr_req->rewind_state.idx = i;
					curr_req->rewind_state.old_pgt_entry = pgt_buf[i];
				}

				blocks_in_cmd = 0;
				break;
			}
		}

		if (blocks_in_cmd != 0)			
		{
			// This is LAST command for this request
			uint32_t cmd = create_tapedev_cmd(
				curr_req->is_write ? TAPEDEV_CMD_WRITE : TAPEDEV_CMD_READ, 
				curr_req->device_pgt_offset, 
				blocks_in_cmd
			);
			// Thanks to this assignment we will go into completed branch once the
			// command ends
			curr_req->sg_idx = curr_req->nents;
			curr_req->cmd = cmd;
		}
	}
}

// To use this function you MUST FIRST ACQUIRE LOCK
int __handle_section_done(uint32_t section_status, struct section *sec)
{
	int err = 0;

	if (section_status != TAPEDEV_SECT_STATUS_DONE)
	{
		pr_err("%s:%u: section_status (%d) is not equal to TAPEDEV_SECT_STATUS_DONE even though it should be \n", __func__, __LINE__, section_status);
		err = -1;
		goto ret;
	}

	sec->status = TAPEDEV_SECT_STATUS_DONE;
	struct req_state *curr_req; 

	// get_curr_req guarantees that curr_req != NULL
	if (get_curr_req(&curr_req, sec))
	{
		err = -1;
		goto ret;
	}

	uint32_t tape_nbr = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	uint32_t curr_cmd_type = GET_CMD_TYPE(curr_req->cmd);
	uint32_t curr_cmd_body = GET_CMD_BODY(curr_req->cmd);

	// !!!!! SENDING COMMANDS is done in handle_next_cmd, here we only set correct
	// values and cmds
	switch(curr_cmd_type)
	{
		case TAPEDEV_CMD_EJECT_TAPE:
		{
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_EJECT_TAPE, section: %u \n", __func__, __LINE__, sec->idx);

			sec->curr_tape = 0;
			if (curr_req->is_ioctl)
			{
				sec->ioctl_cmd_done = true;
				sec->ioctl_status = IOCTL_STATUS_OK;
				wake_up(&sec->ioctl_eject_wait_q);
			}
			else
			{
				// If just completed cmd was eject tape, we must then INSERT TAPE
				uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_TAKE_TAPE, curr_req->tape_nbr, NO_ARG);
				curr_req->cmd = cmd;
			}
			break;
		}
		case TAPEDEV_CMD_TAKE_TAPE:
		{
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_TAKE_TAPE, inserted tape: %u, section: %u \n", __func__, __LINE__, curr_req->tape_nbr, sec->idx);
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != curr_req->tape_nbr)
			{
				pr_err("%s:%u: take_tape was done but inserted tape: '%u' is different from requested tape: '%u' \n", __func__, __LINE__, tape, curr_cmd_body);
				err = -1;
				goto ret;
			}
			sec->curr_tape = tape;
			// After we've inserted tape we must rewind it 
			uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_REWIND, NO_ARG, NO_ARG);
			curr_req->cmd = cmd;

			break;
		}
		case TAPEDEV_CMD_REWIND:
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_REWIND, tape: %u rewinded, section: %u \n", __func__, __LINE__, tape_nbr, sec->idx);

			// After rewind we must fast forward to correct sector
			if (curr_req->start_block_within_tape != 0)
			{
				uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_FAST_FWD, curr_req->start_block_within_tape, NO_ARG);
				curr_req->cmd = cmd;
				break;
			}
			else
			{
				pr_warn("%s:%u: Skipping forwarding by 0 blocks, going to read/write \n", __func__, __LINE__);
				_handle_read_write(curr_req, sec);
			}
			break;
		case TAPEDEV_CMD_FAST_FWD:
		case TAPEDEV_CMD_READ:
		case TAPEDEV_CMD_WRITE:
			_handle_read_write(curr_req, sec);
			break;
		default:
			pr_err("%s:%u: got unsupported cmd: '%u' \n", __func__, __LINE__, curr_cmd_type);
			err = -2;
			goto ret;
	}

	end_req_if_completed(sec, curr_req);

ret:
	return err;
}

// To use this function you MUST FIRST ACQUIRE LOCK
void __handle_next_cmd(struct section *sec)
{
	// If we have request we do request, it has priority over ioctl commands 
	// (both of these checks should have ALWAYS the same value)
	if (sec->req != NULL && !IS_NULL_REQ_STATE(sec->req_state))
	{
		sec->req_state.is_being_executed = true;
		section_send_cmd(sec->req_state.cmd, sec);
	}
	else if (!list_empty(&sec->ioctl_cmd_queue_head))
	{
		// If there is no request we can do ioctl command, after completing one 
		// request we ALWAYS will schedule IOCTL request if present, in that way we 
		// won't starve those requests.
		pr_warn("%s:%u: next cmd is IOCTL for section: %u\n", __func__, __LINE__, sec->idx);
		struct lst_node *node = list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);
		node->cmd.is_being_executed = true;
		section_send_cmd(node->cmd.cmd, sec);
	}
	else if ((sec->req == NULL && !IS_NULL_REQ_STATE(sec->req_state)) 
		|| (sec->req != NULL && IS_NULL_REQ_STATE(sec->req_state)))
	{
		pr_err("%s:%u: INVALID STATE in section: %u, sec->req and sec->req_state have different states \n", __func__, __LINE__, sec->idx);
		return;
	}
	else
	{
		pr_warn("%s:%u: No next cmd present, section: %u\n", __func__, __LINE__, sec->idx);
	}
	// No next cmd present
}

void clear_sec_done_intrpt(struct section* sec)
{
	tapedev_iow(sec->private_data, TAPEDEV_IRQ_CLEAR_ADDR, 
		(1 << TAPEDEV_IRQ_SECT_X_DONE(sec->idx))
	);
}

void clear_sec_err_intrpt(struct section* sec)
{
	tapedev_iow(sec->private_data, TAPEDEV_IRQ_CLEAR_ADDR, 
		(1 << TAPEDEV_IRQ_SECT_X_ERROR(sec->idx))
	);
}

void end_req_if_completed(struct section *sec, struct req_state *curr_req)
{
	if (curr_req->is_ioctl)
	{
		struct lst_node *node = list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);

		list_del(&node->lst_link);
		kfree(node);
	}
	else if (curr_req->completed)
	{
		_end_request(sec, BLK_STS_OK);
	}
	// if nothing completed we do nothing
}

/*
	Must be used with already acquired sec->lock
*/
void _end_request(struct section *sec, blk_status_t status)
{
	if (sec->req != NULL)
	{
		struct tapedev_device *dev = sec->private_data;
		dma_unmap_sg(
			&dev->pdev->dev, 
			sec->sg_arr, 
			sec->req_state.original_nents, 
			sec->req_state.data_direction
		);
		blk_mq_end_request(sec->req, status);
		sec->req = NULL;
		sec->req_state = NULL_REQ_STATE;
	}
	else
	{
		pr_err("%s:%u: doing blk_mq_end_request BUT REQ IS NULL\n", __func__, __LINE__);
	}
}