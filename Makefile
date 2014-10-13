PREFIX := /usr/local
CFLAGS := -I./include
CFLAGS += -Wall -Wextra -fPIC -pedantic -pthread
ifeq ($(CC),gcc)
    CFLAGS += -std=c11 -ggdb3
endif
ifeq ($(CC),clang)
    CFLAGS += -ggdb -Weverything
endif

CFLAGS += $(shell pkg-config --cflags glib-2.0)
LIBRARIES := $(shell pkg-config --libs glib-2.0)
SOURCE := $(wildcard src/*.c)
SOURCE2 := src/uvbstore.c src/uvbserver2.c
EXECUTABLE := uvb-server

uvb2:
	$(CC) $(CFLAGS) $(LIBRARIES) -o $(EXECUTABLE) $(SOURCE2)
all:
	$(CC) $(CFLAGS) $(LIBRARIES) -o $(EXECUTABLE) $(SOURCE)

install:
	install -D $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)

clean:
	$(RM) $(EXECUTABLE) counters.db names.db

uninstall:
	$(RM) $(PREFIX)/bin/$(EXECUTABLE)

