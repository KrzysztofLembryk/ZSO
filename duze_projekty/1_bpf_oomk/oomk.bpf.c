#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/*
    Copy by ssh:  scp -P 2222 ./oomk.bpf.c root@127.0.0.1:~/bpf_oom_killer/
    #######################################################################
    Where to look for files:
    - kernel functions etc - /usr/src/linux-headers-$(uname -r)/
    - tracepoints - /sys/kernel/debug/tracing 
        check members of tracepoint ```args``` struct: 
        ```bpftrace -vl tracepoint:syscalls:sys_enter_openat```
    - kprobes - 
    - structs - /usr/src/linux-headers-$(uname -r)/arch/x86/include/asm/ptrace.h 
*/


// KMALLOC_MAX_SIZE = 1UL << 22 = 4,194,304 bytes = 4MB
// So with such number as max_entries each entry can have at most 400bytes
#define MAX_ENTRIES 8192

struct {
    // We use LRU map since if there is no free space left in our map it will remove
    // least used entry from it, and because we need to pre-allocate our map it will 
    // have fixed size, so such mechanism is useful.
    __uint(type, BPF_MAP_TYPE_HASH); 
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} sysinfo_map SEC(".maps");


// KPROBE - gets function arguments, but before function execution, so these args are
// NOT SET YET
SEC("kprobe/si_meminfo")
int check_ram_usage_entry(void *ctx)
{
    // In sysinfo we have freeram, totalram and MEM_UNIT, since there is a MEM_UNIT
    // it means that freeram and totalram are not in bytes, but in mem_units, thus
    // to get sizes in bytes we need to multiply by mem_unit
    struct sysinfo *sys_info = (struct sysinfo *)PT_REGS_PARM1((struct pt_regs*)ctx);

    // We save our pointer to sys_info struct in our map, so that we can access it 
    // in KRETPROBE, since currently this struct is uninitialized. 
    // In kretprobe's ctx we cannot access this struct, we can only get return value,
    // so we will store pointer to it in our map.
    __u64 sys_info_addr = (__u64)sys_info;
    pid_t pid = bpf_get_current_pid_tgid() >> 32;

    // We save addresses to our variables in map so that in KRETPROBE we can access
    // them, since currently they are uninitialized, however in kretprobe's ctx we 
    // don't get these arguments, we can only get return value, thus we store them
    // in our map
    bpf_map_update_elem(
        &sysinfo_map, 
        &pid, 
        // &sys_info since map_update looks at what is inside the pointer
        // and copies these values, so the result is the same as if we would pass
        // &sys_info_addr
        &sys_info_addr, 
        BPF_NOEXIST
    );

    return 0;
}

// KRETPROBE - gets ONLY RETURN VALUE, in ctx apart from that is trash, fires after
// function execution
// useful docs: https://docs.ebpf.io/ebpf-library/libbpf/ebpf/BPF_KRETPROBE/
SEC("kretprobe/si_meminfo")
int check_ram_usage_exit(void *ctx)
{
    // struct task_struct *task = (void *)bpf_get_current_task();
    pid_t pid = bpf_get_current_pid_tgid() >> 32;
    __u64 *sys_info_ptr = bpf_map_lookup_elem(&sysinfo_map, &pid);

    if (!sys_info_ptr)
    {
        return 0;
    }

    struct sysinfo *sys_info = (struct sysinfo *)*sys_info_ptr; 

    __u64 free_ram; 
    __u64 total_ram; 
    __u32 mem_unit;
    bpf_probe_read_kernel(&free_ram,  sizeof(free_ram),  &sys_info->freeram);
    bpf_probe_read_kernel(&total_ram, sizeof(total_ram),  &sys_info->totalram);
    bpf_probe_read_kernel(&mem_unit,  sizeof(mem_unit),  &sys_info->mem_unit);

    bpf_printk("\nFree Ram: %lu \nTotal Ram: %lu\nMem_unit: %u", free_ram * mem_unit, total_ram * mem_unit, mem_unit);

    bpf_map_delete_elem(&sysinfo_map, &pid);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";