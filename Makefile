DESTDIR := /usr/local
CFLAGS := -ggdb -I./include -I/usr/include -DGPROF -pthread -O3 -Wall \
          -Wextra -fPIC -pedantic
LDFLAGS := -g -pthread -lhttp_parser

ifeq ($(CC),gcc)
	CFLAGS += -std=gnu11
endif
ifeq ($(CC),clang)
	CFLAGS += -Weverything
endif

OUT := out

SOURCE := buffer.c http.c list.c pool.c server.c timers.c
OBJS := $(addprefix $(OUT)/,$(patsubst %.c,%.o,$(SOURCE)))

.PHONY: lmdb
lmdb: uvb-server-lmdb

.PHONY: tm
tm: uvb-server-tm

.PHONY: all
all: lmdb tm

$(OUT)/%.o: src/%.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

$(OUT)/tm_counter.o: src/tm_counter.c Makefile
	$(CC) -c $(CFLAGS) -fgnu-tm -o $@ $<

uvb-server-lmdb: out/lmdb_counter.o $(OBJS) 
	$(CC) $(LDFLAGS) -llmdb -o $@ $(OBJS) out/lmdb_counter.o

uvb-server-tm: out/tm_counter.o $(OBJS) 
	$(CC) $(LDFLAGS) -fgnu-tm -o $@ $(OBJS) out/tm_counter.o

.PHONY: install
install:
	install -D uvb-server $(DESTDIR)/bin/$(EXECUTABLE)

.PHONY: clean
clean:
	$(RM) -rf $(OUT) uvb-server-{lmdb,tm} counters.db names.db
	mkdir $(OUT)

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)/bin/$(EXECUTABLE)

