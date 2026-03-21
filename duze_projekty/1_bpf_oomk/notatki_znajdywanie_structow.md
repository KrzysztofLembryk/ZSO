# Useful remarks

##  how to kprobes/tracepoints structs
- in vmlinux.h EVERY KERNAL TYPE is generated
- so we can just do: ```grep -A 30 "struct pt_regs" vmlinux.h```
- or find it in kernel version in ptrace.h which is in ```/usr/src/linux-headers-$(uname -r)/arch/x86/include/asm/ptrace.h ```
- for TRACEPOINTS we check: ```/sys/kernel/debug/tracing/events```

## libbpf
- ```#include <bpf/bpf_helpers.h>```
- ```#include <bpf/bpf_tracing.h>```
- ```#include <bpf/bpf_core_read.h>```

## Compile to a BPF ELF object 
ELF - Executable and Linkable Format - contain binary data and metadata describing how the code and data should be loaded and executed by the operating system 
```bash
clang --target=bpf -g -Og -D__TARGET_ARCH_x86 -c  example.bpf.c -o example.bpf.o
```

## Generate a skeleton header 
This header provides type-safe example__open(), example__load(), example__attach(), and example__destroy() functions, plus direct pointers to your maps and global variables.
Needed for making a **BPF LOADER**

```bash
bpftool gen skeleton example.bpf.o name example > example.skel.h
```

## Run programmer

```bash
gcc example.user.c -o example.user -lbpf
./example.user
```

- to see what we print using printk: ```bpftool prog tracelog```

## CO-RE: Compile Once — Run Everywhere 
```vmlinux.h``` is a single header that <span style="color: yellow;"> contains every kernel type</span>. At load time libbpf uses BTF information embedded in the ELF object to relocate field accesses so they match the running kernel's struct layout — even if fields were added, removed, or reordered between kernel versions.

```bash
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```
Helper macros from bpf_core_read.h (e.g., BPF_CORE_READ, bpf_core_field_exists) let you write portable field accesses that the CO-RE relocator can adjust.

## Kprobe context - how to find them
- in vmlinux.h EVERY KERNAL TYPE is generated
- so we can just do: ```grep -A 30 "struct pt_regs" vmlinux.h```
- or find it in kernel version in ptrace.h which is in ```/usr/src/linux-headers-$(uname -r)/arch/x86/include/asm/ptrace.h ```
- for TRACEPOINTS we check: ```/sys/kernel/debug/tracing/events```

```c
SEC("kprobe/do_nanosleep")
int handle(void *ctx) {}
```

- The context pointer (void *ctx) is a struct pt_regs * — you read function arguments with PT_REGS_PARM1(ctx), PT_REGS_PARM2(ctx), etc. from <bpf/bpf_tracing.h>.

## How to use/cast contexts

- **how to use PT_REGS_PARM***:https://stackoverflow.com/questions/70905815/how-to-read-all-parameters-from-a-function-ebpf 

- exmpl: ```long sys_write(unsigned int fd, const char __user *buf, size_t count);``` 
- expl: PARM in PT_REGS_PARM1(x) stands for “parameter”. These macros give you access to the parameters of the function on which your kprobe or tracepoint is hooking to. So for example, **PT_REGS_PARM1(ctx)**, where ctx is the struct pt_regs *ctx context passed as an argument to your eBPF program, **will give you access to the first parameter, which is the file descriptor fd**. Similarly, PT_REGS_PARM3(ctx) will give you the count, as you can confirm by looking at this kernel sample (write_size).

