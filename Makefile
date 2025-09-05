CC=gcc
CFLAGS=-Wall -pthread
LIBS=-lncurses

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LIBS)

clean:
	rm -f server client chat.log
