CFLAGS?=-ansi -pedantic -O4 -Wall

default: multipart_parser.o

multipart_parser.o: multipart_parser.c multipart_parser.h

clean:
	rm -f *.o
