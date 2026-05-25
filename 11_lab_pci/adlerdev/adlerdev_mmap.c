#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/file.h>
#include <linux/kref.h>
#include <linux/interrupt.h>

#include "adlerdev.h"

#define ADLERDEV_MAX_DEVICES 256
#define ADLERDEV_NUM_BUFFERS 16

#define ADLERDEV_BUF_SIZE	(16*PAGE_SIZE)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Adler device driver");

struct adlerdev_device {
	struct pci_dev *pdev;
	struct cdev cdev;
	int idx;
	struct device *dev;
	void __iomem *bar;
	spinlock_t slock;

	void *data_cpu;
	dma_addr_t data_dma;
	size_t fill_size;
	uint32_t sum;

	bool open;
	bool buffer_pending;
	wait_queue_head_t wq;
};

static dev_t adlerdev_devno;
static struct adlerdev_device *adlerdev_devices[ADLERDEV_MAX_DEVICES];
static DEFINE_MUTEX(adlerdev_devices_lock);
static struct class adlerdev_class = {
	.name = "adlerdev",
};

/* Hardware handling. */

static inline void adlerdev_iow(struct adlerdev_device *dev, uint32_t reg, uint32_t val)
{
	iowrite32(val, dev->bar + reg);
//	printk(KERN_ALERT "adlerdev %03x <- %08x\n", reg, val);
}

static inline uint32_t adlerdev_ior(struct adlerdev_device *dev, uint32_t reg)
{
	uint32_t res = ioread32(dev->bar + reg);
//	printk(KERN_ALERT "adlerdev %03x -> %08x\n", reg, res);
	return res;
}

/* IRQ handler.  */

static irqreturn_t adlerdev_isr(int irq, void *opaque)
{
	struct adlerdev_device *dev = opaque;
	unsigned long flags;
	uint32_t istatus;
	spin_lock_irqsave(&dev->slock, flags);
//	printk(KERN_ALERT "adlerdev isr\n");
	istatus = adlerdev_ior(dev, ADLERDEV_INTR) & adlerdev_ior(dev, ADLERDEV_INTR_ENABLE);
	if (istatus) {
		adlerdev_iow(dev, ADLERDEV_INTR, istatus);
		WARN_ON(!dev->buffer_pending);
		dev->buffer_pending = false;
		dev->sum = adlerdev_ior(dev, ADLERDEV_SUM);
		wake_up(&dev->wq);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
	return IRQ_RETVAL(istatus);
}

/* Main device node handling.  */

static int adlerdev_open(struct inode *inode, struct file *file)
{
	struct adlerdev_device *dev = container_of(inode->i_cdev, struct adlerdev_device, cdev);
	unsigned long flags;
	int res;

	spin_lock_irqsave(&dev->slock, flags);
	if (dev->open) {
		res = -EBUSY;
	} else {
		dev->sum = ADLERDEV_SUM_INIT;
		dev->buffer_pending = false;
		file->private_data = dev;
		dev->open = true;
		res = nonseekable_open(inode, file);
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	return res;
}

static int adlerdev_release(struct inode *inode, struct file *file)
{
	struct adlerdev_device *dev = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);
	while (dev->buffer_pending) {
		spin_unlock_irqrestore(&dev->slock, flags);
		wait_event(dev->wq, !dev->buffer_pending);
		spin_lock_irqsave(&dev->slock, flags);
	}
	dev->open = false;
	spin_unlock_irqrestore(&dev->slock, flags);

	return 0;
}

static ssize_t adlerdev_write(struct file *file, const char __user *buf,
		size_t len, loff_t *off)
{
	struct adlerdev_device *dev = file->private_data;
	unsigned long flags;
	int bytes_to_write;

	if (len != 4)
		return -EINVAL;

	if (copy_from_user(&bytes_to_write, buf, 4))
		return -EFAULT;

	if (bytes_to_write > ADLERDEV_BUF_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&dev->slock, flags);
	while (dev->buffer_pending) {
		spin_unlock_irqrestore(&dev->slock, flags);
		if (wait_event_interruptible(dev->wq, !dev->buffer_pending))
			return -ERESTARTSYS;
		spin_lock_irqsave(&dev->slock, flags);
	}

	dev->buffer_pending = true;
	adlerdev_iow(dev, ADLERDEV_DATA_PTR, dev->data_dma);
	adlerdev_iow(dev, ADLERDEV_SUM, dev->sum);
	adlerdev_iow(dev, ADLERDEV_DATA_SIZE, bytes_to_write);

	spin_unlock_irqrestore(&dev->slock, flags);

	return 4;
}

static ssize_t adlerdev_read(struct file *file, char __user *buf,
		size_t len, loff_t *off)
{
	struct adlerdev_device *dev = file->private_data;
	uint32_t sum;
	unsigned long flags;
	if (len != 4)
		return -EINVAL;
	spin_lock_irqsave(&dev->slock, flags);
	while (dev->buffer_pending) {
		spin_unlock_irqrestore(&dev->slock, flags);
		if (wait_event_interruptible(dev->wq, !dev->buffer_pending))
			return -ERESTARTSYS;
		spin_lock_irqsave(&dev->slock, flags);
	}
	sum = dev->sum;
	spin_unlock_irqrestore(&dev->slock, flags);
	if (copy_to_user(buf, &sum, 4))
		return -EFAULT;
	return 4;
}


static const struct file_operations adlerdev_file_ops = {
	.owner = THIS_MODULE,
	.open = adlerdev_open,
	.release = adlerdev_release,
	.write = adlerdev_write,
	.read = adlerdev_read,
	// TODO
	.mmap = adlerdev_mmap,
};

/* PCI driver.  */

static int adlerdev_probe(struct pci_dev *pdev,
	const struct pci_device_id *pci_id)
{
	int err, i;

	/* Allocate our structure.  */
	struct adlerdev_device *dev = kzalloc(sizeof *dev, GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto out_alloc;
	}
	pci_set_drvdata(pdev, dev);
	dev->pdev = pdev;

	/* Locks etc.  */
	spin_lock_init(&dev->slock);
	init_waitqueue_head(&dev->wq);

	/* Allocate a free index.  */
	mutex_lock(&adlerdev_devices_lock);
	for (i = 0; i < ADLERDEV_MAX_DEVICES; i++)
		if (!adlerdev_devices[i])
			break;
	if (i == ADLERDEV_MAX_DEVICES) {
		err = -ENOSPC; // XXX right?
		mutex_unlock(&adlerdev_devices_lock);
		goto out_slot;
	}
	adlerdev_devices[i] = dev;
	dev->idx = i;
	mutex_unlock(&adlerdev_devices_lock);

	/* Enable hardware resources.  */
	if ((err = pci_enable_device(pdev)))
		goto out_enable;

	if ((err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))))
		goto out_mask;
	pci_set_master(pdev);

	if ((err = pci_request_regions(pdev, "adlerdev")))
		goto out_regions;

	/* Map the BAR.  */
	if (!(dev->bar = pci_iomap(pdev, 0, 0))) {
		err = -ENOMEM;
		goto out_bar;
	}

	/* Connect the IRQ line.  */
	if ((err = request_irq(pdev->irq, adlerdev_isr, IRQF_SHARED, "adlerdev", dev)))
		goto out_irq;

	/* Allocate a buffer. */
	if (!(dev->data_cpu = dma_alloc_coherent(&dev->pdev->dev,
			ADLERDEV_BUF_SIZE,
			&dev->data_dma, GFP_KERNEL))) {
		goto out_cdev;
	}

	adlerdev_iow(dev, ADLERDEV_INTR, 1);
	adlerdev_iow(dev, ADLERDEV_INTR_ENABLE, 1);
	
	/* We're live.  Let's export the cdev.  */
	cdev_init(&dev->cdev, &adlerdev_file_ops);
	if ((err = cdev_add(&dev->cdev, adlerdev_devno + dev->idx, 1)))
		goto out_cdev;

	/* And register it in sysfs.  */
	dev->dev = device_create(&adlerdev_class,
			&dev->pdev->dev, adlerdev_devno + dev->idx, dev,
			"adler%d", dev->idx);
	if (IS_ERR(dev->dev)) {
		printk(KERN_ERR "adlerdev: failed to register subdevice\n");
		/* too bad. */
		dev->dev = 0;
	}

	return 0;

out_cdev:
	dma_free_coherent(&dev->pdev->dev, PAGE_SIZE, dev->data_cpu, dev->data_dma);
	free_irq(pdev->irq, dev);
out_irq:
	pci_iounmap(pdev, dev->bar);
out_bar:
	pci_release_regions(pdev);
out_regions:
out_mask:
	pci_disable_device(pdev);
out_enable:
	mutex_lock(&adlerdev_devices_lock);
	adlerdev_devices[dev->idx] = 0;
	mutex_unlock(&adlerdev_devices_lock);
out_slot:
	kfree(dev);
out_alloc:
	return err;
}

static void adlerdev_remove(struct pci_dev *pdev)
{
	struct adlerdev_device *dev = pci_get_drvdata(pdev);
	if (dev->dev) {
		device_destroy(&adlerdev_class, adlerdev_devno + dev->idx);
	}
	cdev_del(&dev->cdev);
	adlerdev_iow(dev, ADLERDEV_INTR_ENABLE, 0);
	dma_free_coherent(&dev->pdev->dev, ADLERDEV_BUF_SIZE, dev->data_cpu, dev->data_dma);
	free_irq(pdev->irq, dev);
	pci_iounmap(pdev, dev->bar);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	mutex_lock(&adlerdev_devices_lock);
	adlerdev_devices[dev->idx] = 0;
	mutex_unlock(&adlerdev_devices_lock);
	kfree(dev);
}

static int adlerdev_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return 0;
}

static int adlerdev_resume(struct pci_dev *pdev)
{
	return 0;
}

static struct pci_device_id adlerdev_pciids[] = {
	{ PCI_DEVICE(ADLERDEV_VENDOR_ID, ADLERDEV_DEVICE_ID) },
	{ 0 }
};

static struct pci_driver adlerdev_pci_driver = {
	.name = "adlerdev",
	.id_table = adlerdev_pciids,
	.probe = adlerdev_probe,
	.remove = adlerdev_remove,
	.suspend = adlerdev_suspend,
	.resume = adlerdev_resume,
};

/* Init & exit.  */

static int adlerdev_init(void)
{
	int err;
	if ((err = alloc_chrdev_region(&adlerdev_devno, 0, ADLERDEV_MAX_DEVICES, "adlerdev")))
		goto err_chrdev;
	if ((err = class_register(&adlerdev_class)))
		goto err_class;
	if ((err = pci_register_driver(&adlerdev_pci_driver)))
		goto err_pci;
	return 0;

err_pci:
	class_unregister(&adlerdev_class);
err_class:
	unregister_chrdev_region(adlerdev_devno, ADLERDEV_MAX_DEVICES);
err_chrdev:
	return err;
}

static void adlerdev_exit(void)
{
	pci_unregister_driver(&adlerdev_pci_driver);
	class_unregister(&adlerdev_class);
	unregister_chrdev_region(adlerdev_devno, ADLERDEV_MAX_DEVICES);
}

module_init(adlerdev_init);
module_exit(adlerdev_exit);
