CFLAGS=-O2 -g -Wall

all: master slave

master: master.c common.h
	gcc $(CFLAGS) $^ -o $@

slave: slave.c ebizzy.c common.h
	gcc $(CFLAGS) $^ -o $@

PHONY: clean

clean:
	rm -f master slave

