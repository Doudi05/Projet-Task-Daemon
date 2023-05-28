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
#define FIFO_PATH "/tmp/tasks.fifo"
#define TASK_FILE "/tmp/tasks.txt"
#define TASK_DIR "/tmp/tasks"

volatile int usr1_receive = 0;
volatile int alrm_receive = 0;

/*
 * Écrire le PID du processus dans le fichier texte /tmp/taskd.pid.
 * Il ne faut pas qu’il y ait plus d’un processus qui exécute ce programme.
 * Si le fichier existe déjà, le programme doit s’arrêter.
*/
void write_pid() {
    if (access(PID_FILE, F_OK) != -1) {
        fprintf(stderr, "Le fichier %s existe déjà.\n", PID_FILE);
        exit(1);
    }
    FILE* pidfile = fopen(PID_FILE, "w");
    if (pidfile == NULL) {
        perror("Unable to open pid file");
        exit(1);
    }
    fprintf(pidfile, "%d", getpid());
    fclose(pidfile);
}

/*
 * Supprimer le fichier /tmp/taskd.pid.
*/
void remove_pid() {
    if (unlink(PID_FILE) == -1) {
        perror("unlink");
        exit(1);
    }
}

/*
 * S’il n’existe pas, créer le tube nommé /tmp/tasks.fifo
 * sinon, s’il existe déjà, ouvrir le tube en lecture/écriture.
*/
void create_fifo() {
    if (access(FIFO_PATH, F_OK) == -1) {
        if (mkfifo(FIFO_PATH, 0777) == -1) {
            perror("mkfifo");
            exit(1);
        }
    }
}

/*
 * S’il n’existe pas, créer le fichier texte /tmp/tasks.txt. Sinon, s’il existe déjà, tronquer le.
*/
void create_task_file() {
    FILE* taskfile = fopen(TASK_FILE, "w");
    if (taskfile == NULL) {
        perror("fopen");
        exit(1);
    }
    fclose(taskfile);
}

/*
 * S’il n’existe pas, créer le répertoire /tmp/tasks.
*/
void create_task_dir() {
    if (mkdir(TASK_DIR, 0777) == -1) {
        perror("mkdir");
        exit(1);
    }
}

/*
 * Le programme taskd est chargé de l’exécution des différentes commandes.
 * Sa tâche principale est de dormir jusqu’à ce qu’une commande doive être exécutée. 
 * Il reçoit les commandes à exécuter de l’outil taskcli. 
 * L’ensemble courant des commandes à exécuter est placé dans une structure de données dont la taille évolue dynamiquement.
 * Définir une structure de données pour chaque enregistrement (ou commande) de la liste.
*/
typedef struct {
    int num_cmd;
    time_t start;
    int period;
    char* cmd;
} Task;

/*
 * Définir un tableau dynamique pour la liste des commandes à exécuter avec les opérations suivantes :
    * - Ajouter une commande à la liste
    * - Supprimer une commande de la liste
    * - Récupérer la commande suivante à exécuter
    * - Récupérer la commande suivante à exécuter et la supprimer de la liste
*/
typedef struct {
    Task* tasks;
    int size;
    int capacity;
} TaskList;

TaskList task_list; // Déclaration globale de la liste des tâches

/**
 * @brief Initialize a TaskList
*/
void init_task_list(TaskList* list) {
    list->tasks = NULL;
    list->size = 0;
    list->capacity = 0;
}

/**
 * @brief Add a task to the list
 * @param list The list to add the task to
 * @param task The task to add
*/
void add_task_to_list(TaskList* list, Task task) {
    if (list->size == list->capacity) {
        list->capacity = list->capacity == 0 ? 1 : list->capacity * 2;
        list->tasks = realloc(list->tasks, list->capacity * sizeof(Task));
    }
    list->tasks[list->size] = task;
    list->size++;
}

/**
 * @brief Remove a task from the list
 * @param list The list to remove the task from
 * @param task The task to remove
 * @return 1 if the task was removed, 0 otherwise
*/
int remove_task_from_list(TaskList* list, Task* task) {
    for (int i = 0; i < list->size; i++) {
        if (list->tasks[i].num_cmd == task->num_cmd) {
            for (int j = i; j < list->size - 1; j++) {
                list->tasks[j] = list->tasks[j + 1];
            }
            list->size--;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Get the next task to execute
 * @param list The list to get the task from
 * @return The next task to execute
*/
Task get_next_task(TaskList* list, int index) {
    if (index >= 0 && index < list->size) {
        return list->tasks[index];
    }
    return (Task) {0};
}

/**
 * @brief Free dynamically allocated memory in a TaskList
 * @param list The list to free
 * @param free_tasks Whether to free the tasks
*/
void free_task_list(TaskList* list) {
    if (list->tasks != NULL) {
        free(list->tasks);
    }
    list->size = 0;
    list->capacity = 0;
}

/**
 * @brief Display a TaskList
 * increment num_cmd by 1 every time a task is added to the list
 * @param list The list to display
*/
void display_task_list(TaskList* list) {
    printf("Task list:\n");
    for (int i = 0; i < list->size; i++) {
        printf("%d;%ld;%d;%s\n", list->tasks[i].num_cmd, list->tasks[i].start, list->tasks[i].period, list->tasks[i].cmd);
    }
}

/*
 * Ajouter une commande au fichier /tmp/tasks.txt.
 * en utilisant un verrou consultatif qui doit être posé et retiré avec fcntl(2)
 * Le format du fichier est le suivant : num_cmd;start;period;cmd
*/
void add_task(Task task) {
    // open the file
    int fd;
    if ((fd = open(TASK_FILE, O_WRONLY | O_APPEND)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // lock the file
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_END;
    lock.l_start = 0;
    lock.l_len = 0;
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    // write the task to the file
    char* task_str = malloc(100 * sizeof(char));
    sprintf(task_str, "%d;%ld;%d;%s\n", task.num_cmd, task.start, task.period, task.cmd);
    if (write(fd, task_str, strlen(task_str)) == -1) {
        perror("write");
        exit(EXIT_FAILURE);
    }
    free(task_str);

    // unlock the file
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    // close the file
    if (close(fd) == -1) {
        perror("close");
        exit(EXIT_FAILURE);
    }
}

/**
 * lire la nouvelle commande envoyée par l’outil taskcli à travers le tube nommé quand le signal SIGUSR1 est reçu.
 * La commande est envoyée sous la forme d’une chaîne de caractères de la forme : num_cmd;start;period;cmd
 * Ajouter la commande à la liste des commandes à exécuter.
 * Ajouter la commande au fichier /tmp/tasks.txt.
 * @param list The list to add the task to
 * @param task The task to add
*/
void read_command(TaskList* list, Task* task) {
    // open the fifo
    int fd;
    if ((fd = open(FIFO_PATH, O_RDONLY)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // Utiliser les fonctions de libmessage.so pour lire les informations (send_string, send_argv, recv_string, recv_argv) de la commande envoyée par l’outil taskcli.

    // num_cmd is incremented by 1 every time a task is added to the list
    task->num_cmd = list->size;

    // Receive start time
    char* start_str = recv_string(fd);
    if (start_str == NULL) {
        fprintf(stderr, "Failed to receive start time\n");
        exit(1);
    }
    task->start = strtoul(start_str, NULL, 10);
    free(start_str);

    // Receive period
    char* period_str = recv_string(fd);
    if (period_str == NULL) {
        fprintf(stderr, "Failed to receive period\n");
        exit(1);
    }
    task->period = atoi(period_str);
    free(period_str);
    
    task->cmd = recv_string(fd);
    if (task->cmd == NULL) {
        perror("recv_string");
        exit(EXIT_FAILURE);
    }

    // close the fifo
    close(fd);

    // add the task to the list
    add_task_to_list(list, *task);

    // add the task to the file
    add_task(*task);

    // print the task
    printf("////////////////////////////////////////////////////////////////\n");
    printf("Received task: %d;%ld;%d;%s\n", task->num_cmd, task->start, task->period, task->cmd);
}

/**
 * Check if there is a task or more to execute and execute them
 * For each task, do the following :
 * stdin is redirected to /dev/null
 * stdout is redirected to /tmp/tasks/<num_cmd>.out
 * stderr is redirected to /tmp/tasks/<num_cmd>.err
 * The task is executed with execvp
 * @param list The list of tasks
 * @param task The task to execute
*/
void execute_tasks(TaskList* list, Task* task) {
    // Check if there is a task to execute
    if (list->size == 0) {
        printf("No task to execute\n");
        return;
    }

    // Execute the task
    printf("Executing task %d: %s (start: %ld, period: %d)\n", task->num_cmd, task->cmd, task->start, task->period);
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char* cmd = strtok(task->cmd, " ");
        int max_args = 10;
        char** args = malloc((max_args + 1) * sizeof(char*));
        if (args == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        int i = 0;
        while (cmd != NULL) {
            if (i >= max_args) {
                max_args *= 2;  // Double the array size if more arguments are needed
                char** temp = realloc(args, (max_args + 1) * sizeof(char*));
                if (temp == NULL) {
                    perror("realloc");
                    free(args);
                    exit(EXIT_FAILURE);
                }
                args = temp;
            }
            args[i] = cmd;
            cmd = strtok(NULL, " ");
            i++;
        }
        args[i] = NULL;
        execvp(args[0], args);
        perror("execvp");
        free(args);
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Task %d exited with status %d\n\n", task->num_cmd, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Task %d killed by signal %d\n\n", task->num_cmd, WTERMSIG(status));
        }
    }

    // // Redirect stdin to /dev/null
    // int devnull = open("/dev/null", O_WRONLY);
    // if (devnull == -1) {
    //     perror("open");
    //     exit(EXIT_FAILURE);
    // }
    // if (dup2(devnull, STDIN_FILENO) == -1) {
    //     perror("dup2");
    //     exit(EXIT_FAILURE);
    // }
    // close(devnull);

    // // Redirect stdout to /tmp/tasks/<num_cmd>.out
    // char out_path[PATH_MAX];
    // snprintf(out_path, sizeof(out_path), "%s/%d.out", TASK_DIR, task->num_cmd);
    // int out = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    // if (out == -1) {
    //     perror("open");
    //     exit(EXIT_FAILURE);
    // }
    // if (dup2(out, STDOUT_FILENO) == -1) {
    //     perror("dup2");
    //     exit(EXIT_FAILURE);
    // }
    // close(out);

    // // Redirect stderr to /tmp/tasks/<num_cmd>.err
    // char err_path[PATH_MAX];
    // snprintf(err_path, sizeof(err_path), "%s/%d.err", TASK_DIR, task->num_cmd);
    // int err = open(err_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    // if (err == -1) {
    //     perror("open");
    //     exit(EXIT_FAILURE);
    // }
    // if (dup2(err, STDERR_FILENO) == -1) {
    //     perror("dup2");
    //     exit(EXIT_FAILURE);
    // }
    // close(err);
}

/**
 * Run through the list of tasks to get the earliest start time and return the time to wait before executing it
 * repeat every period seconds after that (if period > 0)
 * Exemple : ./taskcli +9 4 pwd -> pwd will be executed in 9 seconds and every 4 seconds after that
 * @param list The list of tasks
*/
void waitForStart(TaskList* list) {
    // Check if there is a task to execute
    if (list->size == 0) {
        return;
    }

    // Get the earliest start time
    time_t start = 0;
    for (int i = 0; i < list->size; i++) {
        Task task = list->tasks[i];
        if (start == 0 || task.start < start) {
            start = task.start;
        }
    }
    printf("Alarm set to %ld\n", start);
    alarm(start);
}

/**
 * When the start alarm is triggered, reset a new alarm to the period of the task
 * if period == 0, print the task and remove it from the list
 * if period > 0, reset the alarm to every period seconds
 * else, print an error
 * @param list The list of tasks
 * @param task The task to execute
*/
int waitForPeriod(TaskList* list, Task* task) {
    // Check if there is a task to execute
    if (list->size == 0) {
        return 0;
    }

    // Get the task to execute
    Task* task_to_execute = NULL;
    for (int i = 0; i < list->size; i++) {
        Task* t = &list->tasks[i];
        if (t->num_cmd == task->num_cmd) {
            task_to_execute = t;
            break;
        }
    }

    // Check if the task was found
    if (task_to_execute == NULL) {
        printf("Task not found\n");
        return 0;
    }

    // Check if the task is periodic
    if (task_to_execute->period == 0) {
        // execute the task
        execute_tasks(list, task_to_execute);

        // remove the task from the list
        remove_task_from_list(list, task_to_execute);

        return 0;
    } else if (task_to_execute->period > 0) {
        // execute the task
        execute_tasks(list, task_to_execute);

        // reset the alarm
        printf("Alarm set to %d\n", task_to_execute->period);
        alarm(task_to_execute->period);
        
        return 1;
    } else {
        printf("Invalid period\n");
        return 0;
    }
}

void handSIGUSR1(int sig) {
    if (sig == SIGUSR1) {
        usr1_receive = 1;
    }
}

void handSIGALRM(int sig) {
    if (sig == SIGALRM) {
        alrm_receive = 1;
    }
}

void handSIGCHLD() {
    // Signal handler for SIGCHLD
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            printf("Child process %d exited with status %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Child process %d terminated by signal %d\n", pid, WTERMSIG(status));
        }
    }

    // Check if all zombie processes are eliminated
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // Continue cleaning up zombies
    }
}

void handSIGINT() {
    // Signal handler for SIGINT
    // Cleanup tasks
    printf("Received SIGINT. Terminating...\n");
    remove(PID_FILE);
    // Function to release dynamically allocated memory
    free_task_list(&task_list);

    // Terminate and eliminate remaining child processes
    killpg(getpid(), SIGTERM);
    exit(0);
}

void handSIGQUIT() {
    // Signal handler for SIGQUIT
    // Cleanup tasks
    printf("Received SIGQUIT. Terminating...\n");
    remove(PID_FILE);
    // Function to release dynamically allocated memory
    free_task_list(&task_list);

    // Terminate and eliminate remaining child processes
    killpg(getpid(), SIGTERM);
    exit(0);
}

void handSIGTERM() {
    // Signal handler for SIGTERM
    // Cleanup tasks
    printf("Received SIGTERM. Terminating...\n");
    remove(PID_FILE);
    // Function to release dynamically allocated memory
    free_task_list(&task_list);

    // Terminate and eliminate remaining child processes
    killpg(getpid(), SIGTERM);
    exit(0);
}

int main(){
    // Set up the signal handler for SIGUSR1
    struct sigaction sa;
    sa.sa_handler = handSIGUSR1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Set up the signal handler for SIGALRM
    struct sigaction sa_alrm;
    sa_alrm.sa_handler = handSIGALRM;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = 0;
    if (sigaction(SIGALRM, &sa_alrm, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Set up the signal handler for SIGCHLD
    struct sigaction sa_chld;
    sa_chld.sa_handler = handSIGCHLD;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Set up the signal handler for SIGINT
    struct sigaction sa_int;
    sa_int.sa_handler = handSIGINT;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Set up the signal handler for SIGQUIT
    struct sigaction sa_quit;
    sa_quit.sa_handler = handSIGQUIT;
    sigemptyset(&sa_quit.sa_mask);
    sa_quit.sa_flags = 0;
    if (sigaction(SIGQUIT, &sa_quit, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Set up the signal handler for SIGTERM
    struct sigaction sa_term;
    sa_term.sa_handler = handSIGTERM;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Write the PID of the process in the file /tmp/taskd.pid
    write_pid();

    // Create the named pipe /tmp/tasks.fifo
    create_fifo();

    // Create the file /tmp/tasks.txt
    create_task_file();

    // //create the directory /tmp/taskd
    create_task_dir();

    // Initialize the structure of tasks and the list of tasks
    Task task;
    task.start = 0;
    task.period = 0;
    task.cmd = NULL;

    init_task_list(&task_list);

    // When sigusr1 is received, read the command received from the named pipe
    while (1) {
        // Wait for a signal
        pause();
        if (usr1_receive) {
            // Read the command received from the named pipe
            read_command(&task_list, &task);

            // display the list of tasks
            display_task_list(&task_list);

            // Set the alarm to the time to wait before the next task to execute
            waitForStart(&task_list);

            usr1_receive = 0;
        }

        // When the alarm is received, execute the next task
        if (alrm_receive) {
            printf("AAAAAAALLLLLAAAAAARRRRRMMMMMMMMM\n");

            // Execute the task and reset the alarm
            waitForPeriod(&task_list, &task);

            // display the list of tasks
            display_task_list(&task_list);

            alrm_receive = 0;
        }
    }

    // // Initialize the list of tasks
    // TaskList task_list;
    // init_task_list(&task_list);

    // printf("MAIN5\n");

    // sigset_t mask;
    // sigemptyset(&mask);
    // sigaddset(&mask, SIGUSR1);
    // sigaddset(&mask, SIGALRM);

    // printf("MAIN6\n");

    // // Wait for commands to be received
    // while (1) {
    //     // attente de réception d’un signal
    //     sigsuspend(&mask);

    //     if (usr1_receive) {
    //         // Read the command received from the named pipe
    //         read_command(&task_list);
    //         usr1_receive = 0;
    //     }
    // }

    // return 0;

    // printf("MAIN7\n");
}



    // while (1) {
    //     // Read the command received from the named pipe
    //     read_command();

    //     // If the command is "add", add the task to the list of tasks
    //     if (strcmp(buf, "add") == 0) {
    //         // Read the task to be added from the named pipe
    //         read_command();

    //         // Parse the task to be added
    //         Task* task = parse_task(buf);

    //         // Add the task to the list of tasks
    //         add_task(&task_list, task);

    //         // Add the task to the file /tmp/tasks.txt
    //         add_task_to_file(task);
    //     }

    //     // If the command is "remove", remove the task from the list of tasks
    //     if (strcmp(buf, "remove") == 0) {
    //         // Read the task to be removed from the named pipe
    //         read_command();

    //         // Parse the task to be removed
    //         Task* task = parse_task(buf);

    //         // Remove the task from the list of tasks
    //         remove_task(&task_list, task);
    //     }

    //     // If the command is "list", list the tasks
    //     if (strcmp(buf, "list") == 0) {
    //         // List the tasks
    //         list_tasks(&task_list);
    //     }

    //     // If the command is "quit", quit the program
    //     if (strcmp(buf, "quit") == 0) {
    //         // Remove the file /tmp/taskd.pid
    //         remove_pid();

    //         // Quit the program
    //         exit(0);
    //     }
    // }
















    void execute_tasks(TaskList* list, Task* task) {
    // Check if there is a task to execute
    if (list->size == 0) {
        printf("No task to execute\n");
        return;
    }

    // Execute the task
    printf("Executing task %d: %s (start: %ld, period: %d)\n", task->num_cmd, task->cmd, task->start, task->period);
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char* cmd = strtok(task->cmd, " ");
        int max_args = 10;
        char** args = malloc((max_args + 1) * sizeof(char*));
        if (args == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        int i = 0;
        while (cmd != NULL) {
            if (i >= max_args) {
                max_args *= 2;  // Double the array size if more arguments are needed
                char** temp = realloc(args, (max_args + 1) * sizeof(char*));
                if (temp == NULL) {
                    perror("realloc");
                    free(args);
                    exit(EXIT_FAILURE);
                }
                args = temp;
            }
            args[i] = cmd;
            cmd = strtok(NULL, " ");
            i++;
        }
        args[i] = NULL;
        execvp(args[0], args);
        perror("execvp");
        free(args);
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Task %d exited with status %d\n\n", task->num_cmd, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Task %d killed by signal %d\n\n", task->num_cmd, WTERMSIG(status));
        }
    }

    // Redirect stdin to /dev/null
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (dup2(devnull, STDIN_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    close(devnull);

    // Redirect stdout to /tmp/tasks/<num_cmd>.out
    char out_path[PATH_MAX];
    snprintf(out_path, sizeof(out_path), "%s/%d.out", TASK_DIR, task->num_cmd);
    int out = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (out == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (dup2(out, STDOUT_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    close(out);

    // Redirect stderr to /tmp/tasks/<num_cmd>.err
    char err_path[PATH_MAX];
    snprintf(err_path, sizeof(err_path), "%s/%d.err", TASK_DIR, task->num_cmd);
    int err = open(err_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (err == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (dup2(err, STDERR_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    close(err);
}