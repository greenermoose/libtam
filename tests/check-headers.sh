#!/bin/sh
# Every public header compiles on its own, with no feature macros from
# libtam: under strict C17 with POSIX, with GNU, under gnu17, and under
# strict C17 alone. tam_tty.h needs POSIX and says so.
# Arguments: extra compiler flags (include paths for D-Bus, DRM, systemd).
set -u
cc=${CC:-cc}
fail=0
for path in include/*.h; do
	name=${path#include/}
	for mode in "-std=c17 -D_POSIX_C_SOURCE=200809L" "-std=c17 -D_GNU_SOURCE" "-std=gnu17" "-std=c17"; do
		if [ "$mode" = "-std=c17" ] && [ "$name" = tam_tty.h ]; then
			continue
		fi
		if ! printf '#include <%s>\n' "$name" |
			$cc $mode -Wall -Wextra -Wpedantic -Werror -Iinclude "$@" -fsyntax-only -x c -; then
			echo "check-headers: $name does not compile with $mode" >&2
			fail=1
		fi
	done
done
if ! printf '#include <tam_tty.h>\n' | $cc -std=c17 -Iinclude -fsyntax-only -x c - 2>&1 |
	grep -q 'needs POSIX.1-2008'; then
	echo "check-headers: tam_tty.h does not name its POSIX requirement" >&2
	fail=1
fi
[ "$fail" -eq 0 ] && echo "header checks passed"
exit "$fail"
