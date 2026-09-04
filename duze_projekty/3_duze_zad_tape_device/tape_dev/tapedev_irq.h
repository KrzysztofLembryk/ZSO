#ifndef TAPEDEV_IRQ_H
#define TAPEDEV_IRQ_H

#include "tapedev_defs.h"

int handle_sections_interrupts(uint32_t ir_status, uint32_t num_sections, struct tapedev_device *dev);

#endif // TAPEDEV_IRQ_H