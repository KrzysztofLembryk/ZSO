# Running qemu vm
qemu-system-x86_64 -device virtio-scsi-pci,id=scsi0 -drive file=zso2026_cow.qcow2,if=none,id=drive0 -device scsi-hd,bus=scsi0.0,drive=drive0 -enable-kvm -m 8G -device virtio-balloon -fsdev local,id=hshare,path=hshare/,security_model=none -device virtio-9p-pci,fsdev=hshare,mount_tag=hshare -net nic,model=virtio -net user,hostfwd=tcp::2222-:22 -chardev stdio,id=cons,signal=off -device virtio-serial-pci -device virtconsole,chardev=cons


# Creating new COW from zso image
qemu-img create -f qcow2 -F qcow2 -o backing_file=zso2026.qcow2 zso2026_cow.qcow

# Script for making, removing old and setting up new kernel:

```bash
#!/bin/bash
set -e

KERNEL_SRC="${HOME}/linux-6.18.5"

# 0. remove old myCustom kernels
cd "/boot"
rm *myCustom* || true

# 1. Change to the kernel source directory
cd "$KERNEL_SRC"

# 1. Build the kernel using 7 parallel jobs
make -j 7

# 2. Install kernel modules
sudo make modules_install

# 3. Install the kernel (copies kernel, initramfs, System.map, etc.)
sudo make install

# 4.0 After installing new kernel we need to extract its index so that we can set it in GRUB
kernel_index=""
while read -r line; do
    if [[ $line =~ menuentry\ \'Debian\ GNU/Linux,\ with\ Linux\ [^-]+-myCustom-([a-zA-Z0-9]+)(-dirty)?\' ]]; then
        kernel_index="${BASH_REMATCH[1]}${BASH_REMATCH[2]}"
        break
    fi
done < <(grep "menuentry" /boot/grub/grub.cfg | grep -i custom)

# 4.1 Set GRUB to boot custom kernel on next reboot
if [[ -n "$kernel_index" ]]; then
    sudo grub-reboot "Advanced options for Debian GNU/Linux>Debian GNU/Linux, with Linux 6.18.5-myCustom-${kernel_index}"
else
    echo "Kernel index not found."
fi

# 5. Show GRUB environment (default and next boot entry)
sudo grub-editenv list

echo "Kernel build and install complete. On next reboot, your custom kernel will boot."
```
