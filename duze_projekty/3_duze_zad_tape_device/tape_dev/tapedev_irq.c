#include "tapedev_irq.h"
#include "linux/blk_types.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/wait.h"
#include "tapedev.h"
#include "tapedev_defs.h"
#include "tapedev_iow_ior.h"


void end_request(struct section *sec, blk_status_t status);
int __handle_section_error(uint32_t section_status, struct section *sec);
int __handle_section_done(uint32_t section_status, struct section *sec);
void __handle_next_cmd(struct section *sec);
void __abort_rest_of_req_cmds(struct section *sec);
void __end_req_if_completed(struct section *sec, struct req_state *curr_cmd);
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
		pr_err("%s:%u: section_error \n", __func__, __LINE__);
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
			// otherwise we ignore this error, request that ejected tape will in next
			// step insert new one

			break;

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
	end_request(sec, BLK_STS_IOERR);

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
	if (list_empty(&sec->ioctl_cmd_queue_head))
	{
		pr_err("%s:%u: cmd queue is EMPTY even though section just completed command \n", __func__, __LINE__);
		err = -1;
		goto ret;
	}


	sec->status = TAPEDEV_SECT_STATUS_DONE;
	struct lst_node *node = list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);
	struct req_state curr_cmd = node->cmd;
	
	list_del(&node->lst_link);
	// After removing from queue list we must free memory of the node, we no longer
	// need it here, just information aobut curr cmd is sufficient
	kfree(node);

	uint32_t tape_nbr = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	uint32_t curr_cmd_type = GET_CMD_TYPE(curr_cmd.cmd);
	uint32_t curr_cmd_body = GET_CMD_BODY(curr_cmd.cmd);

	switch(curr_cmd_type)
	{
		case TAPEDEV_CMD_TAKE_TAPE:
		{
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_TAKE_TAPE \n", __func__, __LINE__);
			curr_cmd_body = curr_cmd_body >> 8;
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != curr_cmd_body)
			{
				pr_err("%s:%u: take_tape was done but inserted tape: '%u' is different from requested tape: '%u' \n", __func__, __LINE__, tape, curr_cmd_body);
				// TODO: rework errors, add INTERNAL_ERROR or sth and return it here
				// instead of -1
				err = -1;
				goto ret;
			}

			break;
		}
		case TAPEDEV_CMD_EJECT_TAPE:
		{
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_EJECT_TAPE \n", __func__, __LINE__);
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != NO_TAPE)
			{
				pr_err("something went wrong, eject_tape was done but  %u tape is still inserted\n", tape);
				err = -1;
				goto ret;
			}

			// If current command was issued by ioctl we only wake up ioctl threads
			if (curr_cmd.is_ioctl)
			{
				sec->ioctl_cmd_done = true;
				wake_up(&sec->ioctl_eject_wait_q);
			}

			// If current command wasn't issued by ioctl, noone is waiting on queue
			// so we don't need to do anything, tape was ejected, that's all we 
			// wanted, we can continue with next command
			break;
		}
		case TAPEDEV_CMD_REWIND:
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_REWIND, tape: %u rewinded \n", __func__, __LINE__, tape_nbr);
			break;
		case TAPEDEV_CMD_FAST_FWD:
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_FAST_FWD. tape: %u forwarded by %u blocks\n", __func__, __LINE__, tape_nbr, curr_cmd_body >> 8);
			break;
		case TAPEDEV_CMD_READ:
			// In read/write we probably will need to do something with checking how
			// many bytes or sth was read/done etc
			pr_warn("%s:%u: cmd DONE TAPEDEV_CMD_READ, tape: %u has been read\n", __func__, __LINE__, tape_nbr);
			break;
		case TAPEDEV_CMD_WRITE:
			pr_warn("%s:%u: cmd DONE TAPEDEV_CMD_WRITE, tape: %u has been written\n", __func__, __LINE__, tape_nbr);
			break;	
		default:
			pr_err("%s:%u: got unsupported cmd: '%u' \n", __func__, __LINE__, curr_cmd_type);
			err = -2;
			goto ret;
	}

	__end_req_if_completed(sec, &curr_cmd);

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

void __abort_rest_of_req_cmds(struct section *sec)
{
	pr_err("%s:%u: ABORTING rest commands of current request\n", __func__, __LINE__);
	while (!list_empty(&sec->ioctl_cmd_queue_head))
	{
		struct lst_node *node = list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);

		// Once we got to the ioctl commands we stop removing from list, since it
		// means we removed whole request
		if (node->cmd.is_ioctl)
		{
			end_request(sec, BLK_STS_IOERR);
			break;
		}

		list_del(&node->lst_link);
		kfree(node);
	}

	sec->req = NULL;
}

void __end_req_if_completed(struct section *sec, struct req_state *curr_cmd)
{

	// After handling current command we check if list empty 
	if (list_empty(&sec->ioctl_cmd_queue_head))
	{
		pr_warn("%s:%u: After handling current command cmd queue is EMPTY\n", __func__, __LINE__);
		// If there is no next command, we check if just ended command is ioctl,
		// if it is we do nothing, otherwise we inform that request has ended 
		// successfullynow we use queue of cmds,
		if (!curr_cmd->is_ioctl)
			end_request(sec, BLK_STS_OK);
	
	}
	else
	{
		// If list is not empty, we must check if next command is ioctl, if it is
		// it means that just ended command was the last one in our request so we
		// must end this request
		struct lst_node *next_node = list_first_entry(&sec->ioctl_cmd_queue_head, struct lst_node, lst_link);

		// next and curr cmd should NEVER BOTH BE IOCTL, but still better to check it
		if (next_node->cmd.is_ioctl && !curr_cmd->is_ioctl)
			end_request(sec, BLK_STS_OK);
	}
}

/*
	Must be used with already acquired sec->lock
*/
void end_request(struct section *sec, blk_status_t status)
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