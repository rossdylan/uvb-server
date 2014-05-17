CC=clang
CFLAGS=-lglib-2.0 -levent -g
SOURCE= $(wildcard src/*.c)
INCLUDE=-I./include -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include
OUT=-o
EXECUTABLE=uvb-server

all:
	$(CC) $(CFLAGS) $(INCLUDE) $(OUT) $(EXECUTABLE) $(SOURCE)

install:
	cp $(EXECUTABLE) /usr/bin/

clean:
	rm $(EXECUTABLE)

