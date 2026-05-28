# Kernel modules

**Kernel module** is relocatable code/data hat can be inserted and removed from the kernel while the system is running. 

The *module is responsible for a certain service in the kernel* -- for example, modules can be device and file system drivers, network filters, cryptographic algorithms

## Useful module commands

- how to laod a module
```bash
# If parameters are given, they are passed to the module.
insmod full_path_to_module_name.ko [params]

# !! Enter the full path to the module, insmod does not try to search for the file we need

# Passing params in the form 'var_name = value'
insmod ne.ko io = 0x300 irq = 7
```

- how to remove module
```bash
# module_name is module name, not .ko file name
rmmod module_name
```

- get module info
```bash
modinfo module_or_file_name
```

- listing loaded modules
```bash
# Lists all loaded modules with information about their dependencies (the same data can be seen by cat /proc/modules)
lsmod
```

## Compilation of modules
- To compile our module, we need to create a **Kbuild** file describing our code, for example:
```bash
# This compiles the module.c file to the module.ko module, and the different_module.c file to the 
# different_module.ko file
obj-m := module.o different_module.o
```

- To compile ONE module from several sources:
```bash
# Kbuild file will compile the module_p1.c and module_p2.c files and combine them into the module.ko 
# module.
obj-m := module.o
module-objs := module_p1.o module_p2.o
```

- To call the compilation of the module, you should call **make in the kernel source directory**, pointing it to our directory with external modules:
```bash
make -C /usr/src/linux-<version> M=/path/to/the/module
```

### Example of Makefile

```bash
# path to linux SOURCE (cloned from repo or downloaded as tar.gz)
# Remember to run: make oldconfig && make prepare and make -jn on the source 
KDIR ?= /root/zso_linux_kernel

default:
	$(MAKE) -Wall -Wextra -C $(KDIR) M=$$PWD

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean

```

## Module's metadata
```c
MODULE_LICENSE("GPL"); // needed if we want to use kernel functions
MODULE_AUTHOR("John Paul");
MODULE_DESCRIPTION("2137th module");
```

## Module's constructor and destructor
Modules do not have a main function or their own process / thread (unless they create it themselves, but it is quite rare). Instead, the **module's code is called by various kernel subsystems when there is something to do for it**.


Each module can define a function initiating the module (<span style="color: green;">constructor</span>) and releasing the module (<span style="color: red;">destructor</span>)

```c
int init_function(void) {
    // i.e. allocate memory for arrays
	kbuf = kmalloc(bufsize, GFP_KERNEL);
	if (!kbuf)
	{
		pr_err("hello_init :: failed to alloc buf");
		err = -ENOMEM;
		goto err_sysfs_file;
	}

    // or allocate region for character device
    if ((err = alloc_chrdev_region(&hello_major, 0, 2, "hello")))
        goto err_alloc;

    // andd add this device
    if ((err = cdev_add(&hello_cdev, hello_major, 1)))
        goto err_cdev;
}

void cleanup_function(void) {
    // free allocated resources
    kfree(kbuf);
    // We reverse everything what we did in init
	device_destroy(&hello_class, hello_major);
	class_unregister(&hello_class);
}

module_init(init_function);
module_exit(cleanup_function);
```

The **init function is called when the module is loaded**. If everything went well, it should return 0. If it failed to initialize the module, it should return the error code (negated code from errno*.h) -- the module will be immediately removed by the kernel.

The cleanup function is called when the module is removed (but **is not called when the init function has returned an error**)

## Using kernel funcs in modules
- In modules, you can freely use symbols defined and exported by the main kernel code and by other modules.

- In order for a **symbol of our module** to be visible from the outside, it should be exported with the macro EXPORT_SYMBOL (or EXPORT_SYMBOL_GPL - exports a symbol only for modules under the GPL)

```c
EXPORT_SYMBOL(my_function);

int my_function(int x) {
    ...
}
```

## Adding parameters to the module

To be able to use the following:
```bash
insmod module.ko some_var=5
```

We need to declare some_var in our module and use ```module_param(variable, type, permissions)``` macro on it (also params should have description: ```MODULE_PARM_DESC(variable, description)```)

```c
int some_var = 69;
module_param(some_var, int, 0);
MODULE_PARM_DESC(some_var, "Best var ever");

char *path="/sbin/modprobe";
module_param(path, charp, 0);
MODULE_PARM_DESC(path, "Path to modprobe");
```

We can also declare array of params: ```module_param_array(variable, type, pointer_to_count, permissions);```

```c
int num_paths = 2;
char *paths[4] = {"/bin", "/sbin", NULL , NULL};
module_param_array(paths, charp, &num_paths, 0);
MODULE_PARM_DESC(paths, "Search paths [4]");
```

## Reference Count
Each module has its own reference count -- as long as it is positive, the kernel will not allow the module to be removed.

The management of such a counter is usually done by other kernel subsystems, but you have to help them by passing the pointer to your module (macro THIS_MODULE).


For example, for a character device driver, you must fill the **owner field** of the file_operations structure with this pointer.

For example:
```c
static struct file_operations hello_once_fops = {
	.owner = THIS_MODULE,
	.read = hello_once_read,
	.open = hello_open,
	.release = hello_release,
};

// inside init function we have:
cdev_init(&hello_once_cdev, &hello_once_fops);
```