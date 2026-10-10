DBUS_CFLAGS ?= $(shell pkg-config --cflags dbus-1)
DBUS_LIBS ?= $(shell pkg-config --libs dbus-1)

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -fPIC -Iinclude $(DBUS_CFLAGS)
PREFIX ?= /usr/local

SRCS = src/tam_version.c \
       src/tam_term.c \
       src/tam_str.c \
       src/tam_exec.c \
       src/tam_fs.c \
       src/tam_ini.c \
       src/tam_portal.c

OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))
STATIC_LIB = build/libtam.a
SHARED_LIB = build/libtam.so
TEST_BIN = build/test_tam

.PHONY: all test clean install uninstall

all: $(STATIC_LIB) $(SHARED_LIB)

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^

$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(DBUS_LIBS)

$(TEST_BIN): tests/test_tam.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_tam.c $(STATIC_LIB) -o $@ $(DBUS_LIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build

install: $(STATIC_LIB) $(SHARED_LIB)
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 include/tam.h $(DESTDIR)$(PREFIX)/include/
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(STATIC_LIB) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(SHARED_LIB) $(DESTDIR)$(PREFIX)/lib/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/tam.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
