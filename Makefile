# libtam is two static libraries:
#   libtam.a         the core; needs only libc
#   libtam-portal.a  desktop portals; needs D-Bus (pkg-config dbus-1)
# Programs link them statically, so each carries only the objects it uses and
# nothing that does not use the portal loads D-Bus.

DBUS_CFLAGS ?= $(shell pkg-config --cflags dbus-1)

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CFLAGS += -fPIC -Iinclude
PREFIX ?= /usr/local
PKGCONFIGDIR ?= $(PREFIX)/lib/pkgconfig
VERSION := $(shell sed -n 's/^\#define TAM_VERSION_STRING "\(.*\)"$$/\1/p' include/tam.h)

SRCS = src/tam_version.c \
       src/tam_term.c \
       src/tam_str.c \
       src/tam_exec.c \
       src/tam_fs.c \
       src/tam_path.c \
       src/tam_ini.c

OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))
CORE_LIB = build/libtam.a
PORTAL_LIB = build/libtam-portal.a
TEST_BIN = build/test_tam
PORTAL_TEST_BIN = build/test_tam_portal

.PHONY: all test clean install uninstall

all: $(CORE_LIB) $(PORTAL_LIB)

build:
	mkdir -p build

build/%.o: src/%.c include/tam.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tam_portal.o: src/tam_portal.c include/tam.h include/tam_portal.h | build
	$(CC) $(CFLAGS) $(DBUS_CFLAGS) -c $< -o $@

$(CORE_LIB): $(OBJS)
	rm -f $@
	ar rcs $@ $^

$(PORTAL_LIB): build/tam_portal.o
	rm -f $@
	ar rcs $@ $^

$(TEST_BIN): tests/test_tam.c include/tam.h $(CORE_LIB)
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_tam.c $(CORE_LIB) -o $@

$(PORTAL_TEST_BIN): tests/test_tam_portal.c include/tam_portal.h $(PORTAL_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(DBUS_CFLAGS) -fsanitize=address,undefined tests/test_tam_portal.c \
		$(PORTAL_LIB) $(CORE_LIB) -o $@ $(shell pkg-config --libs dbus-1)

test: $(TEST_BIN) $(PORTAL_TEST_BIN)
	./$(TEST_BIN)
	./$(PORTAL_TEST_BIN)

clean:
	rm -rf build

# libtam 0.3.0 and earlier installed a shared libtam.so; install removes it so
# nothing links it by accident.
install: $(CORE_LIB) $(PORTAL_LIB)
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 include/tam.h include/tam_portal.h $(DESTDIR)$(PREFIX)/include/
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(CORE_LIB) $(PORTAL_LIB) $(DESTDIR)$(PREFIX)/lib/
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam' \
		'Description: Tamlinux C foundation library' 'Version: $(VERSION)' \
		'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam' > $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam-portal' \
		'Description: Tamlinux desktop portal library' 'Version: $(VERSION)' \
		'Requires: libtam = $(VERSION), dbus-1' 'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam-portal' > $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/tam.h $(DESTDIR)$(PREFIX)/include/tam_portal.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.a $(DESTDIR)$(PREFIX)/lib/libtam-portal.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc
