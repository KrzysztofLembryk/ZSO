#include "tapedev_irq.h"
#include "linux/blk_types.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/wait.h"
#include "tapedev.h"
#include "tapedev_defs.h"
#include "tapedev_iow_ior.h"


int __handle_section_error(uint32_t section_status, struct section *sec);
int __handle_section_done(uint32_t section_status, struct section *sec);
int __handle_ejection_cmds_if_present(struct section *sec);
void __handle_next_cmd(struct section *sec);
void clear_sec_done_intrpt(struct section* sec);
void clear_sec_err_intrpt(struct section* sec);

int handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec)
{
	pr_warn("%s:%u: handling intrpt for section %u \n", __func__, __LINE__, sec->idx);
	int err = 0;

	// -------- CRITICAL SECTION Start --------
	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

    if (section_status == TAPEDEV_SECT_STATUS_WORKING)
    {
		pr_warn("%s:%u: section_status == TAPEDEV_SECT_STATUS_WORKING, section_done: %u\n", __func__, __LINE__, section_done);
        sec->status = TAPEDEV_SECT_STATUS_WORKING;
        goto release_lock;
    }
	uint32_t nodes_in_lst = list_count_nodes(&sec->cmd_queue_head);
	pr_warn("%s:%u: nodes in cmd qeueu: %u \n", __func__, __LINE__, nodes_in_lst);

	pr_warn("%s:%u: Starting to iterate over cmd queue \n", __func__, __LINE__);

	struct lst_node *curr;
	list_for_each_entry(curr, &sec->cmd_queue_head, lst_link)
	{
		pr_warn("%s:%u: cmd: %u \n", __func__, __LINE__, GET_CMD_TYPE(curr->cmd.cmd));
	}

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

// To use this function you MUST FIRST ACQUIRE LOCK
int __handle_section_error(uint32_t section_status, struct section *sec)
{
	// If we got error we probably should ABORT ALL WAITING COMMANDS
	int err = 0x8A;


	struct lst_node *curr_cmd_node = list_first_entry(&sec->cmd_queue_head, struct lst_node, lst_link);
	struct section_cmd curr_cmd = curr_cmd_node->cmd;
	// pr_err("%s:%u: section: %d, we got error for current command: %u\n", __func__, __LINE__, sec->idx, curr_cmd.cmd);
	// TODO: IDK how we should handle these things yet
	switch (section_status)
	{
		case TAPEDEV_SECT_STATUS_ERR_INVALID_CMD:
			pr_err("%s:%u: section: %d, error: ERR_INVALID_CMD\n", __func__, __LINE__, sec->idx);

			/* Invalid command received */
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_CMD;
			break;

		case TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE:

			pr_err("%s:%u: section: %d, error: ERR_TAPE_ACTIVE\n", __func__, __LINE__, sec->idx);
			/* Tape is currently active/busy */
			err = TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE;
			break;

		case TAPEDEV_SECT_STATUS_ERR_NO_TAPE:

			// TODO: if our request ejects tape and there is NO TAPE, we should 
			// kinda ignore this error and continue with execution, since it doesn't
			// matter, our next command will insert new tape 
			pr_err("%s:%u: section: %d, error: ERR_NO_TAPE\n", __func__, __LINE__, sec->idx);
			/* No tape present */
			// We need to wake up ioctl thread if it issued this command
			list_del(&curr_cmd_node->lst_link);
			kfree(curr_cmd_node);
			if (curr_cmd.is_ioctl)
			{
				sec->ioctl_cmd_done = true;
				sec->ioctl_status = TAPEDEV_SECT_STATUS_ERR_NO_TAPE;
				wake_up(&sec->ioctl_eject_wait_q);
			}
			// otherwise we ignore this error, request that ejected tape will in next
			// step insert new one

			err = TAPEDEV_SECT_STATUS_ERR_NO_TAPE;
			break;

		case TAPEDEV_SECT_STATUS_ERR_RESET:

			pr_err("%s:%u: section: %d, error: ERR_RESET\n", __func__, __LINE__, sec->idx);
			/* Device was reset */
			err = TAPEDEV_SECT_STATUS_ERR_RESET;
			break;

		case TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO:

			pr_err("%s:%u: section: %d, error: ERR_INVALID_TAPE_NO\n", __func__, __LINE__, sec->idx);
			/* Invalid tape number specified */
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO;
			break;

		case TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS:

			pr_err("%s:%u: section: %d, error: ERR_INVALID_FFWD_POS\n", __func__, __LINE__, sec->idx);
			/* Invalid fast-forward position */
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS;
			break;

		case TAPEDEV_SECT_STATUS_ERR_READ_PAST_END:

			pr_err("%s:%u: section: %d, error: ERR_READ_PAST_END\n", __func__, __LINE__, sec->idx);
			/* Attempted to read past end of tape */
			err = TAPEDEV_SECT_STATUS_ERR_READ_PAST_END;
			break;

		case TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END:

			pr_err("%s:%u: section: %d, error: ERR_WRITE_PAST_END\n", __func__, __LINE__, sec->idx);
			/* Attempted to write past end of tape */
			err = TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END;
			break;

		case TAPEDEV_SECT_STATUS_ERR_IO:

			pr_err("%s:%u: section: %d, error: ERR_IO\n", __func__, __LINE__, sec->idx);
			/* General I/O error */
			err = TAPEDEV_SECT_STATUS_ERR_IO;
			break;

		case TAPEDEV_SECT_STATUS_ERR_PGTABLE:

			pr_err("%s:%u: section: %d, error: ERR_PGTABLE\n", __func__, __LINE__, sec->idx);
			/* Page table error */
			err = TAPEDEV_SECT_STATUS_ERR_PGTABLE;
			break;

		default:

			pr_err("%s:%u: section: %d, error: Unknown\n", __func__, __LINE__, sec->idx);
			/* Unknown status code */

			break;
	}
	sec->status = -err;

	if (curr_cmd.is_ioctl)
		return -err;
	// This allows us below flow:
	// - at the start of the request we ALWAYS eject the tape, because sec->curr_tape
	// 	might show the tape we want, BUT there might be an ioctl command running 
	// 	and it might end while we are creating our request commands, and we will get
	// 	situation where we assumed we have a tape, but when our command is being 
	// 	computed there is no tape inserted
	// - so we always EJECT tape, and only after that we insert our tape
	if (section_status == TAPEDEV_SECT_STATUS_ERR_NO_TAPE)
	{
		pr_err("%s:%u: request wanted to EJECT TAPE, but there was no tape, if next command inserts tape it's fine, returning \n", __func__, __LINE__);
		return -err;
	}

	pr_err("%s:%u: ABORTING rest commands of current request\n", __func__, __LINE__);
	while (!list_empty(&sec->cmd_queue_head))
	{
		struct lst_node *node = list_first_entry(&sec->cmd_queue_head, struct lst_node, lst_link);

		// Once we got to the ioctl commands we stop removing from list, since it
		// means we removed whole request
		if (node->cmd.is_ioctl)
		{
			blk_mq_end_request(sec->req, BLK_STS_IOERR);
			break;
		}

		list_del(&node->lst_link);
		kfree(node);
	}

	if (list_empty(&sec->cmd_queue_head))
		blk_mq_end_request(sec->req, BLK_STS_IOERR);

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
	if (list_empty(&sec->cmd_queue_head))
	{
		pr_err("%s:%u: cmd queue is EMPTY even though section just completed command \n", __func__, __LINE__);
		err = -1;
		goto ret;
	}


	sec->status = TAPEDEV_SECT_STATUS_DONE;
	struct lst_node *node = list_first_entry(&sec->cmd_queue_head, struct lst_node, lst_link);
	struct section_cmd curr_cmd = node->cmd;
	
	list_del(&node->lst_link);
	// After removing from queue list we must free memory of the node, we no longer
	// need it here, just information aobut curr cmd is sufficient
	kfree(node);

	uint32_t curr_cmd_type = GET_CMD_TYPE(curr_cmd.cmd);
	uint32_t curr_cmd_body = GET_CMD_BODY(curr_cmd.cmd);

	pr_warn("%s:%u: CMD: '%u' done \n", __func__, __LINE__, curr_cmd_type);

	switch(curr_cmd_type)
	{
		case TAPEDEV_CMD_TAKE_TAPE:
		{
			pr_warn("%s:%u: cmd DONE: TAPEDEV_CMD_TAKE_TAPE \n", __func__, __LINE__);
			curr_cmd_body = curr_cmd_body >> 8;
			sec->current_tape = curr_cmd_body;
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != sec->current_tape)
			{
				pr_err("%s:%u: take_tape was done but inserted tape: '%u' is different from requested tape: '%u' \n", __func__, __LINE__, tape, sec->current_tape);
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
			// TODO: add constant NO_TAPE instead of magic number 0
			sec->current_tape = NO_TAPE;
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != NO_TAPE)
			{
				pr_err("something went wrong, eject_tape was done but  %u tape is still inserted\n", tape);
				err = -1;
				goto ret;
			}

			// If current command was issued by ioctl we only wake up ioctl threads
			if (curr_cmd.is_ioctl)
				wake_up(&sec->ioctl_eject_wait_q);

			// If current command wasn't issued by ioctl, noone is waiting on queue
			// so we don't need to do anything, tape was ejected, that's all we 
			// wanted, we can continue with next command
			break;
		}
		case TAPEDEV_CMD_REWIND:
			pr_warn("%s:%u: tape: %u rewinded\n", __func__, __LINE__, sec->current_tape);
			break;
		case TAPEDEV_CMD_FAST_FWD:
			pr_warn("%s:%u: tape: %u forwarded by %u blocks\n", __func__, __LINE__, sec->current_tape, curr_cmd_body >> 8);
			break;
		case TAPEDEV_CMD_READ:
			// In read/write we probably will need to do something with checking how
			// many bytes or sth was read/done etc
			pr_warn("%s:%u: tape: %u has been read\n", __func__, __LINE__, sec->current_tape);
			break;
		case TAPEDEV_CMD_WRITE:
			pr_warn("%s:%u: tape: %u has been written\n", __func__, __LINE__, sec->current_tape);
			break;	
		default:
			pr_err("%s:%u: got unsupported cmd: '%u' \n", __func__, __LINE__, curr_cmd_type);
			err = -2;
			goto ret;
	}

	// After handling current command we check if list empty 
	if (list_empty(&sec->cmd_queue_head))
	{
		// If there is no next command, we check if just ended command is ioctl,
		// if it is we do nothing, otherwise we inform that request has ended 
		// successfullynow we use queue of cmds,
		if (!curr_cmd.is_ioctl)
			blk_mq_end_request(sec->req, BLK_STS_OK);
	}
	else
	{
		// If list is not empty, we must check if next command is ioctl, if it is
		// it means that just ended command was the last one in our request so we
		// must end this request
		struct lst_node *next_node = list_first_entry(&sec->cmd_queue_head, struct lst_node, lst_link);

		// next and curr cmd should NEVER BOTH BE IOCTL, but still better to check it
		if (next_node->cmd.is_ioctl && !curr_cmd.is_ioctl)
			blk_mq_end_request(sec->req, BLK_STS_OK);
	}

ret:
	return err;
}

// To use this function you MUST FIRST ACQUIRE LOCK
int __handle_ejection_cmds_if_present(struct section *sec)
{
	if (sec->ejection_cmds)
	{
		if (sec->current_tape)
		{
			// After handling curr command tape is inserted, so we send cmd EJECT
			// and release lock.
			sec->curr_cmd = (struct section_cmd) {
				.cmd = TAPEDEV_CMD_EJECT_TAPE,
				.is_ioctl = true
			};

			section_send_cmd(sec->curr_cmd.cmd, sec);

			return 1;
		}
		else
		{
			// After handling curr command there is no tape inserted so we just wake
			// wake up ioctl threads.
			wake_up(&sec->cmd_wait_q);
		}
	}
	return 0;
}

// To use this function you MUST FIRST ACQUIRE LOCK
void __handle_next_cmd(struct section *sec)
{
	pr_warn("%s:%u: Will schedule next cmd for section: %u\n", __func__, __LINE__, sec->idx);
	if (list_empty(&sec->cmd_queue_head))
	{
		pr_warn("%s:%u: cmd queue EMPTY\n", __func__, __LINE__);
		return;
	}
	// We get first command in queue, but not remove it from the list.
	// Removal will only happen once command is done.
	struct lst_node *node = list_first_entry(&sec->cmd_queue_head, struct lst_node, lst_link);

	pr_warn("%s:%u: cmd that will be scheduled is: %u\n", __func__, __LINE__, GET_CMD_TYPE(node->cmd.cmd));
	section_send_cmd(node->cmd.cmd, sec);

	return; 
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