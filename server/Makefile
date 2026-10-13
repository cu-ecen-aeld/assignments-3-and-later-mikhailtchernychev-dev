CC ?= ${CROSS_COMPILE}gcc 
CFLAGS += -Wall -Werror -g -DUSE_AESD_CHAR_DEVICE

.PHONY: default
default: all

all: aesdsocket 
aesdsocket : aesdsocket.o
aesdsocket.o: aesdsocket.c   Makefile

clean:
	rm -f aesdsocket  aesdsocket.o

