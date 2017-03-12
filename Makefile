CC:=gcc
CFLAGS:=-Wall -g -O2
LDFLAGS:=
LIBS:=-lm

all: server client usftp

server: sock.o server.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

client: sock.o client.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) -lpthread

usftp: sock.o usftp.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

PHONY: clean

clean:
	rm -f sock.o server.o client.o server client
