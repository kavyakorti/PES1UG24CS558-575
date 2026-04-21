#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const size_t chunk = 5 * 1024 * 1024;
    const int rounds = 30;
    char **ptrs = calloc(rounds, sizeof(char *));
    int i;

    if (!ptrs) {
        perror("calloc");
        return 1;
    }

    for (i = 0; i < rounds; i++) {
        ptrs[i] = malloc(chunk);
        if (!ptrs[i]) {
            perror("malloc");
            break;
        }
        memset(ptrs[i], 0xAB, chunk);
        printf("allocated %d MB\n", (i + 1) * 5);
        fflush(stdout);
        sleep(1);
    }

    sleep(5);

    for (i = 0; i < rounds; i++) {
        free(ptrs[i]);
    }
    free(ptrs);

    return 0;
}
