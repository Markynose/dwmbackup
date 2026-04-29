CC = cc
CFLAGS = -std=c99 -O2 -Wall

chifetch: chifetch.c
	$(CC) $(CFLAGS) -o chifetch chifetch.c

install: chifetch
	cp chifetch /usr/local/bin/chifetch

clean:
	rm -f chifetch
