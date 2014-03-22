PREFIX ?=/usr/local
CC ?= gcc
CFLAGS ?= -g -O2
all : a10disp

install : a10disp
	install -m 0755 a10disp $(PREFIX)/bin

uninstall : $(PREFIX)/bin/a10disp
	rm -f $(PREFIX)/bin/a10disp

a10disp : a10disp.c
	$(CC) -Wall $(CFLAGS) a10disp.c -o a10disp

clean :
	rm -f a10disp
