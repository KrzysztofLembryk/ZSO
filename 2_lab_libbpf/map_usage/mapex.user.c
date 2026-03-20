#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "mapex.skel.h"


int main(void)
{
    // __u32 init_map_value = 0;
    int err = 0;
    struct mapex *skel = mapex__open_and_load();

    if (!skel)
    {
        fprintf(stderr, "Failed to open/load BPF obj\n");
        return 1;
    }

    // bpf_map__set_initial_value - must be set before loading a programme that uses
    // given map
    // err = bpf_map__set_initial_value(skel->maps.my_map, &init_map_value, sizeof(init_map_value));
    // if (err)
    // {
    //     fprintf(stderr, "Failed set initial map value to %d\n", init_map_value);
    //     goto cleanup;
    // }

    // err = mapex__load(skel);
    // if (err)
    // {
    //     fprintf(stderr, "Failed to load BPF programme\n");
    //     goto cleanup;
    // }

    err = mapex__attach(skel);
    if (err)
    {
        fprintf(stderr, "Failed to attach BPF programme\n");
        goto cleanup;
    }

    printf("BPF program attached — trigger it by running 'sleep 1' "
           "in another terminal.\n");
    printf("Press Enter after triggering...\n");
    getchar();
    __u32 key;
    __u32 val;
    int is_end = bpf_map__get_next_key(skel->maps.my_map, NULL, &key, sizeof(key));

    /* Read back what the BPF program wrote */
    int map_fd = bpf_map__fd(skel->maps.my_map);

    while (!is_end)
    {
        if (bpf_map_lookup_elem(map_fd, &key, &val))
        {
            fprintf(stderr, "Failed to lookup value in map\n");
            goto cleanup;
        }
        printf("map[%u] = %u  (written by the BPF program)\n", key, val);

        is_end = bpf_map__get_next_key(skel->maps.my_map, &key, &key, sizeof(key));
    }

cleanup:
    mapex__destroy(skel);
    return err;
}