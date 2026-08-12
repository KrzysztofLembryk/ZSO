# Flow of the solution
We insert our module into the kernel, then:

- ```init_tapedev``` is invoked
    - we register our block device by calling ```register_blkdev()```, it gives us major number and creates TAPEDEV_NAME in ```/proc/devices```, it doesn't create any device yet. Major number means that our driver will be used to handle all of tapedev devices.

    - then we ```class_register()``` our tapedev_class, it creates a class entry in ```/sys/class/tapedev``` and lets kernel track devices that belong to this class

    - then we register our PCI driver by using ```pci_register_driver(&tapedev_pci_driver)```; immediately it scans PCI devices and matches them against
    our driver's id_table:
    ```c
    tapedev_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TAPEDEV, PCI_DEVICE_ID_TAPEDEV) }
    }
    ```
    And for each match it calls our ```probe``` function

- ```tapedev_probe``` is invoked (whole flow well described in the code)