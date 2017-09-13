	# $(MAKE) -C libircclient/libircclient
	# $(MAKE) -C libarchive

CFLAGS=-Wall -g
IDIRS=-I libircclient/libircclient/include -I libarchive/libarchive
LIBS=-lz -lbz2 -llzma -lpthread -lnettle -lxml2 

bookz: bookz.o names.o libircclient/libircclient/src/libircclient.a libarchive/libarchive/libarchive.a
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

libarchive:
	git clone 'https://github.com/libarchive/libarchive.git'

libarchive/libarchive/libarchive.a: libarchive
	(cd libarchive && cmake .)
	$(MAKE) -C libarchive

libircclient:
	git clone 'https://github.com/EvanTheB/libircclient.git'

libircclient/libircclient/src/libircclient.a: libircclient
	(cd libircclient/libircclient && ./configure)
	$(MAKE) -C libircclient/libircclient

%.o: %.c
	$(CC) -c -o $@ $^ $(CFLAGS) $(IDIRS)

.PHONY: clean

clean:
	rm *.o bookz
	$(MAKE) clean -C libarchive
	$(MAKE) clean -C libircclient/libircclient
