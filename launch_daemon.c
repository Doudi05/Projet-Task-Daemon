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

//  il faut lancer le programme taskd en utilisant launch_daemon : ./launch_daemon ABSOLUTE_PATH_TO_TASKD
/**
 * Implémenter la méthode du double fork(2) pour lancer le daemon. Pour rappel, voici les actions à effectuer :
 * Le processus principal crée un nouveau processus, et attend la fin de son fils.
 * Le processus fils devient leader de session via setsid(2).
 * Le processus fils crée un nouveau processus, et se termine en appelant exit(2), rendant le petit-fils orphelin.
 * Le processus petit-fils n’est pas leader de session, ce qui l’empêche d’être rattaché à un terminal.
 * Le processus principal se termine, laissant le petit-fils être le daemon.
 * Changer la configuration du daemon :
 * Changer le répertoire courant à / via chdir(2)
 * Changer le umask à 0 via umask(2)
 * Fermer tous les descripteurs de fichiers standard
*/

int main(int argc, char *argv[]) {
    pid_t pid, sid;

    // Création du premier processus fils
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }

    // Terminer le processus parent
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // Devenir leader de session
    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    // Changer le répertoire courant à /
    if (chdir("/") < 0) {
        exit(EXIT_FAILURE);
    }

    // Changer le umask à 0
    umask(0);

    // Fermer tous les descripteurs de fichiers standard
    int fd;
    for (fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }

    // Exécuter le programme taskd
    execv(argv[1], argv+1);

    // Si execv échoue, nous atteignons cette ligne
    exit(EXIT_FAILURE);
}