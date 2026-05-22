# What is a device driver

A device driver is a piece of software that operates given device that is attached to
a computer. 

*"A driver provides software interface to hardware devices, it acts as a translator between a hardware and applications."* 

Enabling other software to access hardware functions without needing to know precise details about the hardware.

Example:
```text
    A high-level application for interacting with a serial port may simply have two functions for send data and receive data. At a lower level, a device driver implementing these functions would communicate with the particular serial port controller installed on a user's computer. The commands needed to control a 16550 UART are much different from the commands needed to control a USB-to-serial adapter.
```

The driver almost always has exclusive **direct access** to the device. Device drivers are usually **kernel modules**.  

## How to communicate between driver and kernel
To make a driver's functionality available to user programs, you must use one of many possible communication mechanisms with the kernel. The most common are:

- block device file -- used for hard drives and sufficiently similar creations (CD-ROM, SSD, ...). *The driver exposes read and write* functions for blocks, and the block layer handles requests from the user (buffering, queuing, etc.) 

- **character device file** -- used for most types of devices. **The driver provides functions corresponding to system calls that operate on files** -- the kernel passes these calls directly to the driver, allowing the implementation of any interface. 

- network interface -- the driver exposes functions for sending and receiving packets, to which the network subsystem connects. The user can use it through socket calls.

- file in ```proc``` -- used in the case of drivers with a trivial interface (e.g., the entire driver functionality is reading/writing a single parameter).

- file in ```sysfs``` -- as above, but newer (and simpler) interface.


## Character devices numbers
Using character and block devices involves **creating a corresponding special file** somewhere in the file system (```/dev```) and opening it.

This special file is only a "gateway to the kernel" and the only information about it stored in the file system is:
- a flag indicating the device type (```b -- block, c -- character```)
- a **major** device number - major number chooses the driver
- a **minor** device number - minor number chooses instance of the device supported by that driver

(there are cases where a single major number is shared by many drivers if they only export one device)


At the C language level **both numbers are packed into a single number** of type ```dev_t```
Useful macros for dev_t:

- ```int MAJOR(dev_t nbr)``` - returns major device nbr
- ```int MINOR(dev_t nbr)``` - returns minor device nbr
- ```dev_t MKDEV(int major, int minor)``` - packs numbers into dev_t type. 

Example:
```c
// Usually 32 bits, 12 bits for major, 20 bits for minor
dev_t device_nbr;

// We set which value should first minor have
unsigned first_minor = 0;

// Beginning from first_minor value we set how many minors we want.
// So if first_minor = 0, than we have minors 0 and 1, for count = 2.
// Minors create arithmetic sequence with r = 1, and a_0 = first_minor
unsigned minor_count = 2;

// In our module inside init function we need to initialize our device_nbr
static int init_func(void) {
    // Major nbr will be chosen dynamically
	if ((err = alloc_chrdev_region(&device_nbr, first_minor, minor_count, "device_name")))
		goto err_alloc;
}

```

