#!/bin/bash 
qemu-system-x86_64 -device virtio-scsi-pci,id=scsi0 -drive file=zso2026_cow_for_kernel_task.qcow,if=none,id=drive0 -device scsi-hd,bus=scsi0.0,drive=drive0 -enable-kvm -cpu host -smp 7 -m 8G -device virtio-balloon -fsdev local,id=hshare,path=hshare/,security_model=none -device virtio-9p-pci,fsdev=hshare,mount_tag=hshare -net nic,model=virtio -net user,hostfwd=tcp::2223-:22
