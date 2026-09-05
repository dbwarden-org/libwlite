# libwlite — Makefile

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -pedantic -O2 -I include
LDFLAGS ?=
LIBS    ?= -lsqlite3

PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

# Sources
LIB_SRC = wlite/schema.c wlite/parser.c wlite/introspect.c \
          wlite/diff.c wlite/planner.c wlite/migrate.c wlite/serialize.c \
          wlite/query.c wlite/record.c wlite/tx.c wlite/schema_inspect.c \
          wlite/compile.c
LIB_OBJ = $(LIB_SRC:.c=.o)
LIB_PIC = $(LIB_SRC:.c=.pic.o)

# Targets
.PHONY: all clean install test

all: libwlite.a libwlite.so

libwlite.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

libwlite.so: $(LIB_PIC)
	$(CC) -shared -o $@ $^ -lsqlite3

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.pic.o: %.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

clean:
	rm -f $(LIB_OBJ) $(LIB_PIC) libwlite.a libwlite.so

install: libwlite.a libwlite.so
	install -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)/wlite
	install -m 644 libwlite.a $(DESTDIR)$(LIBDIR)/
	install -m 755 libwlite.so $(DESTDIR)$(LIBDIR)/
	install -m 644 include/wlite/wlite.h $(DESTDIR)$(INCLUDEDIR)/wlite/

test: libwlite.a
	$(CC) -g -I include -o tests/test_wlite tests/test_wlite.c -L. -lwlite $(LIBS) && LD_LIBRARY_PATH=. ./tests/test_wlite
	$(CC) -g -I include -o tests/test_edge tests/test_edge_cases.c -L. -lwlite $(LIBS) && LD_LIBRARY_PATH=. ./tests/test_edge
	$(CC) -g -I include -o tests/test_conformance tests/conformance.c -L. -lwlite $(LIBS) && LD_LIBRARY_PATH=. ./tests/test_conformance
	$(CC) -g -I include -o tests/test_comprehensive tests/test_comprehensive.c -L. -lwlite $(LIBS) && LD_LIBRARY_PATH=. ./tests/test_comprehensive
