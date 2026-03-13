#include <unistd.h>

void writer(int signum) 
{
    write(1, "12", 2);
}
