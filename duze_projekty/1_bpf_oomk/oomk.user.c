#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sys/sysinfo.h>
#include "oomk.skel.h"


int main(void)
{
    // __u32 init_map_value = 0;
    int err = 0;
    struct oomk *skel = oomk__open_and_load();

    if (!skel)
    {
        fprintf(stderr, "Failed to open/load BPF obj\n");
        return 1;
    }

    err = oomk__attach(skel);
    if (err)
    {
        fprintf(stderr, "Failed to attach BPF programme\n");
        goto cleanup;
    }

    struct sysinfo info;

    sysinfo(&info);

    printf("user sysinfo: %lu / %lu\n", info.freeram, info.totalram);

    /* Keep the process alive so our probes work */
    for (;;)
        pause();

cleanup:
    oomk__destroy(skel);
    return err;
}