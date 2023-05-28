CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -I. -O2 -g
LDFLAGS = -L. -lmessage -Wl,-rpath=$(shell pwd) -g
TARGETS = taskd taskcli
OBJ_FILES = taskd.o taskcli.o
LIBRARY = libmessage.so

all: $(TARGETS)

$(TARGETS): % : %.o $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(LIBRARY): message.o
	$(CC) -shared -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGETS) $(OBJ_FILES) $(LIBRARY) /tmp/taskd.pid /tmp/tasks.fifo /tmp/tasks.txt /tmp/taskd.out /tmp/taskd.err
	rm -rf /tmp/tasks

mrproper: clean
	rm -f *~ *.o