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
#include "message.h"

#define PID_FILE "/tmp/taskd.pid"
#define FIFO_PATH "/tmp/tasks.fifo"
#define TASK_FILE "/tmp/tasks.txt"

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
*/
void create_fifo() {
    if (mkfifo(FIFO_PATH, 0666) == -1) {
        perror("mkfifo");
        exit(1);
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
    if (mkdir("/tmp/tasks", 0777) == -1) {
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
    char* cmd_args;
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
 * @param index The index of the task to remove
*/
void remove_task_from_list(TaskList* list, int index) {
    for (int i = index; i < list->size - 1; i++) {
        list->tasks[i] = list->tasks[i + 1];
    }
    list->size--;
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
    return (Task) {0, 0, 0, NULL};
}

/**
 * @brief Free the memory used by a TaskList
 * @param list The list to free
 * @param free_tasks Whether to free the tasks
*/
void free_task_list(TaskList* list, int free_tasks) {
    if (free_tasks) {
        for (int i = 0; i < list->size; i++) {
            free(list->tasks[i].cmd_args);
        }
    }
    free(list->tasks);
}


/*
 * Ajouter une commande au fichier /tmp/tasks.txt.
 * Le format du fichier est le suivant : num_cmd;start;period;cmd_args
*/
void add_task(Task task) {
    FILE* taskfile = fopen(TASK_FILE, "a");
    if (taskfile == NULL) {
        perror("fopen");
        exit(1);
    }
    fprintf(taskfile, "%d;%ld;%d;%s\n", task.num_cmd, task.start, task.period, task.cmd_args);
    fclose(taskfile);
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

// lire la nouvelle commande envoyée par l’outil taskcli à travers le tube nommé quand le signal SIGUSR1 est reçu.
void read_command(TaskList* list) {
    // Open the FIFO
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    // Read the command
    char buf[BUFSIZ];
    int n = read(fd, buf, BUFSIZ);
    if (n == -1) {
        perror("read");
        exit(1);
    }
    buf[n] = '\0';

    // Close the FIFO
    if (close(fd) == -1) {
        perror("close");
        exit(1);
    }

    // Parse the command
    Task task;
    task.num_cmd = list->size;
    task.start = time(NULL);
    task.period = 0;
    task.cmd_args = malloc((n + 1) * sizeof(char));
    strcpy(task.cmd_args, buf);

    // Add the task to the list
    add_task_to_list(list, task);
    add_task(task);
}

int main(int argc, char* argv[]){
    printf("1234");
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

    // Write the PID of the process in the file /tmp/taskd.pid
    write_pid();

    // Create the named pipe /tmp/tasks.fifo
    create_fifo();

    // Create the file /tmp/tasks.txt
    create_task_file();

    // Initialize the list of tasks
    TaskList task_list;
    init_task_list(&task_list);

    printf("wawiting...!");

    // Wait for commands to be received
    while (1) {
        printf("wawiting...!");
        pause();
        if (usr1_receive) {
            usr1_receive = 0;
            read_command(&task_list);
        }
        sleep(1);
    }

    return 0;





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
}













































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
#include "message.h"

#define PID_FILE "/tmp/taskd.pid"
#define FIFO_PATH "/tmp/tasks.fifo"
#define TASK_FILE "/tmp/tasks.txt"

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
    if (mkdir("/tmp/tasks", 0777) == -1) {
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
 * @param index The index of the task to remove
*/
void remove_task_from_list(TaskList* list, int index) {
    for (int i = index; i < list->size - 1; i++) {
        list->tasks[i] = list->tasks[i + 1];
    }
    list->size--;
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
 * @brief Free the memory used by a TaskList
 * @param list The list to free
 * @param free_tasks Whether to free the tasks
*/
void free_task_list(TaskList* list, int free_tasks) {
    if (free_tasks) {
        for (int i = 0; i < list->size; i++) {
            free(list->tasks[i].cmd);
        }
    }
    free(list->tasks);
    list->tasks = NULL;
    list->size = 0;
    list->capacity = 0;
}

/**
 * @brief Display a TaskList
 * @param list The list to display
*/
void display_task_list(TaskList* list) {
    printf("Task list:\n");
    for (int i = 0; i < list->size; i++) {
        Task task = list->tasks[i];
        printf("%d: %ld %d %s\n", i, task.start, task.period, task.cmd);
    }
}

/*
 * Ajouter une commande au fichier /tmp/tasks.txt.
 * Le format du fichier est le suivant : num_cmd;start;period;cmd
*/
void add_task(Task task) {
    FILE* taskfile = fopen(TASK_FILE, "a");
    if (taskfile == NULL) {
        perror("fopen");
        exit(1);
    }
    fprintf(taskfile, "%ld;%d;%s\n", task.start, task.period, task.cmd);
    fclose(taskfile);
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
    printf("Received task: %ld;%d;%s\n\n", task->start, task->period, task->cmd);
}

// /**
//  * run through the list of tasks and compute the waiting time before the next task starts
//  * @param list The list of tasks
//  * @return The waiting time before the next task starts
// */
// void compute_start_time(TaskList* list) {
//     time_t now = time(NULL);
//     for (int i = 0; i < list->size; i++) {
//         if (list->tasks[i].start < now) {
//             list->tasks[i].start += (now - list->tasks[i].start) / list->tasks[i].period * list->tasks[i].period + list->tasks[i].period;
//         }
//     }
// }

// /**
//  * Set the alarm to the next task to execute in the list of tasks (the one with the smallest start time)
//  * @param list The list of tasks
//  * @return The waiting time before the next task starts
// */
// void set_alarm(TaskList* list) {
//     time_t now = time(NULL);
//     time_t min_start = now + 1000000000;
//     for (int i = 0; i < list->size; i++) {
//         if (list->tasks[i].start < min_start) {
//             min_start = list->tasks[i].start;
//         }
//     }
//     alarm(min_start - now);
// }

void execute_tasks(TaskList* task_list, Task* task) {
    time_t now = time(NULL);
    for (int i = 0; i < task_list->size; i++) {
        task = &task_list->tasks[i];
        time_t next_execution = task->start + task->period;

        if (next_execution <= now) {
            // Task is due to be executed
            printf("Executing task %d: %s (start: %ld, period: %d)\n", i, task->cmd, task->start, task->period);
            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                exit(1);
            } else if (pid == 0) {
                // Child process
                char* cmd = strtok(task->cmd, " ");
                char* args[10];
                int i = 0;
                while (cmd != NULL) {
                    args[i] = cmd;
                    cmd = strtok(NULL, " ");
                    i++;
                }
                args[i] = NULL;
                execvp(args[0], args);
                perror("execvp");
                exit(1);
            } else {
                // Parent process
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    printf("Task %d exited with status %d\n\n", i, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("Task %d killed by signal %d\n\n", i, WTERMSIG(status));
                }
            }

            // Update the start time of the task
            task->start += task->period;
        }
    }
}

void set_next_alarm(TaskList* task_list, Task* task) {
    time_t now = time(NULL);
    time_t min_wait = -1; // Initialize to a large value
    int has_pending_task = 0;

    for (int i = 0; i < task_list->size; i++) {
        task = &task_list->tasks[i];
        time_t next_execution = task->start + task->period;

        if (next_execution <= now) {
            // Task is due to be executed
            has_pending_task = 1;
            break;
        }

        time_t wait_time = next_execution - now;
        if (wait_time < min_wait) {
            min_wait = wait_time;
        }
    }

    if (has_pending_task) {
        // There is a task to execute immediately
        printf("ALARM: Executing task(s) immediately.\n");
        // Call the function to execute the task(s)
        execute_tasks(task_list, task);
    } else if (min_wait != -1) {
        // Set the next alarm
        printf("Next alarm in %ld seconds.\n", min_wait);
        alarm(min_wait);
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

    // Write the PID of the process in the file /tmp/taskd.pid
    write_pid();

    // Create the named pipe /tmp/tasks.fifo
    create_fifo();

    // Create the file /tmp/tasks.txt
    create_task_file();

    // //create the directory /tmp/taskd
    // create_directory();

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
            
            // Set the next alarm
            // set_next_alarm(&task_list, &task);

            //send sigalrm
            kill(getpid(), SIGALRM);

            usr1_receive = 0;
        }

        // When the alarm is received, execute the next task
        if (alrm_receive) {
            if (task_list.size > 0) {
                printf("ALARME\n");
            }
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




















    /**
 * A la réception du signal SIGALRM, vérifier qu’il y a bien une (ou plusieurs) action(s) à effectuer et les lancer.
 * Pour chaque action, on fera les redirections indiquées ci-dessous :
 * - stdin : redirection vers /dev/null
 * - stdout : redirection vers /tmp/tasks/<num_cmd>.out
 * - stderr : redirection vers /tmp/tasks/<num_cmd>.err
 * Les deux fichiers de sortie seront ouverts en mode ajout en fin de fichier.
 * @param list The list of tasks
 * @param task The task to execute
*/
void execute_tasks(TaskList* list, Task* task) {
    // when the alarm is triggered, check if there is a task or more to execute and execute them
    for (int i = 0; i < list->size; i++) {
        Task task = list->tasks[i];
        if (task.start <= time(NULL)) {
            // execute the task
            execute_task(&task);
            // remove the task from the list
            remove_task_from_list(list, task);
        }
    }
    // open the output files
    char out_file[100];
    char err_file[100];
    sprintf(out_file, "%s/%d.out", TASK_DIR, task->num_cmd);
    sprintf(err_file, "%s/%d.err", TASK_DIR, task->num_cmd);
    int out_fd = open(out_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    int err_fd = open(err_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (out_fd == -1 || err_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // redirect stdin to /dev/null
    int devnull_fd = open("/dev/null", O_RDONLY);
    if (devnull_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (dup2(devnull_fd, STDIN_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    close(devnull_fd);

    // redirect stdout and stderr to the output files
    if (dup2(out_fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    if (dup2(err_fd, STDERR_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    close(out_fd);
    close(err_fd);

    // execute the command
    char* argv[3];
    argv[0] = task->cmd;
    argv[1] = NULL;
    execvp(argv[0], argv);
    perror("execvp");
    exit(EXIT_FAILURE);
}