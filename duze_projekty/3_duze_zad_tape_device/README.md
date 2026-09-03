# Notes
## DMA
- yt vid with dma: https://www.youtube.com/watch?v=kl9c6DrDnHo
- docs (api, how to alloc buffer): https://kernel-internals.org/mm/dma/
- linux kernel dma docs: https://docs.kernel.org/core-api/dma-api.html
- dma exmpl: https://blakerain.com/blog/allocating-memory-for-dma-in-linux/
## bvec
```c
/**
 * struct bio_vec - a contiguous range of physical memory addresses
 * @bv_page:   First page associated with the address range.
 * @bv_len:    Number of bytes in the address range.
 * @bv_offset: Start of the address range relative to the start of @bv_page.
 *			in bv_page might be data we dont want, thus we start reading from offset
 *			to read only specific data we want from this page
 *			
 *			  page 
 *		-------------
 *		|			      |
 *		| some data	|
 *		|			      |
 *		-------------  --
 *		|  offset		|   |
 *		|				    |   |
 *		| our data	|	  | bv_len
 *		|			      |	  |
 *		|			      |	  |
 *		-------------  --
 *		|			      |
 *		| some data |
 *		|			      |
 *		-------------
 *
 * All pages within a bio_vec starting from @bv_page are contiguous and
 * can simply be iterated (see bvec_advance()).
 */
struct bio_vec {
	struct page	*bv_page;
	unsigned int	bv_len;
	unsigned int	bv_offset;
};

```
# Q&A

##

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

## running tests
```bash
./rw_basic  /dev/tapedev0s0
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