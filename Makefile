CC := clang
CFLAGS := -g -I./include
CFLAGS += $(shell pkg-config --cflags glib-2.0 libevent)
LIBRARIES := $(shell pkg-config --libs glib-2.0 libevent)
SOURCE := $(wildcard src/*.c)
EXECUTABLE := uvb-server

all:
	$(CC) $(CFLAGS) $(LIBRARIES) -o $(EXECUTABLE) $(SOURCE)

install:
	cp $(EXECUTABLE) /usr/local/bin/$(EXECUTABLE)

clean:
	rm $(EXECUTABLE) counters.db names.db

uninstall:
	$(RM) /usr/local/bin/$(EXECUTABLE)

