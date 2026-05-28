KDIR ?= /root/zso_linux_kernel

default:
	$(MAKE) -Wall -Wextra -C $(KDIR) M=$$PWD

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
