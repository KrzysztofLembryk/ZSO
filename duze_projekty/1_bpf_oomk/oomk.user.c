#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <sys/resource.h>
#include <bpf/bpf.h>
#include <sys/sysinfo.h>
#include <string.h>
#include "oomk.skel.h"

/*
    Instead of always finding path of libc.so.6 by hand, we can do it by checking
    /proc/self/maps, so if this path changes we will have updated path, isntead of
    always updating it manually.
*/
int find_libc_path(char *result, size_t size)
{
    // in terminal to get it run: find "/usr/lib" -name "libc.so.6"

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) 
    {
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) 
    {
        if (strstr(line, "libc.so.6")) 
        {
            char *path = strchr(line, '/');
            if (path) 
            {
                path[strcspn(path, "\n")] = '\0';
                strncpy(result, path, size - 1);
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
}

long find_rand_offset(char *libc_path)
{
    // Getting offset as string
    char bash_cmd[256];
    strcpy(bash_cmd, "nm -D ");
    strcat(bash_cmd, libc_path);
    strcat(bash_cmd, " | grep \"T rand@@\"");

    char buffer[1000];
    FILE *pipe;
    int buffer_len; 

    pipe = popen(bash_cmd, "r");

    if (NULL == pipe) {
        perror("pipe");
        exit(1);
    } 

    fgets(buffer, sizeof(buffer), pipe);
    pclose(pipe);

    buffer_len = strlen(buffer);
    buffer[buffer_len-1] = '\0'; 

    // Extracting hexadecimal offset value
    char offset_str[64];
    size_t offset_str_idx = 0;

    // TODO: add regex that checks if our string is in good format
    // We expect returned value to be in format: 0000000000040790 T rand@@GLIBC_2.2.5
    bool first_non_zero_present = false;
    bool correct_format = false;
    for (int i = 0; i < buffer_len; i++)
    {
        if (offset_str_idx >= sizeof(offset_str) - 1)
        {
            perror("offset_str_idx greater than allowed");
            exit(1);
        }
        if (buffer[i] == ' ')
        {
            continue;
        }
        if (buffer[i] == 'T')
        {
            correct_format = true;
            break;
        }
        if ((buffer[i] == '0' && first_non_zero_present) 
            || (buffer[i] >= '1' && buffer[i] <= '9')
            || (buffer[i] >= 'a' && buffer[i] <= 'f')
        )
        {
            first_non_zero_present = true;
            offset_str[offset_str_idx] = buffer[i];
            offset_str_idx += 1;
        }
    }

    if (!correct_format)
    {
        perror("find_rand_offset:: offset_str is not in expected format\n");
        exit(1);
    }

    // so that strtol works, we need to have null terminator on our string
    offset_str[offset_str_idx] = '\0';

    // in offset_str number is stored as hexadecimal
    long offset = strtol(offset_str, NULL, 16);

    return offset;
}

int main(void)
{
    char libc_path[256];

    if (find_libc_path(libc_path, 256) == -1)
    {
        fprintf(stderr, "Couldn't find libc.so.6 path\n");
        return 1;
    }

    long rand_func_offset = find_rand_offset(libc_path);
    int err = 0;
    struct oomk *skel = oomk__open_and_load();

    if (!skel)
    {
        fprintf(stderr, "Failed to open/load BPF obj\n");
        return 1;
    }

    skel->links.libc_rand_exit = bpf_program__attach_uprobe(
                            skel->progs.libc_rand_exit,
							true /* uretprobe */,
						    -1 /* any pid */,
							libc_path, // points to the executable file of the rand func 
							rand_func_offset);

    err = libbpf_get_error(skel->links.libc_rand_exit);
	if (err) {
		fprintf(stderr, "Failed to attach uprobe: %d\n", err);
		goto cleanup;
	}

    err = oomk__attach(skel);
    if (err)
    {
        fprintf(stderr, "Failed to attach BPF programme\n");
        goto cleanup;
    }

    struct sysinfo info;

    sysinfo(&info);

    /* Keep the process alive so our probes work */
    for (;;)
    {
        sleep(1);
        sysinfo(&info);
    }

cleanup:
    oomk__destroy(skel);
    return err;
}