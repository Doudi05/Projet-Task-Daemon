#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

int main(int argc, char *argv[]) {
    time_t t = time(NULL);
    printf("%ld\n", t);
    return EXIT_SUCCESS;
}