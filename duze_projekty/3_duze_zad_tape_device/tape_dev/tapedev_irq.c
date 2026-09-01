#include "tapedev_irq.h"
#include "tapedev.h"
#include "tapedev_iow_ior.h"


int __handle_section_error(uint32_t section_status, struct section *sec);
int __handle_section_done(uint32_t section_status, struct section *sec);
int __handle_ejection_cmds_if_present(struct section *sec);
void __handle_next_cmd(struct section *sec);
void clear_sec_done_intrpt(struct section* sec);
void clear_sec_err_intrpt(struct section* sec);

int handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec)
{
	int err = 0;

	// -------- CRITICAL SECTION Start --------
	unsigned long flags;
	spin_lock_irqsave(&sec->lock, flags);

    if (section_status == TAPEDEV_SECT_STATUS_WORKING)
    {
		pr_info("%s:%u: section_status == TAPEDEV_SECT_STATUS_WORKING, section_done: %u\n", __func__, __LINE__, section_done);
        sec->status = TAPEDEV_SECT_STATUS_WORKING;
        goto release_lock;
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
		pr_info("%s:%u:else if (section_done) section_id: %u, done: %u, STATUS: %u\n", __func__, __LINE__, sec->idx, section_done, section_status);
		clear_sec_done_intrpt(sec);

		pr_info("%s:%u: before clearing section_done intrpt section_status == %u\n", __func__, __LINE__, section_status);

		uint32_t status = section_read_from(TAPEDEV_SECT_STATUS_ADDR, sec);

		pr_info("%s:%u: after clearing section_done intrpt section_status == %u\n", __func__, __LINE__, status);

		// TODO: if handle_section done failed something went REAALLY wrong, so we 
		// end execution without next steps ???
		err = __handle_section_done(section_status, sec);
		if (err)
			goto release_lock;
	}
	else // section IDLE ???
	{
		// TODO: SOMETHING IS WRONG with section 1, section 0 completes but not section 1
		if (sec->idx == 1)
		{
			pr_warn("%s:%u: section: %u, is IDLE\n", __func__, __LINE__, sec->idx);
		}
		sec->status = TAPEDEV_SECT_STATUS_IDLE;
	}

	// After handling curr command whether it was DONE or we got ERROR, we check if 
	// there are any IOCTL commands scheduled and if there are we start them, set 
	// them as curr_cmd and end.
	if (__handle_ejection_cmds_if_present(sec))
		goto release_lock;

	// There were no IOCTL ejection_cmds, so we handle next_cmd 
	__handle_next_cmd(sec);

release_lock:
	spin_unlock_irqrestore(&sec->lock, flags);

	return err;
}

// To use this function you MUST FIRST ACQUIRE LOCK
int __handle_section_error(uint32_t section_status, struct section *sec)
{
	// Unknown error
	struct section_cmd curr_cmd = sec->curr_cmd;
	sec->curr_cmd = NO_CMD;
	int err = 0x8A;

	pr_err("%s:%u: section: %d, we got error for current command: %u\n", __func__, __LINE__, sec->idx, curr_cmd.cmd);
	// TODO: IDK how we should handle these things yet
	switch (section_status)
	{
		case TAPEDEV_SECT_STATUS_ERR_INVALID_CMD:
			pr_warn("%s:%u: section: %d, error: ERR_INVALID_CMD\n", __func__, __LINE__, sec->idx);

			/* Invalid command received */
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_CMD;
			break;

		case TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE:

			pr_warn("%s:%u: section: %d, error: ERR_TAPE_ACTIVE\n", __func__, __LINE__, sec->idx);
			/* Tape is currently active/busy */
			err = TAPEDEV_SECT_STATUS_ERR_TAPE_ACTIVE;
			break;

		case TAPEDEV_SECT_STATUS_ERR_NO_TAPE:

			pr_warn("%s:%u: section: %d, error: ERR_NO_TAPE\n", __func__, __LINE__, sec->idx);
			/* No tape present */
			err = TAPEDEV_SECT_STATUS_ERR_NO_TAPE;
			break;

		case TAPEDEV_SECT_STATUS_ERR_RESET:

			pr_warn("%s:%u: section: %d, error: ERR_RESET\n", __func__, __LINE__, sec->idx);
			/* Device was reset */
			err = TAPEDEV_SECT_STATUS_ERR_RESET;
			break;

		case TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO:

			pr_warn("%s:%u: section: %d, error: ERR_INVALID_TAPE_NO\n", __func__, __LINE__, sec->idx);
			/* Invalid tape number specified */
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_TAPE_NO;
			break;

		case TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS:

			pr_warn("%s:%u: section: %d, error: ERR_INVALID_FFWD_POS\n", __func__, __LINE__, sec->idx);
			/* Invalid fast-forward position */
			err = TAPEDEV_SECT_STATUS_ERR_INVALID_FFWD_POS;
			break;

		case TAPEDEV_SECT_STATUS_ERR_READ_PAST_END:

			pr_warn("%s:%u: section: %d, error: ERR_READ_PAST_END\n", __func__, __LINE__, sec->idx);
			/* Attempted to read past end of tape */
			err = TAPEDEV_SECT_STATUS_ERR_READ_PAST_END;
			break;

		case TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END:

			pr_warn("%s:%u: section: %d, error: ERR_WRITE_PAST_END\n", __func__, __LINE__, sec->idx);
			/* Attempted to write past end of tape */
			err = TAPEDEV_SECT_STATUS_ERR_WRITE_PAST_END;
			break;

		case TAPEDEV_SECT_STATUS_ERR_IO:

			pr_warn("%s:%u: section: %d, error: ERR_IO\n", __func__, __LINE__, sec->idx);
			/* General I/O error */
			err = TAPEDEV_SECT_STATUS_ERR_IO;
			break;

		case TAPEDEV_SECT_STATUS_ERR_PGTABLE:

			pr_warn("%s:%u: section: %d, error: ERR_PGTABLE\n", __func__, __LINE__, sec->idx);
			/* Page table error */
			err = TAPEDEV_SECT_STATUS_ERR_PGTABLE;
			break;

		default:

			pr_warn("%s:%u: section: %d, error: Unknown\n", __func__, __LINE__, sec->idx);
			/* Unknown status code */

			break;
	}
	sec->status = -err;
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
	struct section_cmd curr_cmd = sec->curr_cmd;
	sec->curr_cmd = NO_CMD;

	uint32_t curr_cmd_type = GET_CMD_TYPE(curr_cmd.cmd);
	uint32_t curr_cmd_body = GET_CMD_BODY(curr_cmd.cmd);

	switch(curr_cmd_type)
	{
		case TAPEDEV_CMD_TAKE_TAPE:
		{
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

			goto wake_up_request_worker;
		}
		case TAPEDEV_CMD_EJECT_TAPE:
		{
			// TODO: add constant NO_TAPE instead of magic number 0
			sec->current_tape = NO_TAPE;
			uint32_t tape = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, sec); 
	
			if (tape != NO_TAPE)
			{
				pr_err("something went wrong, eject_tape was done but  %u tape is still inserted\n", tape);
				err = -1;
				goto ret;
			}
			// No matter who issued the command, we always wake up ioctl threads
			if (sec->ejection_cmds)
			{
				sec->ejection_cmds = 0;
				wake_up_all(&sec->ioctl_eject_wait_q);
			}
	
			// If current command was issued by ioctl we only wake up ioctl threads
			if (curr_cmd.is_ioctl)
				goto ret;

			// If current command wasn't issued by ioctl we need to also wake up 
			// request handling thread and also check if NEXT command isn't 
			// EJECT_TAPE, if it is we must cancel it since it was issued by ioctl
			// and we've just woken up all ioctl threads
			if (GET_CMD_TYPE(sec->next_cmd.cmd) == TAPEDEV_CMD_EJECT_TAPE)
				sec->next_cmd = NO_CMD;
	
			goto wake_up_request_worker;
		}
		case TAPEDEV_CMD_REWIND:
			pr_info("%s:%u: tape: %u rewinded\n", __func__, __LINE__, sec->current_tape);
			goto wake_up_request_worker;
		case TAPEDEV_CMD_FAST_FWD:
			pr_info("%s:%u: tape: %u forwarded by %u blocks\n", __func__, __LINE__, sec->current_tape, curr_cmd_body);
			goto wake_up_request_worker;
		case TAPEDEV_CMD_READ:
			pr_info("%s:%u: tape: %u has been read\n", __func__, __LINE__, sec->current_tape);
			goto wake_up_request_worker;
		case TAPEDEV_CMD_WRITE:
			pr_info("%s:%u: tape: %u hase been written\n", __func__, __LINE__, sec->current_tape);
			goto wake_up_request_worker;
		default:
			pr_err("%s:%u: got unsupported cmd: '%u' \n", __func__, __LINE__, curr_cmd_type);
			err = -2;
			goto ret;
	}
wake_up_request_worker:
	sec->cmd_done = true;
	wake_up(&sec->cmd_wait_q);
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
	uint32_t next_cmd_type = GET_CMD_TYPE(sec->next_cmd.cmd);

	// If next command is NONE, there is nothing to do
	if (next_cmd_type == TAPEDEV_CMD_NONE)
		return;

	// If next cmd isn't NONE, we set it as current and send it to tapedev
	sec->curr_cmd = sec->next_cmd;
	sec->next_cmd = NO_CMD;

	section_send_cmd(sec->curr_cmd.cmd, sec);

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