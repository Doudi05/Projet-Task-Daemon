#Write a Makefile for taskd, taskcli and libmessage.so that will build the project.
# remove *.pid files from /tmp

all: taskd taskcli

taskd: taskd.o libmessage.so
	gcc -o taskd taskd.o -L. -lmessage -lpthread

taskcli: taskcli.o libmessage.so
	gcc -o taskcli taskcli.o -L. -lmessage -lpthread

taskd.o: taskd.c
	gcc -c taskd.c -o taskd.o -I.

taskcli.o: taskcli.c
	gcc -c taskcli.c -o taskcli.o -I.

libmessage.so: message.o
	gcc -shared -o libmessage.so message.o

message.o: message.c
	gcc -c -fPIC message.c -o message.o -I.
	
clean:
	rm -f *.o *.so taskd taskcli
	rm -f /tmp/*.pid