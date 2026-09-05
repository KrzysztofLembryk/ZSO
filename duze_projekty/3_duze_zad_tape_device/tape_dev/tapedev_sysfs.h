#ifndef TAPEDEV_SYSFS_H
#define TAPEDEV_SYSFS_H

#include "tapedev_iow_ior.h"
#include "tapedev_defs.h"

// https://medium.com/@emanuele.santini.88/sysfs-in-linux-kernel-a-complete-guide-part-1-c3629470fc84
static ssize_t tape_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct section *s = disk->private_data;
	pr_info("tape_type_show -- section_type: %u\n", s->section_type);
	return sysfs_emit(buf, "%u\n", s->section_type);
}

static ssize_t tapes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct section *s = disk->private_data;
	pr_info("tapes_show -- tapes: %u\n", s->n_tapes);
	return sysfs_emit(buf, "%u\n", s->n_tapes);
}

static ssize_t current_tape_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct section *s = disk->private_data;
	uint32_t tape_nbr = section_read_from(TAPEDEV_SECT_TAPE_NO_ADDR, s); 
	pr_info("current_tape_show -- curr tape: %u\n", tape_nbr);
	return sysfs_emit(buf, "%u\n", tape_nbr);
}

static DEVICE_ATTR_RO(tape_type);
static DEVICE_ATTR_RO(tapes);
static DEVICE_ATTR_RO(current_tape);

static struct attribute *tape_attrs[] = {
 &dev_attr_tape_type.attr,
 &dev_attr_tapes.attr,
 &dev_attr_current_tape.attr,
 NULL,
};

static const struct attribute_group tape_attr_group = {
	.name = "tape",
	.attrs = tape_attrs
};

#endif // TAPEDEV_SYSFS_H