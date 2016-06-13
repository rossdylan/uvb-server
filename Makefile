DESTDIR := /usr/local
CFLAGS := -ggdb -I./include -I/usr/include -DGPROF -pthread -O0 -Wall	\
          -Wextra -fPIC -pedantic -fgnu-tm
LDFLAGS := -g -pthread -lhttp_parser -llmdb -fgnu-tm

ifeq ($(CC),gcc)
	CFLAGS += -std=gnu11
endif
ifeq ($(CC),clang)
	CFLAGS += -Weverything
endif

OUT := out

SOURCE := buffer.c http.c list.c pool.c server.c timers.c
OBJS := $(addprefix $(OUT)/,$(patsubst %.c,%.o,$(SOURCE)))

EXECUTABLE := uvb-server

.PHONY: all
all: uvb-server-lmdb

$(OUT)/%.o: src/%.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

uvb-server-%: $(OUT)/%_counter.o $(OBJS) 
	$(CC) $(LDFLAGS) -o $@ $< $(OBJS)

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

