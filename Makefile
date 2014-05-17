CC=clang
CFLAGS=-lglib-2.0 -levent -g
SOURCE= $(wildcard src/*.c)
INCLUDE=-I./include $(shell pkg-config --cflags --libs glib-2.0)
OUT=-o
EXECUTABLE=uvb-server

all: $(EXECUTABLE)

$(EXECUTABLE):
	$(CC) $(CFLAGS) $(INCLUDE) $(OUT) $(EXECUTABLE) $(SOURCE)

install:
	cp $(EXECUTABLE) /usr/local/bin/$(EXECUTABLE)

uninstall:
	$(RM) /usr/local/bin/$(EXECUTABLE)

clean:
	$(RM) $(EXECUTABLE) counters.db names.db
