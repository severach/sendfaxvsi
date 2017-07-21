BINDIR=/usr/local/bin

all: sendfaxvsi

sendfaxvsi: sendfaxvsi.c
	gcc $(CFLAGS) -Wall -s -o 'sendfaxvsi' 'sendfaxvsi.c'

install:
	install -Dpm755 'sendfaxvsi' -t "$(DESTDIR)$(BINDIR)"

clean:
	rm -f 'sendfaxvsi'
