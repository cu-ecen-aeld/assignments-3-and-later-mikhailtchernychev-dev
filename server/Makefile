CC=${CROSS_COMPILE}gcc 
CFLAGS=-Wall -Werror -g

.PHONY: default
default: all

all: aesdsocket 
aesdsocket : aesdsocket.o
aesdsocket.o: aesdsocket.c 

clean:
	rm -f aesdsocket  aesdsocket.o

