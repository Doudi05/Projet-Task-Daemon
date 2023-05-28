#define _POSIX_C_SOURCE 200809L

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
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include "message.h"

#define PID_FILE "/tmp/taskd.pid"
#define TASK_FILE "/tmp/tasks.txt"
#define FIFO_PATH "/tmp/tasks.fifo"

/*
 * Vérifier si le fichier /tmp/taskd.pid existe.
 * Si oui, lire le PID du processus taskd et le retourner.
 * Sinon, retourner -1.
*/
int read_pid() {
    FILE* pidfile = fopen(PID_FILE, "r");
    if (pidfile == NULL) {
        return -1;
    }
    int pid;
    fscanf(pidfile, "%d", &pid);
    fclose(pidfile);
    return pid;
}

void print_usage(char *progname) {
    fprintf(stderr, "Usage: %s START PERIOD CMD [ARG]...\n", progname);
    fprintf(stderr, "Usage: %s\n", progname);
}

/**
 * Déterminer la date de départ et la période en fonction des arguments.
 * Afficher une aide si une erreur est détectée (mauvais nombre d’arguments, arguments erronés).
 * Utiliser strtol(3) pour convertir en entier les chaînes numériques passées en argument et détecter s’il y a une erreur.
 * Envoyer les informations nécessaires (date de départ, période, commande avec ses arguments) via le tube nommé /tmp/tasks.fifo en se servant notamment de libmessage.so.
 * Envoyer le signal SIGUSR1 à taskd pour lui indiquer qu’il va devoir lire des données.
*/
void send_command(int argc, char *argv[]) {
    time_t start;
    int period;
    char *cmd = NULL;

    if (argc == 1) {
        // ./taskcli
        // Lire et afficher le contenu du fichier /tmp/tasks.txt en utilisant un verrou consultatif fcntl(2).
        FILE *taskfile = fopen(TASK_FILE, "r");
        if (taskfile == NULL) {
            perror("fopen");
            exit(1);
        }
        int fd = fileno(taskfile);
        struct flock lock;
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        if (fcntl(fd, F_SETLKW, &lock) == -1) {
            perror("fcntl");
            exit(1);
        }
        char buf[1024];
        while (fgets(buf, sizeof(buf), taskfile) != NULL) {
            printf("%s", buf);
        }
        fclose(taskfile);
    } else if (argc >= 4) {
        // Envoyer le signal SIGUSR1 à taskd pour lui indiquer qu’il va devoir lire des données.
        int pid = read_pid();
        if (pid == -1) {
            fprintf(stderr, "No taskd process running\n");
            exit(1);
        }
        if (kill(pid, SIGUSR1) == -1) {
            perror("kill");
            exit(1);
        }

        // ./taskcli START PERIOD CMD [ARG]...
        // Déterminer la date de départ et la période en fonction des arguments.
        // Afficher une aide si une erreur est détectée (mauvais nombre d’arguments, arguments erronés).
        char *endptr;
        start = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || start < 0) {
            fprintf(stderr, "Invalid start time : %s\n", argv[1]);
            print_usage(argv[0]);
            exit(1);
        }
        period = strtol(argv[2], &endptr, 10);
        if (*endptr != '\0' || period < 0) {
            fprintf(stderr, "Invalid period : %s\n", argv[2]);
            print_usage(argv[0]);
            exit(1);
        }

        size_t total_length = 0;
        for (int i = 3; i < argc; i++) {
            total_length += strlen(argv[i]) + 1;  // +1 for the space separator
        }

        // Allocate memory for the concatenated string
        cmd = malloc(total_length + 1);  // +1 for the null terminator
        if (cmd == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }

        // Concatenate the arguments into cmd
        cmd[0] = '\0';  // Ensure cmd is an empty string
        for (int i = 3; i < argc; i++) {
            strcat(cmd, argv[i]);
            if (i < argc - 1) {
                strcat(cmd, " ");  // Add a space separator between arguments
            }
        }
        
        printf("\n");
        printf("start: %ld\n", start);
        printf("period: %d\n", period);
        printf("cmd: %s\n", cmd);
        
        printf("\n\n");

        printf("\n\n");
        if (period == 0) {
            if (argv[1][0] == '+') {
                if (start == 0) {
                    printf("On souhaite exécuter %s à partir de maintenant\n", cmd);
                    printf("La commande ne sera exécutée qu'une seule fois\n");
                }else {
                    printf("On souhaite exécuter %s en commencant dans %ld secondes\n", cmd, start);
                    printf("La commande ne sera exécutée qu'une seule fois\n");
                }
            }else {
                struct tm *tm = localtime(&start);
                if (tm == NULL) {
                    perror("localtime");
                    exit(1);
                }
                char buf[1024];
                if (strftime(buf, sizeof(buf), "%c", tm) == 0) {
                    fprintf(stderr, "strftime failed\n");
                    exit(1);
                }
                printf("On souhaite exécuter %s le %s\n", cmd, buf);
                printf("La commande ne sera exécutée qu'une seule fois\n");
            }
        } else {
            if (argv[1][0] == '+') {
                if (start == 0) {
                    printf("On souhaite exécuter %s toutes les %d secondes à partir de maintenant\n", cmd, period);
                }else {
                    printf("On souhaite exécuter %s toutes les %d secondes en commençant dans %ld secondes\n", cmd, period, start);
                }
            }else {
                struct tm *tm = localtime(&start);
                if (tm == NULL) {
                    perror("localtime");
                    exit(1);
                }
                char buf[1024];
                if (strftime(buf, sizeof(buf), "%c", tm) == 0) {
                    fprintf(stderr, "strftime failed\n");
                    exit(1);
                }
                printf("On souhaite exécuter %s toutes les %d secondes en commençant le %s\n", cmd, period, buf);
            }
        }
        printf("\n\n");

        // Envoyer les informations nécessaires (date de départ, période, commande avec ses arguments) via le tube nommé /tmp/tasks.fifo
        // Utiliser les fonctions de libmessage.so pour envoyer les informations (send_string, send_argv, recv_string, recv_argv).
        int fd = open(FIFO_PATH, O_WRONLY);
        if (fd == -1) {
            perror("open");
            exit(1);
        }
        if (send_string(fd, argv[1]) == -1) {
            perror("send_string");
            exit(1);
        }
        if (send_string(fd, argv[2]) == -1) {
            perror("send_string");
            exit(1);
        }
        if (send_string(fd, cmd) == -1) {
            perror("send_string");
            exit(1);
        }
        close(fd);
        free(cmd);
    }else {
        print_usage(argv[0]);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    // tester si un processus est en train d’exécuter taskd
    int pid = read_pid();
    if (pid == -1) {
        printf("No taskd process running\n");
    } else {
        printf("taskd process running with PID %d\n", pid);
    }

    // tester l'envoi d'une commande
    send_command(argc, argv);

    return 0;
}