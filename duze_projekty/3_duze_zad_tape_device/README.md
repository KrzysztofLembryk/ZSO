# Q&A

## Data flow
```text
When userspace does read()/write() on /dev/tapedev0s2, the kernel block layer queues a struct request, your driver's queue handler picks it up, translates it into a command for the PCI card (via MMIO/DMA), the PCI card drives the tape library hardware, and completion triggers an IRQ that calls your blk_mq_end_request().
```

## Debbugging/releasing inserted module

```bash
# Check inserted modules
lsmod

# or for shorter version
lsmod | cut -d " " -f 1

# check if tapedev module is present, and how many refcount it has
lsmod | grep tapedev

# Check if any process has it open
lsof | grep tapedev

# Find the stuck insmod
ps aux | grep insmod
kill -9 <PID>

# last resort
rmmod -f tapedev

# if still stuck just reboot
```

## installing kernel
We need to install and boot our kernel that we compiled in order to install our external module
```bash
make -j8
make modules_install
make install
sudo update-grub
```

## Installing module
```bash
make  # or bear -- make
make install
insmod tapedev.ko
```

## Clang acting up
- If clangd doesnt see kernel modules/funcs/etc., we need to create compile_commands.json
in our project (```tapedev/``` dir)

```bash
# First, clean the previous build so `bear` can capture the full compilation process
make clean

# Then, run make wrapped with bear
bear -- make

# or (but above works fine)
# make -C /root/zso_linux_kernel M=$(pwd) compile_commands.json
```

- in root of project create ```.vscode/settings.json```, inside ```settings.json```
```json
{
  "clangd.arguments": [
    "--compile-commands-dir=./tape_dev"
  ]
}
```

- remember to have kernel source cloned and compiled 

- probably you also must have ```.clangd``` file:
```bash
CompileFlags:
  Add:
    - -D__KERNEL__
    - -DMODULE
    - -nostdinc
    - -include
    - /root/zso_linux_kernel/include/linux/compiler-version.h
    - -include
    - /root/zso_linux_kernel/include/linux/kconfig.h
    - -I/root/zso_linux_kernel/include
    - -I/root/zso_linux_kernel/arch/x86/include
    - -I/root/zso_linux_kernel/arch/x86/include/generated
    - -I/root/zso_linux_kernel/include/uapi
    - -I/root/zso_linux_kernel/arch/x86/include/uapi
    - -I/root/zso_linux_kernel/include/generated/uapi
    - -I/root/zso_linux_kernel/arch/x86/include/generated/uapi
    - -I/root/zso_linux_kernel/..
```

Or

```bash
CompileFlags:
  Remove:
    - "-nostdinc"
    - "-mno-80387"
    - "-mno-fp-ret-in-387"
    - "-mpreferred-stack-boundary=3"
    - "-mskip-rax-setup"
    - "-fconserve-stack"
    - "-falign-jumps=1"
    - "-falign-loops=1"
    - "-fno-allow-store-data-races"
  Add:
    - "-nostdlibinc"
    - "-Wno-unknown-warning-option"
```

- then clangd: Restart language server by ```Ctrl+Shift+P```