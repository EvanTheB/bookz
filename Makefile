default:
	$(CC) -Wall -c -g -I libircclient/include -I libarchive/libarchive -o bookz.o bookz.c
	$(CC) -Wall -g -o bookz bookz.o libircclient/src/libircclient.a libarchive/libarchive/libarchive.a -lz -lbz2 -llzma -lpthread -lnettle -lxml2
	# strip minitar
	# ls -lh minitar
