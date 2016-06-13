DESTDIR := /usr/local
CFLAGS := -ggdb -I./include -I/usr/include -DGPROF -pthread -O0 -Wall	\
          -Wextra -fPIC -pedantic
LDFLAGS := -g -pthread -lhttp_parser

ifeq ($(CC),gcc)
	CFLAGS += -std=gnu11
endif
ifeq ($(CC),clang)
	CFLAGS += -Weverything
endif

COUNTER ?= lmdb_counter
ifeq ($(COUNTER),lmdb_counter)
	LDFLAGS += -llmdb
else ifeq ($(COUNTER),tm_counter)
	CFLAGS += -fgnu-tm
	LDFLAGS += -fgnu-tm
endif

OUT := out

SOURCE := buffer.c http.c list.c pool.c server.c timers.c $(COUNTER).c
OBJS := $(addprefix $(OUT)/,$(patsubst %.c,%.o,$(SOURCE)))

EXECUTABLE := uvb-server

.PHONY: all
all: uvb-server

$(OUT)/%.o: src/%.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

uvb-server: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.PHONY: install
install:
	install -D uvb-server $(DESTDIR)/bin/$(EXECUTABLE)

.PHONY: clean
clean:
	$(RM) -rf $(OUT) $(EXECUTABLE) counters.db names.db
	mkdir $(OUT)

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)/bin/$(EXECUTABLE)

