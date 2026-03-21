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

/* ####################### CONSTANTS DEFS #######################*/
#define MAX_ENTRIES 8192
#define ONE_GB (1024ULL * 1024 * 1024)
#define ALLOWED_NUMBER_OF_RAND_CALLS 99

/* ####################### HELPER FUNCTIONS DEFS #######################*/
// Function checks if inside str1 there is 'oomp' substr
static bool oomp_substr_exists_in(
    const char *str, 
    size_t str_len
)
{
    static const char *oomp_str = "oomp";
    static const size_t oomp_len = 4;

    for (size_t i = 0; i < str_len; i++)
    {
        if (str[i] == oomp_str[0])
        {
            for (size_t j = 1; j < oomp_len; j++)
            {
                if (i + j < str_len)
                {
                    if (str[i + j] == oomp_str[j])
                    {
                        if (j >= oomp_len - 1)
                        {
                            return true;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                else 
                {
                    return false;
                }
            }
        }
    }
    return false;
}

static bool is_oomp_present()
{
    struct task_struct *task = (void *)bpf_get_current_task();
    char comm[16];
    BPF_CORE_READ_STR_INTO(&comm, task, comm);

    return oomp_substr_exists_in(comm, 16);
}

/* ####################### MAPS DEFS #######################*/
// KMALLOC_MAX_SIZE = 1UL << 22 = 4,194,304 bytes = 4MB
// So with such number as max_entries each entry can have at most 400bytes

struct concurrent_element {
    struct bpf_spin_lock semaphore;
    int rand_calls_count;
};

// STATE_MAP - in it we will store all information about whether given process
// should be killed or not
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH); 
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u64);
} state_map SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_HASH); 
    // We dont expect many processes to run sys_info
    __uint(max_entries, 16);
    __type(key, __u32);
    __type(value, __u64);
} sysinfo_map SEC(".maps");

// Global variable (translated into one elem array by compiler) that tells us if we 
// can kill processes (if memory usage is >= 1GB)
// !!! REMEMBER TO USE __sync_* for this variable to prevent DATA RACES !!!
int is_killing_allowed = 0;

SEC("tracepoint/syscalls/sys_enter_getrandom/")
int rand_entry()
{
    // Only when oomp is present in process name we do anything
    if (is_oomp_present())
    {
        // __sync_fetch_and_add - returns previous value stored in variable, by adding
        // 0 we don't change this variable so it works like safe read
        int i_can_kill = __sync_fetch_and_add(&is_killing_allowed, 0);
    
        if (i_can_kill)
        {
            bpf_printk("rand_entry:: I CAN KILL\n");
        }
        else
        {
            bpf_printk("rand_entry:: i cannot kill :( :( \n");
        }
    }

    return 0;
}

// KPROBE - gets function arguments, but before function execution, so these args are
// NOT SET YET
SEC("kprobe/si_meminfo")
int check_ram_usage_entry(void *ctx)
{
    struct sysinfo *sys_info = (struct sysinfo *)PT_REGS_PARM1((struct pt_regs*)ctx);

    // We save our pointer to sys_info struct in our map, so that we can access it 
    // in KRETPROBE, since in kprobe this struct is uninitialized. 
    // In kretprobe's ctx we cannot access this struct, we can only get return value,
    // so we will store pointer to it in our map.
    __u64 sys_info_addr = (__u64)sys_info;
    pid_t pid = bpf_get_current_pid_tgid() >> 32;

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
    pid_t pid = bpf_get_current_pid_tgid() >> 32;
    __u64 *sys_info_ptr = bpf_map_lookup_elem(&sysinfo_map, &pid);

    if (!sys_info_ptr)
    {
        return 0;
    }

    // In sysinfo we have freeram, totalram and MEM_UNIT, since there is a MEM_UNIT
    // it means that freeram and totalram are not in bytes, but in mem_units, thus
    // to get sizes in bytes we need to multiply by mem_unit
    struct sysinfo *sys_info = (struct sysinfo *)*sys_info_ptr; 

    __u64 free_ram; 
    __u64 total_ram; 
    __u64 ram_used;
    __u32 mem_unit;
    bpf_probe_read_kernel(&free_ram,  sizeof(free_ram),  &sys_info->freeram);
    bpf_probe_read_kernel(&total_ram, sizeof(total_ram),  &sys_info->totalram);
    bpf_probe_read_kernel(&mem_unit,  sizeof(mem_unit),  &sys_info->mem_unit);

    free_ram = free_ram * (__u64)mem_unit;
    total_ram = total_ram * (__u64)mem_unit;
    ram_used = total_ram - free_ram;

    // __u64 gb_int = ram_used / ONE_GB;
    // __u64 gb_frac = (ram_used % ONE_GB) * 100 / ONE_GB;

    if (ram_used >= ONE_GB)
    {
        // Read value at a, write b to a, return original value of a
        int was_killing_allowed = __sync_lock_test_and_set(&is_killing_allowed, 1);

        if (!was_killing_allowed)
        {
            bpf_printk("We should check if there is anyone that should be killed, since when last checking killing was not allowed");
        }
    }
    else 
    {
        // Not enough ram is being used, we do not allow killing
        __sync_lock_test_and_set(&is_killing_allowed, 0);
    }

    bpf_map_delete_elem(&sysinfo_map, &pid);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";