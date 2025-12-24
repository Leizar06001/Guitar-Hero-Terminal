CC=gcc
CFLAGS=-O2 -Wall -Wextra -std=c11 -I. $(shell pkg-config --cflags sdl2 opusfile)
LDLIBS=$(shell pkg-config --libs sdl2 opusfile)

OBJS=main.o midi.o audio.o terminal.o

all: midifall

midifall: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

main.o: main.c config.h audio.h midi.h terminal.h
	$(CC) $(CFLAGS) -c main.c -o main.o

midi.o: midi.c midi.h config.h
	$(CC) $(CFLAGS) -c midi.c -o midi.o

audio.o: audio.c audio.h config.h
	$(CC) $(CFLAGS) -c audio.c -o audio.o

terminal.o: terminal.c terminal.h config.h midi.h
	$(CC) $(CFLAGS) -c terminal.c -o terminal.o

clean:
	rm -f midifall $(OBJS)

.PHONY: all clean
