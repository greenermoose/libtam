DBUS_CFLAGS ?= $(shell pkg-config --cflags dbus-1)
DBUS_LIBS ?= $(shell pkg-config --libs dbus-1)

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -fPIC -Iinclude $(DBUS_CFLAGS)
PREFIX ?= /usr/local
PKGCONFIGDIR ?= $(PREFIX)/lib/pkgconfig
VERSION := $(shell sed -n 's/^\#define TAM_VERSION_STRING "\(.*\)"$$/\1/p' include/tam.h)

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

build/%.o: src/%.c include/tam.h | build
	$(CC) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^

$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(DBUS_LIBS)

$(TEST_BIN): tests/test_tam.c include/tam.h $(STATIC_LIB)
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
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam' \
		'Description: Tamlinux C foundation library' 'Version: $(VERSION)' \
		'Requires.private: dbus-1' 'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam' > $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/tam.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc
