PREFIX ?=/usr/local
CC ?= gcc
all : a10disp

install : a10disp
	install -m 0755 a10disp $(PREFIX)/bin

uninstall : $(PREFIX)/bin/a10disp
	rm -f $(PREFIX)/bin/a10disp

a10disp : a10disp.c
	$(CC) -Wall -O a10disp.c -o a10disp -g

clean :
	rm -f a10disp
