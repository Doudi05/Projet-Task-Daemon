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

int send_string(int fd, char *str) {
    int len = strlen(str);
    if (write(fd, &len, sizeof(int)) != sizeof(int)) {
        return -1;
    }
    if (write(fd, str, len) != len) {
        return -1;
    }
    return 0;
}

char *recv_string(int fd) {
    int len;
    if (read(fd, &len, sizeof(int)) != sizeof(int)) {
        return NULL;
    }
    char *str = malloc(len + 1);
    if (read(fd, str, len) != len) {
        free(str);
        return NULL;
    }
    str[len] = '\0';
    return str;
}

int send_argv(int fd, char *argv[]) {
    int i;
    for (i = 0; argv[i] != NULL; i++) {
        if (send_string(fd, argv[i]) == -1) {
            return -1;
        }
    }
    if (send_string(fd, "") == -1) {
        return -1;
    }
    return 0;
}

char **recv_argv(int fd) {
    char **argv = malloc(sizeof(char *));
    int i = 0;
    while (1) {
        char *arg = recv_string(fd);
        if (arg == NULL) {
            break;
        }
        argv = realloc(argv, (i + 2) * sizeof(char *));
        argv[i] = arg;
        argv[i + 1] = NULL;
        i++;
    }
    return argv;
}

// int main(int argc, char *argv[]) {
//     int fd[2];
//     if (pipe(fd) == -1) {
//         perror("pipe");
//         return EXIT_FAILURE;
//     }
//     pid_t pid = fork();
//     if (pid == -1) {
//         perror("fork");
//         return EXIT_FAILURE;
//     }
//     if (pid == 0) {
//         close(fd[1]);
//         char **argv = recv_argv(fd[0]);
//         close(fd[0]);
//         if (argv == NULL) {
//             perror("recv_argv");
//             return EXIT_FAILURE;
//         }
//         execvp(argv[0], argv);
//         perror("execvp");
//         return EXIT_FAILURE;
//     }
//     close(fd[0]);
//     if (send_argv(fd[1], argv + 1) == -1) {
//         perror("send_argv");
//         return EXIT_FAILURE;
//     }
//     close(fd[1]);
//     int status;
//     if (waitpid(pid, &status, 0) == -1) {
//         perror("waitpid");
//         return EXIT_FAILURE;
//     }
//     if (WIFEXITED(status)) {
//         return WEXITSTATUS(status);
//     }
//     return EXIT_FAILURE;
// }




/////////2eme version//////////////////

// char **recv_argv(int fd) {
//     char **argv = NULL;
//     int argc = 0;
//     while (1) {
//         char *arg = recv_string(fd);
//         if (arg == NULL) {
//             break;
//         }
//         argc++;
//         argv = realloc(argv, argc * sizeof(char *));
//         argv[argc - 1] = arg;
//     }
//     argv = realloc(argv, (argc + 1) * sizeof(char *));
//     argv[argc] = NULL;
//     return argv;
// }