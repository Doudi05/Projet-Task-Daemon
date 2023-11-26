# Projet Task Daemon
 
Le but de ce projet est de réaliser un clone simplifié de cron(8) et at(8), 
permettant de définir des actions à effectuer à intervalle de temps régulier. 
Le projet est à implémenter en langage C.

Le projet se compose de trois programmes principaux :
— un lanceur de daemon générique : launch_daemon (pouvant éventuellement servir pour lancer d’autres daemons)
— le programme exécuté par le daemon : taskd
— un outil en ligne de commande : taskcli

L’outil taskcli permet, entre autres, d’envoyer au programme taskd une commande à exécuter ainsi que sa date de départ et sa période. Une période de 0 indique que la commande ne doit être effectuée qu’une seule fois (à la manière de at(8)). La syntaxe est la suivante : 
./taskcli START PERIOD CMD [ARG]...

Pour envoyer une commande à exécuter au programme taskd, l’outil taskcli utilisera un tube nommé /tmp/tasks.fifo et le signal SIGUSR1 pour réveiller taskd.

Le programme taskd est chargé de l’exécution des différentes commandes. Pour cela, sa tâche principale est de dormir jusqu’à ce qu’une commande doive être exécutée. Il reçoit les commandes à exécuter de l’outil taskcli. L’ensemble courant des commandes à exécuter est placé dans une structure de données dont la taille évolue dynamiquement.

N.B. : Quand une commande qui ne doit être exécutée qu’une seule fois (c’est à dire dont la période est égale à 0) a été exécutée, elle doit être "supprimée" de la liste courante des commandes à exécuter.

Par ailleurs, le programme taskd maintient un fichier texte nommé /tmp/tasks.txt qui contient une description de la liste courante des commandes à exécuter. Le fichier /tmp/tasks.txt contient une ligne par commande à exécuter. Le format du fichier est le suivant : 
num_cmd;start;period;cmd

L’outil taskcli peut être appelé de deux façons :
./taskcli START PERIOD CMD [ARG]...
./taskcli

La première forme permet d’envoyer à taskd une nouvelle commande à exécuter. La seconde forme (sans argument) permet de lire le fichier /tmp/tasks.txt, et d’afficher son contenu.

Lorsqu’il démarre, le programme taskd enregistre son PID dans un fichier texte nommé /tmp/taskd.pid. L’existence ou la non existence de ce fichier permet de savoir si un processus est en train d’exécuter taskd. Ce fichier texte, quand il existe, permet donc de connaître le PID du processus qui exécute taskd, ce qui permettra ensuite de le "contacter" en lui envoyant par exemple un signal.

Écrire une fonction qui permet de tester l’existence d’un processus exécutant taskd, et de lire le cas échéant son PID.
