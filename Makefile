CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -fPIC -Iinclude
PREFIX ?= /usr/local

SRCS = src/tam_version.c \
       src/tam_term.c \
       src/tam_str.c \
       src/tam_exec.c \
       src/tam_fs.c \
       src/tam_ini.c

OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))
STATIC_LIB = build/libtam.a
SHARED_LIB = build/libtam.so
TEST_BIN = build/test_tam

.PHONY: all test clean install

all: $(STATIC_LIB) $(SHARED_LIB)

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^

$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^

$(TEST_BIN): tests/test_tam.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_tam.c $(STATIC_LIB) -o $@

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
