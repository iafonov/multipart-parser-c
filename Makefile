CPPFLAGS?=-U DEBUG_MULTIPART
CFLAGS?=-std=c89 -ansi -pedantic -O0 -Wall -fPIC -g

default: solib alib

multipart_parser.o: multipart_parser.c multipart_parser.h

solib: multipart_parser.o
	$(CC) -shared -Wl,-soname,libmultipart.so -o libmultipart.so multipart_parser.o

alib: multipart_parser.o
	$(AR) rcs libmultipart.a multipart_parser.o

install: default
	install multipart_parser.h /usr/local/include
	install libmultipart.a /usr/local/lib
clean:
	rm -f *.o *.so
