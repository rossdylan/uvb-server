PREFIX := /usr/local
CFLAGS := -I./include
CFLAGS += -Wall -Wextra -fPIC -pedantic
ifeq ($(CC),gcc)
    CFLAGS += -std=c11 -ggdb3
endif
ifeq ($(CC),clang)
    CFLAGS += -ggdb -Weverything
endif

CFLAGS += $(shell pkg-config --cflags glib-2.0 libevent)
LIBRARIES := $(shell pkg-config --libs glib-2.0 libevent)
SOURCE := $(wildcard src/*.c)
EXECUTABLE := uvb-server

all:
	$(CC) $(CFLAGS) $(LIBRARIES) -o $(EXECUTABLE) $(SOURCE)

install:
	install -D $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)

clean:
	$(RM) $(EXECUTABLE) counters.db names.db

uninstall:
	$(RM) $(PREFIX)/bin/$(EXECUTABLE)

