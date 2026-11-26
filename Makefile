# Default libtam build is three static libraries (display/session are optional):
#   libtam.a         the core; needs only libc
#   libtam-portal.a  desktop portals; needs D-Bus (pkg-config dbus-1)
#   libtam-render.a  document source, layout and Markdown/HTML/Mermaid
#                    rendering; needs only libtam.a
# Programs link them statically, so each carries only the objects it uses and
# nothing that does not use the portal loads D-Bus.

DBUS_CFLAGS ?= $(shell pkg-config --cflags dbus-1)

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
# Debug information names source files relative to the source tree, not
# the directory it was built in, so builds in different places are identical.
override CFLAGS += -g -fPIC -Iinclude -ffile-prefix-map=$(CURDIR)=.
# libtam is C17 with Linux and GNU interfaces. The public headers set no
# feature macros: programs choose theirs (tests/check-headers.sh).
override CFLAGS += -std=c17 -D_GNU_SOURCE
# Development installs default to $(HOME)/.local so they need no root;
# /usr/local is retired on Tamlinux. An explicit PREFIX still wins.
PREFIX ?= $(HOME)/.local
PKGCONFIGDIR ?= $(PREFIX)/lib/pkgconfig
VERSION := $(shell tr -d '[:space:]' < VERSION)
TAM_VERSION_MAJOR := $(shell echo $(VERSION) | cut -d. -f1)
TAM_VERSION_MINOR := $(shell echo $(VERSION) | cut -d. -f2)
TAM_VERSION_PATCH := $(shell echo $(VERSION) | cut -d. -f3)

# "make" alone builds the libraries; the version rule below is not the goal.
.DEFAULT_GOAL := all

include/tam.h: VERSION
	@sed -i \
	  -e 's/^#define TAM_VERSION_MAJOR .*/#define TAM_VERSION_MAJOR $(TAM_VERSION_MAJOR)/' \
	  -e 's/^#define TAM_VERSION_MINOR .*/#define TAM_VERSION_MINOR $(TAM_VERSION_MINOR)/' \
	  -e 's/^#define TAM_VERSION_PATCH .*/#define TAM_VERSION_PATCH $(TAM_VERSION_PATCH)/' \
	  -e 's/^#define TAM_VERSION_STRING .*/#define TAM_VERSION_STRING "$(VERSION)"/' $@

SRCS = src/tam_version.c \
       src/tam_term.c \
       src/tam_str.c \
       src/tam_exec.c \
       src/tam_fs.c \
       src/tam_path.c \
       src/tam_ini.c \
       src/tam_sha256.c \
       src/tam_walk.c \
       src/tam_rec.c \
       src/tam_fs_tree.c \
       src/tam_cultivar.c \
       src/tam_uni.c \
       src/tam_key.c \
       src/tam_tty.c \
       src/tam_pager.c \
       src/tam_io.c \
       src/tam_proc.c \
       src/tam_sock.c \
       src/tam_b64.c

OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))
CORE_LIB = build/libtam.a
PORTAL_LIB = build/libtam-portal.a
TEST_BIN = build/test_tam
FILES_TEST_BIN = build/test_tam_files
PORTAL_TEST_BIN = build/test_tam_portal
CULTIVAR_TEST_BIN = build/test_tam_cultivar
TERMINAL_TEST_BIN = build/test_tam_terminal
PRIMITIVES_TEST_BIN = build/test_tam_primitives
# libtam-render.a: tam-view's document source, layout and renderers. It
# needs only libtam.a, so it is built and installed by default.
RENDER_SRCS = src/render/source.c src/render/layout.c src/render/render.c \
              src/render/render_md.c src/render/render_html.c src/render/render_mermaid.c
RENDER_OBJS = $(patsubst src/render/%.c,build/render/%.o,$(RENDER_SRCS))
RENDER_LIB = build/libtam-render.a
RENDER_CFLAGS = -D_FILE_OFFSET_BITS=64 -Isrc/render
RENDER_TEST_BIN = build/test_tam_render
RENDER_LAYOUT_TEST_BIN = build/test_tam_render_layout
DISPLAY_SRCS = src/tam_display_discovery.c src/tam_display_kms.c src/tam_display_plan.c src/tam_display_fbcon.c src/tam_vt.c src/tam_display_access.c src/tam_display_restore.c src/tam_display_atomic.c
DISPLAY_OBJS = $(patsubst src/%.c,build/display/%.o,$(DISPLAY_SRCS))
SESSION_CFLAGS ?= $(shell pkg-config --cflags libsystemd)
SESSION_LIBS ?= $(shell pkg-config --libs libsystemd)
SESSION_LIB = build/libtam-display-session.a
DISPLAY_CFLAGS ?= $(shell pkg-config --cflags-only-I libdrm)
DISPLAY_VERSION := $(shell sed -n 's/^\#define TAM_DISPLAY_API_VERSION "\(.*\)"$$/\1/p' include/tam_display.h)
DISPLAY_LIB = build/libtam-display.a
DISPLAY_SAN_OBJS = $(patsubst src/%.c,build/display-asan/%.o,$(DISPLAY_SRCS))
SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all core portal render display test test-display test-terminal clean install install-core install-portal install-render install-display uninstall uninstall-core uninstall-portal uninstall-render uninstall-display FORCE

all: $(CORE_LIB) $(PORTAL_LIB) $(RENDER_LIB)
core: $(CORE_LIB)
portal: $(PORTAL_LIB)
render: $(RENDER_LIB)
display: $(DISPLAY_LIB)

session: $(SESSION_LIB)

build/display/tam_display_session.o: src/tam_display_session.c build/.session-flags | build/display
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SESSION_CFLAGS) $(DISPLAY_CFLAGS) -MMD -MP -c $< -o $@

build/.session-flags: FORCE | build
	@printf '%s\n' '$(CC) $(CPPFLAGS) $(CFLAGS) $(DISPLAY_CFLAGS) $(SESSION_CFLAGS) $(SESSION_LIBS)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(SESSION_LIB): build/display/tam_display_session.o
	$(AR) rcs $@ $^

-include build/display/tam_display_session.d

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

test-display: build/test_display build/test_display_owned build/test_vt
	./build/test_display
	./build/test_display_owned
	./build/test_vt

-include $(DISPLAY_OBJS:.o=.d) $(DISPLAY_SAN_OBJS:.o=.d)

build:
	mkdir -p build

build/%.o: src/%.c $(wildcard include/*.h) src/tam_internal.h build/.flags | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tam_portal.o: src/tam_portal.c include/tam.h include/tam_portal.h build/.flags | build
	$(CC) $(CFLAGS) $(DBUS_CFLAGS) -c $< -o $@

$(CORE_LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(PORTAL_LIB): build/tam_portal.o
	rm -f $@
	$(AR) rcs $@ $^

build/render:
	mkdir -p $@

build/render/%.o: src/render/%.c $(wildcard include/*.h) src/render/render_internal.h build/.flags | build/render
	$(CC) $(CFLAGS) $(RENDER_CFLAGS) -c $< -o $@

$(RENDER_LIB): $(RENDER_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

# The renderer and the whole core, built with sanitizers.
$(RENDER_TEST_BIN): tests/test_render.c $(RENDER_SRCS) $(SRCS) $(wildcard include/*.h) src/render/render_internal.h | build
	$(CC) $(CFLAGS) $(RENDER_CFLAGS) -fsanitize=address,undefined tests/test_render.c $(RENDER_SRCS) $(SRCS) -o $@

$(RENDER_LAYOUT_TEST_BIN): tests/test_render_layout.c $(RENDER_SRCS) $(SRCS) $(wildcard include/*.h) src/render/render_internal.h | build
	$(CC) $(CFLAGS) $(RENDER_CFLAGS) -fsanitize=address,undefined tests/test_render_layout.c $(RENDER_SRCS) $(SRCS) -o $@

$(TEST_BIN): tests/test_tam.c include/tam.h $(CORE_LIB)
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_tam.c $(CORE_LIB) -o $@

$(FILES_TEST_BIN): tests/test_tam_files.c include/tam.h $(CORE_LIB)
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_tam_files.c $(CORE_LIB) -o $@

$(PORTAL_TEST_BIN): tests/test_tam_portal.c include/tam_portal.h $(PORTAL_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(DBUS_CFLAGS) -fsanitize=address,undefined tests/test_tam_portal.c \
		$(PORTAL_LIB) $(CORE_LIB) -o $@ $(shell pkg-config --libs dbus-1)

$(CULTIVAR_TEST_BIN): tests/test_cultivar.c src/tam_cultivar.c src/tam_io.c include/tam.h | build
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_cultivar.c src/tam_cultivar.c src/tam_io.c -o $@

$(TERMINAL_TEST_BIN): tests/test_terminal.c $(wildcard include/*.h) src/tam_uni.c src/tam_key.c src/tam_tty.c src/tam_pager.c src/tam_io.c src/tam_b64.c src/tam_proc.c | build
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_terminal.c src/tam_uni.c src/tam_key.c src/tam_tty.c src/tam_pager.c src/tam_io.c src/tam_b64.c src/tam_proc.c src/tam_version.c -o $@

test-terminal: $(TERMINAL_TEST_BIN)
	./$(TERMINAL_TEST_BIN)

# The whole core, built with sanitizers, behind the 0.11.0 primitives' tests.
$(PRIMITIVES_TEST_BIN): tests/test_primitives.c $(SRCS) $(wildcard include/*.h) src/tam_internal.h | build
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/test_primitives.c $(SRCS) -o $@

test: $(TEST_BIN) $(FILES_TEST_BIN) $(PORTAL_TEST_BIN) $(CULTIVAR_TEST_BIN) $(TERMINAL_TEST_BIN) $(PRIMITIVES_TEST_BIN) $(RENDER_TEST_BIN) $(RENDER_LAYOUT_TEST_BIN)
	./$(TEST_BIN)
	./$(FILES_TEST_BIN)
	./$(PORTAL_TEST_BIN)
	./$(CULTIVAR_TEST_BIN)
	./$(TERMINAL_TEST_BIN)
	./$(PRIMITIVES_TEST_BIN)
	./$(RENDER_LAYOUT_TEST_BIN)
	./$(RENDER_TEST_BIN)
	CC='$(CC)' sh tests/check-headers.sh $(DBUS_CFLAGS) $(DISPLAY_CFLAGS) $(SESSION_CFLAGS)

clean:
	rm -rf build

# libtam 0.3.0 and earlier installed a shared libtam.so; install removes it so
# nothing links it by accident.
install: install-core install-portal install-render

install-core: $(CORE_LIB)
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 include/tam.h include/tam_uni.h include/tam_key.h \
		include/tam_tty.h include/tam_pager.h include/tam_io.h \
		include/tam_proc.h include/tam_sock.h include/tam_b64.h \
		$(DESTDIR)$(PREFIX)/include/
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

install-render: install-core $(RENDER_LIB)
	install -m 644 include/tam_render.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(RENDER_LIB) $(DESTDIR)$(PREFIX)/lib/
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam-render' \
		'Description: Tamlinux document source, layout and Markdown/HTML/Mermaid rendering' \
		'Version: $(VERSION)' 'Requires: libtam = $(VERSION)' \
		'Cflags: -I$${includedir} -D_FILE_OFFSET_BITS=64' \
		'Libs: -L$${libdir} -ltam-render' > $(DESTDIR)$(PKGCONFIGDIR)/libtam-render.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam-render.pc

install-display: $(DISPLAY_LIB)
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PKGCONFIGDIR)
	install -m 644 include/tam_display.h include/tam_vt.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(DISPLAY_LIB) $(DESTDIR)$(PREFIX)/lib/
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam-display' \
		'Description: Tamlinux experimental owned DRM display operations' 'Version: $(DISPLAY_VERSION)' \
		'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam-display' > $(DESTDIR)$(PKGCONFIGDIR)/libtam-display.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam-display.pc

uninstall: uninstall-core uninstall-portal uninstall-render uninstall-display uninstall-session

uninstall-render:
	rm -f $(DESTDIR)$(PREFIX)/include/tam_render.h $(DESTDIR)$(PREFIX)/lib/libtam-render.a
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam-render.pc

uninstall-core:
	rm -f $(DESTDIR)$(PREFIX)/include/tam.h $(DESTDIR)$(PREFIX)/include/tam_uni.h \
		$(DESTDIR)$(PREFIX)/include/tam_key.h $(DESTDIR)$(PREFIX)/include/tam_tty.h \
		$(DESTDIR)$(PREFIX)/include/tam_pager.h $(DESTDIR)$(PREFIX)/include/tam_io.h \
		$(DESTDIR)$(PREFIX)/include/tam_proc.h $(DESTDIR)$(PREFIX)/include/tam_sock.h \
		$(DESTDIR)$(PREFIX)/include/tam_b64.h $(DESTDIR)$(PREFIX)/lib/libtam.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libtam.so
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam.pc

uninstall-portal:
	rm -f $(DESTDIR)$(PREFIX)/include/tam_portal.h $(DESTDIR)$(PREFIX)/lib/libtam-portal.a
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam-portal.pc

uninstall-display:
	rm -f $(DESTDIR)$(PREFIX)/include/tam_display.h $(DESTDIR)$(PREFIX)/include/tam_vt.h $(DESTDIR)$(PREFIX)/lib/libtam-display.a
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam-display.pc

.PHONY: session install-session uninstall-session
install-session: install-display $(SESSION_LIB)
	install -m 644 include/tam_display_session.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(SESSION_LIB) $(DESTDIR)$(PREFIX)/lib/
	printf '%s\n' 'prefix=$(PREFIX)' 'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' '' 'Name: libtam-display-session' \
		'Description: Tamlinux optional seat access and session observation' 'Version: $(DISPLAY_VERSION)' \
		'Requires: libtam-display = $(DISPLAY_VERSION), libsystemd' 'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -ltam-display-session' > $(DESTDIR)$(PKGCONFIGDIR)/libtam-display-session.pc
	chmod 644 $(DESTDIR)$(PKGCONFIGDIR)/libtam-display-session.pc

uninstall-session:
	rm -f $(DESTDIR)$(PREFIX)/include/tam_display_session.h $(DESTDIR)$(PREFIX)/lib/libtam-display-session.a
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libtam-display-session.pc

build/test_display_owned: tests/test_display_owned.c $(DISPLAY_SAN_OBJS) build/.display-flags
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DISPLAY_CFLAGS) $(SAN_FLAGS) -Isrc $< $(DISPLAY_SAN_OBJS) -Wl,--wrap=tam_display_inspect_with -o $@


VT_WRAPS = open close fstat ioctl tcgetpgrp
build/test_vt: tests/test_vt.c $(DISPLAY_SAN_OBJS) build/.display-flags
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SAN_FLAGS) -Isrc $< $(DISPLAY_SAN_OBJS) $(foreach s,$(VT_WRAPS),-Wl,--wrap=$(s)) -o $@

SESSION_WRAPS = tam_vt_check tam_vt_check_restore sd_pid_get_session sd_session_get_uid sd_session_get_vt sd_session_get_type sd_session_get_seat sd_session_is_active sd_session_is_remote sd_device_new_from_devnum sd_device_get_property_value sd_device_has_tag sd_device_unref lstat fstat open close ioctl sd_bus_open_system sd_bus_set_method_call_timeout sd_bus_call_method sd_bus_message_read sd_bus_message_unref sd_bus_match_signal sd_bus_slot_unref sd_bus_flush_close_unref sd_bus_process sd_bus_get_fd sd_bus_get_events
build/display-asan/tam_display_session.o: src/tam_display_session.c build/.session-flags | build/display-asan
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SESSION_CFLAGS) $(DISPLAY_CFLAGS) $(SAN_FLAGS) -MMD -MP -c $< -o $@
-include build/display-asan/tam_display_session.d
build/test_display_session: tests/test_display_session.c build/display-asan/tam_display_session.o $(DISPLAY_SAN_OBJS) build/.session-flags
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SESSION_CFLAGS) $(DISPLAY_CFLAGS) $(SAN_FLAGS) -Isrc $< build/display-asan/tam_display_session.o $(DISPLAY_SAN_OBJS) $(foreach s,$(SESSION_WRAPS),-Wl,--wrap=$(s)) -o $@ $(SESSION_LIBS)

.PHONY: test-session
test-session: build/test_display_session
	./build/test_display_session
