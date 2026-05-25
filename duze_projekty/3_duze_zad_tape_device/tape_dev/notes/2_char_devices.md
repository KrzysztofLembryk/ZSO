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
    // We also need to add the name of the associated device or driver for which we want
    // this number
	if ((err = alloc_chrdev_region(&device_nbr, first_minor, minor_count, "device_name")))
		goto err_alloc;
}

```

The kernel exports information about available device drivers live in the ```sysfs``` file system, and the udevd program (or systemd-udevd) monitors this information continuously  and creates appropriate files in ```/dev```.


## Registering device driver workflow

1) First we need to allocate space for aforementioned device numbers for our device drivers (in init func)
```c
int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count, const char *name);
void unregister_chrdev_region(dev_t first, unsigned int count);
```

2) Prepare the ```file_operations``` structure describing the operations on our device. Such structures are usually global

```c
static struct file_operations our_fops = {
	.owner = THIS_MODULE,
	.read = our_read_func,
	.open = our_open_func,
	.release = our_release_func,
};
```

3) Prepare and fill the ```cdev``` (character device) structure

```c
static struct cdev our_cdev_1;
static struct cdev our_cdev_2;
void cdev_init(struct cdev *cdev, const struct file_operations *fops);

// iniside init func
cdev_init(&our_cdev_1, &our_fops);

// If we want more devices we need to also init them (i.e. with different ops, like our_ops_2)
cdev_init(&our_cdev_2, &our_fops);
```

(We can also request a dynamic allocation of the structure: ```struct cdev *cdev_alloc(void);```,
In this case, we must manually fill the ops field with a pointer to our structure (do not mix this call with cdev_init).)

4) Register our ```cdev``` structure:

```c
int cdev_add(struct cdev *p, dev_t dev, unsigned count);
void cdev_del(struct cdev *p);

// iniside init func
// count: the number of consecutive minor numbers corresponding to this device
unsigned count = 1;
if ((err = cdev_add(&our_cdev_1, device_nbr, count)))
    goto err_cdev;

// We allocated minor_count = 2, and here we use it, so our_cdev_1 has minor = 0,
// and our_cdev_2 has minor = 1, and both of them have the same major
if ((err = cdev_add(&hello_once_cdev, device_nbr + 1, count)))
    goto err_cdev_2;
```


At this point, our device becomes available to user space when someone opens the appropriate special file.

If the cdev structure was created by cdev_alloc, the structure will be automatically released by cdev_del. If, on the other hand, it was initialized by cdev_init, releasing it is the driver's responsibility.

Note that cdev_del only detaches the device from the device array, but does not guarantee that no one is using it anymore - previously opened file descriptors will still work (although if we are in module_exit, we are guaranteed that there are no such descriptors). In the case of implementing e.g. a device that should support hot-unplug, you must ensure this yourself e.g. by **counting references**.

5) Register the **device class** in ```sysfs``` (or use an existing one if it fits).
This is only done **once for all our devices** (or for the device type if we have many).

```c
struct class our_class = {
    .name = "my_new_driver_class",
};
int class_register(struct class *class);
void class_unregister(struct class *class);

// inside init func
if ((err = class_register(&our_class)))
    goto err_class;
```

6) Register **our device** in ```sysfs```:

    - ```parent``` points to the device to which our device is connected -- the directory in sysfs corresponding to our device will be a subdirectory of the directory of the specified device. For character device drivers corresponding to e.g. PCI devices, the parent will be set to the dev field of the pci_device structure. You can set this parameter to NULL to receive a top-level device. 
    - ```drvdata``` can be used to store additional private information for our driver (useful if e.g. we want to create files in sysfs to control our device). fmt and subsequent parameters are passed to sprintf to create the device name that will appear in ```/dev```.
```c
struct device *device_create(struct class *cls, struct device *parent,
               dev_t devt, void *drvdata, const char *fmt, ...);
void device_destroy(struct class *cls, dev_t devt);

static struct device *our_device;

// first zero is pointer to the parent struct device of this new device, if any (we dont have any parent)
struct device *parent = 0;
// second zero is the data to be added to the device for callbacks (we add no data)
void *drvdata = 0;
// device name it will appear in /dev
const char *dev_name = "my_device";

// inside init func
our_device = device_create(&our_class, parent, device_nbr, drvdata, dev_name);
if (IS_ERR(our_device)) {
    err = PTR_ERR(our_device);
    goto err_device;
}
```

At this point, udevd will receive a notification about the new device and create the appropriate file in ```/dev```.


## ```file_operations``` structure
The ```file_operations``` structure (defined in ```linux/fs.h```) describes how to perform operations on a given file. Every file (and in general everything that can be an open file descriptor) in Linux has such a structure -- for ordinary files, it is provided by the file system driver. For character devices, it is provided by the device driver. It has many fields (corresponding to operations), the most important of which are:

```c
struct file_operations {
    struct module *owner;
    loff_t (*llseek) (struct file *, loff_t, int);
    ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
    ssize_t (*write) (struct file *, const char __user *, size_t,
        loff_t *);
    int (*unlocked_ioctl) (struct file *, unsigned int,
        unsigned long);
    int (*compat_ioctl) (struct file *, unsigned int,
        unsigned long);
    int (*mmap) (struct file *, struct vm_area_struct *);
    int (*open) (struct inode *, struct file *);
    int (*release) (struct inode *, struct file *);
    /* ... */
};
```

We **must fill** the ```owner``` field with a pointer ```THIS_MODULE``` -- this allows the kernel to **automatically manage the module's reference counter**.


## ```file``` structure
The ```file``` structure (defined in ```linux/fs.h```) represents an **open file within the kernel**. It is *created by the kernel when open is called* and passed to all operations on the file until the last close call (i.e., when release is called). It is worth noting that an open file (the file structure) is a different thing than a file on disk (represented by the inode structure). 

```c
struct file {
    mode_t                  f_mode;
    loff_t                  f_pos;
    unsigned int            f_flags;
    struct file_operations  *f_op;
    void                    *private_data;

    /* ... */
};
```
 - The ```f_mode``` field allows you to **determine whether the file is open for reading (FMODE_READ), writing (FMODE_WRITE), or both**. You do not need to check this field in the read and write functions, because the kernel performs this test before calling the driver's function.
 
- The ```f_pos``` field specifies the **position for writing or reading** (used by read, write, lseek, etc.).

- The ```f_flags``` flags are mainly used to **check if operation should be blocking or not** (O_NONBLOCK), although it contains many more flags.

- The ```f_op``` field **specifies a set of functions that implement file operations**. This field is set (to operations from the cdev structure) by the kernel when open is called, and then it is used for all subsequent operations (the driver can replace the value of this field in open to choose an alternative set of functions).

- The ```private_data``` pointer *is set to NULL when the file is opened*. The **driver can use this pointer for its own purposes** (in which case it is responsible for freeing the memory allocated for this field).

## The ```open``` operation -- opening a file

```c
int open(struct inode *inode, struct file *filp)
```

The ```open``` operation allows the driver to perform **preparatory operations before other operations** (so we can also do nothing in it), usually:
- **check for errors** related to the device (e.g., check if the device is ready);
- initialize the device if it is being opened for the first time and we are using lazy initialization;
- **identify the minor number** (```MINOR(inode->i_rdev)```) 
- if necessary, **replace the set of operations pointed to by f_op**
- **allocate memory** for data related to the device, initialize the data structures, and assign the private_data pointer;

## The ```release``` operation -- closing a file
The kernel keeps a **reference counter for each existing file structure** (it can be increased, for example, by calling dup or inheriting an open file by fork). When this **counter finally falls to 0** (close is called on the last descriptor, or the process holding that descriptor calls exit), the ```release``` function is called, *serving as the file's destructor*:

```c
int release(struct inode *inode, struct file *filp)
```

Its task is to **free the resources allocated in the ```open``` operation** and also:
- free the ```private_data``` memory;
- turn off the device when it is the last release call;


## ```read``` and ```write``` operations -- data transfer

```c
// offp --- is a pointer to the current position in the file.  
// If such a position makes sense for our file, we take it from there and write the updated position value there
ssize_t read(struct file *filp, char __user *buff, size_t count,
             loff_t *offp)
ssize_t write(struct file *filp, const char __user *buff, size_t count,
                loff_t *offp)
```

- The task of the ```read``` operation is to **copy a portion of data from the** <span style="color: red;">kernel address space</span> **to a specified address (buff) in the** <span style="color: yellow;">user address space</span>.

- The write operation works in the opposite direction.

These functions are used to implement many system calls (```read```, ```pread```, ```readv```, ...).


The value returned by this function will be interpreted as follows:

- a value greater than zero indicates the number of bytes copied; 
    - if it is equal to the value of the argument passed to the system call, it indicates complete success; 
    - if it is smaller, it means that only part of the data was transferred - then it should be expected that the program will repeat the system call (e.g., this is the standard behavior of the library functions fread/fwrite)

    - if the value is equal to 0, the end of the file has been reached (used only in read)

    - a negative value indicates an error

When implementing these operations, remember to maintain the correct semantics - returning an error means that no bytes were read/written. If our driver detects an error only after a certain amount of transferred bytes (and there is no easy way to undo the transfer), return the number of transferred bytes instead of an error - the error code will be returned when the user repeats the operation for the remaining bytes.

Example:
```c
static ssize_t our_read(struct file *file, char __user *ubuf, size_t count, loff_t *filepos)
{
    // somewhere defined constants
	size_t file_len = msg_len * msg_repeats;
	loff_t pos = *filepos;
	loff_t end;
	if (pos >= file_len || pos < 0)
		return 0;
	if (count > file_len - pos)
		count = file_len - pos;
	end = pos + count;
	while (pos < end)
        // we must use put_user/copy_from_user functions to get data from userspace to kernel and vice versa
		if (put_user(kbuf[pos++ % msg_len], ubuf++))
			return -EFAULT;
	*filepos = pos;
	return count;
}
```

## ```llseek``` operation -- changing the file position
```c
loff_t llseek(struct file *filp, loff_t off, int whence)
```
The ```llseek``` operation implements the system calls ```lseek``` and ```llseek```. The default kernel behavior when the ```llseek``` operation is not specified in the driver's operations is to change the f_pos field of the file structure. **If the concept of changing the file position does not make sense for the device, write a function that returns an error here**. A ready-made function ```no_llseek``` is available in the kernel for this purpose, which **always returns -ESPIPE**.

##  Operation ```ioctl``` – invoking device-specific commands
Function prototypes look as follows:

```c
// The first argument corresponds to the file descriptor passed by the system call. 
// The cmd argument is exactly the same as in the system call. 
// The optional arg argument is passed as an unsigned long number regardless of the type used in the system call.
long (*unlocked_ioctl) (struct file *filp, unsigned int cmd,
                unsigned long arg);
long (*compat_ioctl) (struct file *filp, unsigned int cmd,
                unsigned long arg);
```

- The ```unlocked_ioctl``` function **handles ioctl calls from the "main" kernel architecture**. 
- The ```compat_ioctl``` function handles ```ioctl``` calls **from user programs** in compatibility mode with the 32-bit architecture version – e.g., programs for the i386 architecture under a kernel for the x86_64 architecture. If the structures passed through ioctl do not contain fields of architecture-dependent size, both fields can be set to the same function.


Typically, the implementation of the ioctl operation simply contains a switch statement selecting the appropriate behavior depending on the value of the cmd argument. Different commands are represented by different numbers, which are usually given names using preprocessor definitions. The user program should be able to include a header file with such declarations (usually the same one that is used when compiling the driver module).


It is the responsibility of the driver interface developer to determine the numerical values corresponding to the commands interpreted by the driver. A simple choice, assigning successive small values to individual commands, unfortunately is generally not a good solution. Commands should be unique across the system to avoid errors when a correct command is sent to an incorrect device. 

```c
//The following macros (defined in linux/ioctl.h) should be used when determining numerical values for commands:

// general-purpose command (without an argument)
_IO(type, nr)

// command with write to user space
_IOR(type, nr, dataitem)

// command with read from user space
_IOW(type, nr, dataitem)

// command with write and read
_IOWR(type, nr, dataitem)

// Examples
#define HELLO_IOCTL_SET_REPEATS _IO('H', 0x00)
#define HELLO_IOCTL_CHANGE_GREETING _IOW('H', 0x01, char)
```

Where:
- **type** -- unique number for the driver (8 bits, selected after reviewing ```Documentation/userspace-api/ioctl/ioctl-number.rst```) – a magic number
- **nr** -- sequential command number (8 bits)
- **dataitem** -- structure associated with the command; the size of the given structure usually cannot be greater than 16kb-1 (depends on the value of _IOC_SIZEBITS). Encoding the structure size written/read as a parameter can be helpful for detecting programs compiled with outdated driver versions and helps to avoid, for example, writing beyond the buffer.


## sysfs

In the kernel, there is often a need to grant access to certain device data to user space. Using character devices for this purpose is rather cumbersome – a character device is a fairly "heavy" object, and access to it is done through a limited read/write interface or an inconvenient ioctl interface.

The first solution to these problems in Linux was the proc file system, allowing easy creation of a large number of special files for communication with the user. However, it had several drawbacks: primarily the lack of structure (everyone puts files wherever they like), and data transfer requires costly and delicate formatting and parsing of the byte stream.

To solve the problems with the proc file system, the sysfs file system was created. It has the following features:

    every device, driver, module, etc., in the system is based on a kobject structure and automatically receives a directory in sysfs

    directories in sysfs are organized hierarchically – in the case of devices, each device is a subdirectory of the device it is connected to

    relationships between objects are represented by symbolic links

    object attributes are represented by files

    it is mounted in /sys

To grant a user access to some functionality, you need to get access to your kobject structure and attach attributes to it. In the case of devices, the kobject structure is the kobj field of the device structure.