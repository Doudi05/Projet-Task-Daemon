if (mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            perror("mkfifo");
            exit(1);
        }
        // Ouvrir le tube en écriture.
        int fd = open(FIFO_PATH, O_WRONLY);
        if (fd == -1) {
            perror("open");
            exit(1);
        }
        task_t task;
        task.start = start;
        task.period = period;
        task.cmd = argv[3];
        task.args = argv + 3;
        task.argc = argc - 3;
        if (write(fd, &task, sizeof(task)) == -1) {
            perror("write");
            exit(1);
        }
        close(fd);

        // Envoyer le signal SIGUSR1 à taskd pour lui indiquer qu’il va devoir lire des données.
        int pid = read_pid();
        if (pid == -1) {
            printf("No taskd process running\n");
        } else {
            if (kill(pid, SIGUSR1) == -1) {
                perror("kill");
                exit(1);
            }
        }

















        typedef struct {
    Task* tasks;
    int size;
    int capacity;
    int front;
    int rear;
} TaskQueue;

/**
 * Créer une nouvelle file d'attente avec une capacité donnée.
*/
TaskQueue* createTaskQueue(int capacity) {
    TaskQueue* queue = (TaskQueue*) malloc(sizeof(TaskQueue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->tasks = (Task*) malloc(queue->capacity * sizeof(Task));
    return queue;
}

/**
 * Vérifie si la file d'attente est vide.
*/
int isTaskQueueEmpty(TaskQueue* queue) {
    return (queue->size == 0);
}

/**
 * Vérifie si la file d'attente est pleine.
*/
int isTaskQueueFull(TaskQueue* queue) {
    return (queue->size == queue->capacity);
}

/**
 * Ajouter une commande à la file d'attente.
*/
void enqueue(TaskQueue* queue, Task task) {
    if (isTaskQueueFull(queue)) {
        return;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->tasks[queue->rear] = task;
    queue->size = queue->size + 1;
}

/**
 * Retirer une commande de la file d'attente.
*/
Task dequeue(TaskQueue* queue) {
    if (isTaskQueueEmpty(queue)) {
        Task task;
        task.num_cmd = -1;
        return task;
    }
    Task task = queue->tasks[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size = queue->size - 1;
    return task;
}

/**
 * Supprime toutes les commandes de la file d'attente.
*/
void clearTaskQueue(TaskQueue* queue) {
    while (!isTaskQueueEmpty(queue)) {
        dequeue(queue);
    }
}

/**
 * Supprime la file d'attente et libère la mémoire allouée pour stocker les commandes.
*/
void destroyTaskQueue(TaskQueue* queue) {
    clearTaskQueue(queue);
    free(queue->tasks);
    free(queue);
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
    fprintf(taskfile, "%d;%ld;%d;%s", task.num_cmd, task.start, task.period, task.cmd);
    for (int i = 0; task.args[i] != NULL; i++) {
        fprintf(taskfile, " %s", task.args[i]);
    }
    fprintf(taskfile, "\n");
    fclose(taskfile);
}





void read_command() {
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    char buf[BUFSIZ];
    int n = read(fd, buf, BUFSIZ);
    if (n == -1) {
        perror("read");
        exit(1);
    }
    buf[n] = '\0';

    // Parse the command
    char* cmd_args = strtok(buf, " ");
    int period = atoi(strtok(NULL, " "));
    if (period == 0) {
        perror("atoi");
        exit(1);
    }

    // Add the command to the list
    Task task = {task.num_cmd, time(NULL), period, cmd_args};
    add_task_to_list(&, task);

    // Add the command to the file
    add_task(task);

    close(fd);
}