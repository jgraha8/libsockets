CC:=gcc
CFLAGS:=-Wall -g -O2

all: server client

server: sock.o server.o
	$(CC) $(LDFLAGS) -o $@ $^

client: sock.o client.o
	$(CC) $(LDFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

PHONY: clean

clean:
	rm -f sock.o server.o server
