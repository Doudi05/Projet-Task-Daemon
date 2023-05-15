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
    char *str;
    char *argv[] = {"Hello", "world", "!", NULL};

    mkfifo(FIFO_PATH, 0666);

    if ((fd = open(FIFO_PATH, O_WRONLY)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (send_argv(fd, argv) == -1) {
        perror("send_argv");
        exit(EXIT_FAILURE);
    }

    close(fd);

    if ((fd = open(FIFO_PATH, O_RDONLY)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    str = recv_string(fd);
    printf("Received string: %s\n", str);
    free(str);

    close(fd);

    unlink(FIFO_PATH);

    return 0;
}