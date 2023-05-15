#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include "message.h"

#define FIFO_PATH "/tmp/myfifo"

int main()
{
    int fd;
    char **argv;
    char *str = "Hello back!";

    mkfifo(FIFO_PATH, 0666);

    if ((fd = open(FIFO_PATH, O_RDONLY)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    argv = recv_argv(fd);
    if (argv == NULL) {
        perror("recv_argv");
        exit(EXIT_FAILURE);
    }

    int i;
    for (i = 0; argv[i] != NULL; i++) {
        printf("Received argument %d: %s\n", i, argv[i]);
        free(argv[i]);
    }
    free(argv);

    close(fd);

    if ((fd = open(FIFO_PATH, O_WRONLY)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (send_string(fd, str) == -1) {
        perror("send_string");
        exit(EXIT_FAILURE);
    }

    close(fd);

    unlink(FIFO_PATH);

    return 0;
}