#ifndef TAPEDEV_IRQ_H
#define TAPEDEV_IRQ_H

#include "tapedev_defs.h"

int handle_section_interrupt(uint32_t section_done, uint32_t section_error, uint32_t section_status, struct section *sec);

#endif // TAPEDEV_IRQ_H