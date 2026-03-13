#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

int main()
{

    // syscall res: chmod("./f", 01411)
    // int rc = syscall(SYS_chmod, "./f", 777);

    // zapomnielismy dodac 0 przed 777
    int rc = syscall(SYS_chmod, "./f", 0777);

    if (rc == -1)
    {
        return errno;
    }
    return 0;
}