PREFIX := /usr/local
CPPFLAGS := -I./include -I/usr/include
CFLAGS := -Wall -Wextra -fPIC -pedantic -pthread -lhttp_parser
ifeq ($(CC),gcc)
    CFLAGS += -std=c11 -ggdb3
endif
ifeq ($(CC),clang)
    CFLAGS += -ggdb -Weverything
endif

SOURCE := $(wildcard src/*.c)

EXECUTABLE := uvb-server

all:
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $(EXECUTABLE) $(SOURCE)

install:
	install -D $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)

clean:
	$(RM) $(EXECUTABLE) counters.db names.db

uninstall:
	$(RM) $(PREFIX)/bin/$(EXECUTABLE)

