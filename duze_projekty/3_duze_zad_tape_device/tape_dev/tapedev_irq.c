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

	for (int sec_id = 0; sec_id < num_sections; sec_id++)
	{
		struct section *sec = dev->sections[sec_id];
		
		// section_done might have big value since we get exact bit that was set,
		// thus we just check if value is greater than 0, if yes we section is done 

		section_done = (ir_status & (1 << TAPEDEV_IRQ_SECT_X_DONE(sec_id))) > 0;
		section_error = (ir_status & (1 << TAPEDEV_IRQ_SECT_X_ERROR(sec_id))) > 0;
		section_status = section_read_from(TAPEDEV_SECT_STATUS_ADDR, sec);

		int err = 0;
		if (section_done || section_error)
		{
			err = _handle_section_interrupt(
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
		// if section currently working we do nothing
	}
	return 0;
}


int _handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec)
{
	pr_warn("%s:%u: handling intrpt for section %u \n", __func__, __LINE__, sec->idx);
	int err = 0;

	// -------- CRITICAL SECTION Start --------
	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

    if (section_status == TAPEDEV_SECT_STATUS_WORKING)
    {
        sec->status = TAPEDEV_SECT_STATUS_WORKING;
        goto release_lock;
    }

	// uint32_t nodes_in_lst = list_count_nodes(&sec->ioctl_cmd_queue_head);

	if (section_error)
	{
		clear_sec_err_intrpt(sec);
		// After getting error from CURR_CMD we need to check if there are any eject 
		// requests or start next_cmd 
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
	else // section IDLE ???
	{
		// TODO: SOMETHING IS WRONG with section 1, section 0 completes but not section 1
		sec->status = TAPEDEV_SECT_STATUS_IDLE;
	}

	// After handling curr command if it was DONE, we schedule next command in 
	// cmd_queue if present 
	// If we got ERROR we probably should ABORT all next commands (apart from ioctl?)
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
			else
			{
				// Otherwise we ignore this error, since when handling request we 
				// always firstly eject tape even if there is no tape inside.
				uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_TAKE_TAPE, curr_req->tape_nbr, NO_ARG);
				curr_req->cmd = cmd;
			}
			return -1;
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

// To use this function you MUST FIRST ACQUIRE LOCK
int __handle_section_done(uint32_t section_status, struct section *sec)
{
	int err = 0;
	// Probably it works like this: when error check status, if section_done we don't
	// need to check status
	// TAPEDEV_IRQ_SECT_n_DONE - Section finished a command, if no error we probably
	// 	can safely assume that everything is OK.
	// TAPEDEV_IRQ_SECT_n_ERROR - Section error, check status.

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
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_EJECT_TAPE \n", __func__, __LINE__);

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
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_TAKE_TAPE, inserted tape: %u \n", __func__, __LINE__, curr_req->tape_nbr);
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != curr_req->tape_nbr)
			{
				pr_err("%s:%u: take_tape was done but inserted tape: '%u' is different from requested tape: '%u' \n", __func__, __LINE__, tape, curr_cmd_body);
				// TODO: rework errors, add INTERNAL_ERROR or sth and return it here
				// instead of -1
				err = -1;
				goto ret;
			}

			// After we've inserted tape we must rewind it 
			uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_REWIND, NO_ARG, NO_ARG);
			curr_req->cmd = cmd;

			break;
		}
		case TAPEDEV_CMD_REWIND:
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_REWIND, tape: %u rewinded \n", __func__, __LINE__, tape_nbr);

			// After rewind we must fast forward to correct sector
			uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_FAST_FWD, curr_req->start_sector_within_tape, NO_ARG);
			curr_req->cmd = cmd;
			break;
		case TAPEDEV_CMD_FAST_FWD:
		case TAPEDEV_CMD_READ:
		case TAPEDEV_CMD_WRITE:
			if (curr_req->sg_idx >= curr_req->nents)
			{
				curr_req->completed = true;
			}
			else if (curr_req->prev_tape_nbr != curr_req->tape_nbr)
			{
				// This case means that we've just ended read/write, and there is no
				// more space on the tape, so we must change it
				curr_req->prev_tape_nbr = curr_req->tape_nbr;
				uint32_t cmd = create_tapedev_cmd(TAPEDEV_CMD_EJECT_TAPE, NO_ARG, NO_ARG);
				curr_req->cmd = cmd;
			}
			else
			{
				pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_FAST_FWD. tape: %u forwarded by %u blocks\n", __func__, __LINE__, tape_nbr, curr_cmd_body >> 8);

				uint64_t *pgt_buf = sec->cpu_dma_buf;
				uint32_t n_blocks_in_cmd = 0;
				for (int i = curr_req->sg_idx; i < curr_req->nents; i++)
				{
					// nbr of blocks in 64bit pgt_buf elem is at low 32 bits
					uint32_t n_blocks = (uint32_t)(pgt_buf[i] & 0xffffffffULL);
					n_blocks_in_cmd += n_blocks;

					// We should have exactly this number of commands, since when 
					// populating pgt_buf we partitioned it in this way 
					if (n_blocks_in_cmd == curr_req->left_blocks_in_tape)
					{
						uint32_t cmd = create_tapedev_cmd(
							curr_req->is_write ? TAPEDEV_CMD_WRITE : TAPEDEV_CMD_READ, curr_req->sg_idx, 
							n_blocks_in_cmd
						);
						curr_req->sg_idx = i + 1;
						curr_req->cmd = cmd;
						curr_req->left_blocks_in_tape = curr_req->total_blocks_in_tape;
						curr_req->tape_nbr++;
						curr_req->start_sector_within_tape = 0;
						n_blocks_in_cmd = 0;

						break;
					}
					else if (n_blocks_in_cmd > curr_req->left_blocks_in_tape)
					{
						pr_err("%s:%u: n_blocks_in_cmd > curr_req->left_blocks_in_tape, THIS SHOULD NEVER HAPPEN, there is a logic error somewhere\n", __func__, __LINE__);
						_end_request(sec, BLK_STS_IOERR);
						err = -2;
						goto ret;
					}
				}
				if (n_blocks_in_cmd != 0)			
				{
					// we will be sending LAST cmd, no more blocks
					uint32_t cmd = create_tapedev_cmd(
						curr_req->is_write ? TAPEDEV_CMD_WRITE : TAPEDEV_CMD_READ, curr_req->sg_idx, 
						n_blocks_in_cmd
					);
					curr_req->sg_idx = curr_req->nents;
					curr_req->cmd = cmd;
				}
			}
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
	// If we have request we do request (both of these checks should have ALWAYS
	// the same value)
	if (sec->req != NULL && !IS_NULL_REQ_STATE(sec->req_state))
	{
		sec->req_state.is_being_executed = true;
		section_send_cmd(sec->req_state.cmd, sec);
	}
	else if (!list_empty(&sec->ioctl_cmd_queue_head))
	{
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