#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        printf("io_pulse tick %d\n", i);
        fflush(stdout);
        usleep(300000);
    }
    return 0;
}
