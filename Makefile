# libtam is two static libraries:
#   libtam.a         the core; needs only libc
#   libtam-portal.a  desktop portals; needs D-Bus (pkg-config dbus-1)
# Programs link them statically, so each carries only the objects it uses and
# nothing that does not use the portal loads D-Bus.

DBUS_CFLAGS ?= $(shell pkg-config --cflags dbus-1)

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
override CFLAGS += -g -fPIC -Iinclude
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
DISPLAY_SRCS = src/tam_display_discovery.c src/tam_display_kms.c src/tam_display_plan.c
DISPLAY_OBJS = $(patsubst src/%.c,build/display/%.o,$(DISPLAY_SRCS))
DISPLAY_CFLAGS ?= $(shell pkg-config --cflags-only-I libdrm)
DISPLAY_VERSION := $(shell sed -n 's/^\#define TAM_DISPLAY_API_VERSION "\(.*\)"$$/\1/p' include/tam_display.h)
DISPLAY_LIB = build/libtam-display.a
DISPLAY_SAN_OBJS = $(patsubst src/%.c,build/display-asan/%.o,$(DISPLAY_SRCS))
SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all core portal display test test-display clean install install-core install-portal install-display uninstall uninstall-core uninstall-portal uninstall-display FORCE

all: $(CORE_LIB) $(PORTAL_LIB)
core: $(CORE_LIB)
portal: $(PORTAL_LIB)
display: $(DISPLAY_LIB)

build/display build/display-asan:
	mkdir -p $@

# Track compiler/flags as well as source/header dependencies, without forcing
# unchanged objects to rebuild. Shell variables here contain build flags only.
build/.flags: FORCE | build
	@printf '%s\n' '$(CC) $(CPPFLAGS) $(CFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

build/.display-flags: FORCE | build
	@printf '%s\n' '$(CC) $(CPPFLAGS) $(CFLAGS) $(DISPLAY_CFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

build/display/%.o: src/%.c build/.display-flags | build/display
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DISPLAY_CFLAGS) -MMD -MP -c $< -o $@

build/display-asan/%.o: src/%.c build/.display-flags | build/display-asan
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DISPLAY_CFLAGS) $(SAN_FLAGS) -MMD -MP -c $< -o $@

$(DISPLAY_LIB): $(DISPLAY_OBJS)
	$(AR) rcs $@ $^

build/test_display: tests/test_display.c $(DISPLAY_SAN_OBJS) build/.display-flags
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DISPLAY_CFLAGS) $(SAN_FLAGS) -Isrc $< $(DISPLAY_SAN_OBJS) -o $@

test-display: build/test_display
	./build/test_display

-include $(DISPLAY_OBJS:.o=.d) $(DISPLAY_SAN_OBJS:.o=.d)

build:
	mkdir -p build

build/%.o: src/%.c include/tam.h build/.flags | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tam_portal.o: src/tam_portal.c include/tam.h include/tam_portal.h build/.flags | build
	$(CC) $(CFLAGS) $(DBUS_CFLAGS) -c $< -o $@

$(CORE_LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(PORTAL_LIB): build/tam_portal.o
	rm -f $@
	$(AR) rcs $@ $^

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
install: install-core install-portal

install-core: $(CORE_LIB)
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 include/tam.h $(DESTDIR)$(PREFIX)/include/
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(CORE_LIB) $(DESTDIR)$(PREFIX)/lib/
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam' \
		'Description: Tamlinux C foundation library' 'Version: $(VERSION)' \
		'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam' > $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc

install-portal: install-core $(PORTAL_LIB)
	install -m 644 include/tam_portal.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(PORTAL_LIB) $(DESTDIR)$(PREFIX)/lib/
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam-portal' \
		'Description: Tamlinux desktop portal library' 'Version: $(VERSION)' \
		'Requires: libtam = $(VERSION), dbus-1' 'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam-portal' > $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc

install-display: $(DISPLAY_LIB)
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PKGCONFIGDIR)
	install -m 644 include/tam_display.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(DISPLAY_LIB) $(DESTDIR)$(PREFIX)/lib/
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam-display' \
		'Description: Tamlinux experimental read-only DRM inspection' 'Version: $(DISPLAY_VERSION)' \
		'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam-display' > $(DESTDIR)$(PKGCONFIGDIR)/libtam-display.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam-display.pc

uninstall: uninstall-core uninstall-portal uninstall-display

uninstall-core:
	rm -f $(DESTDIR)$(PREFIX)/include/tam.h $(DESTDIR)$(PREFIX)/lib/libtam.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc

uninstall-portal:
	rm -f $(DESTDIR)$(PREFIX)/include/tam_portal.h $(DESTDIR)$(PREFIX)/lib/libtam-portal.a
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc

uninstall-display:
	rm -f $(DESTDIR)$(PREFIX)/include/tam_display.h $(DESTDIR)$(PREFIX)/lib/libtam-display.a
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam-display.pc
