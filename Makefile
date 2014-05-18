CFLAGS := -I./include
ifeq ($(CC),gcc)
    CFLAGS += -std=c11 -ggdb3
endif
ifeq ($(CC),clang)
    CFLAGS += -ggdb
endif

CFLAGS += $(shell pkg-config --cflags glib-2.0 libevent)
LIBRARIES := $(shell pkg-config --libs glib-2.0 libevent)
SOURCE := $(wildcard src/*.c)
EXECUTABLE := uvb-server

all:
	$(CC) $(CFLAGS) $(LIBRARIES) -o $(EXECUTABLE) $(SOURCE)

install:
	cp $(EXECUTABLE) /usr/local/bin/$(EXECUTABLE)

clean:
	$(RM) $(EXECUTABLE) counters.db names.db

uninstall:
	$(RM) /usr/local/bin/$(EXECUTABLE)

