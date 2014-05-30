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
OBJECTS := $(SOURCE:.c=.o)
EXECUTABLE := uvb-server

.PHONY: all clean install

all: $(EXECUTABLE)

install:
	install -D $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)

clean:
	$(RM) $(EXECUTABLE) counters.db names.db $(OBJECTS)

uninstall:
	$(RM) $(PREFIX)/bin/$(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBRARIES)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
