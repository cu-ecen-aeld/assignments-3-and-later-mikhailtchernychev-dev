CC = ${CROSS_COMPILE}gcc 
CFLAGS += -Wall -Werror -g -Wno-unused-result

$(info CC=${CC})

.PHONY: default
default: all

all: aesdsocket 
aesdsocket : aesdsocket.o
aesdsocket.o: aesdsocket.c   Makefile

clean:
	rm -f aesdsocket  aesdsocket.o

