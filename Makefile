all : a10disp

install : a10disp
	install -m 0755 a10disp /bin

uninstall : /bin/a10disp
	rm -f /bin/a10disp

a10disp : a10disp.c
	gcc -Wall -O a10disp.c -o a10disp

clean :
	rm -f a10disp
