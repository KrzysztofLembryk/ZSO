# Running qemu vm
qemu-system-x86_64 -device virtio-scsi-pci,id=scsi0 -drive file=zso2026_cow.qcow2,if=none,id=drive0 -device scsi-hd,bus=scsi0.0,drive=drive0 -enable-kvm -m 8G -device virtio-balloon -fsdev local,id=hshare,path=hshare/,security_model=none -device virtio-9p-pci,fsdev=hshare,mount_tag=hshare -net nic,model=virtio -net user,hostfwd=tcp::2222-:22 -chardev stdio,id=cons,signal=off -device virtio-serial-pci -device virtconsole,chardev=cons


# Creating new COW from zso image
qemu-img create -f qcow2 -F qcow2 -o backing_file=zso2026.qcow2 zso2026_cow.qcow
