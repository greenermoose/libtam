# libtam — The Tamlinux C Foundation Library

`libtam` is the official C foundation library for Tamlinux packages, core system daemons, hardware telemetry probes, and command-line utilities.

## Vision & Anti-Bloat Philosophy

Tamlinux packages and utilities must be fast, rock-solid, and free of bloat. Rather than pulling in heavyweight runtime environments or complex frameworks like GLib, `libtam` provides a compact, cohesive standard C foundation designed to:
- Enable writing complete system daemons and utilities in **150–300 lines of clean C**.
- Maintain **sub-megabyte resident memory (RSS)** and **instant 0–2 ms cold start**.
- Natively compile against both `glibc` and `musl` on minimal Void Linux installations and legacy hardware.
- Enforce strict memory hygiene and timeout safety across all operations.

## Capabilities & Modules

| Module | Header / Symbol | Description |
| :--- | :--- | :--- |
| **`tam_str`** | `tam_str_*` | Safe dynamic string manipulation with auto-growth, format appending (`tam_str_cat_printf`), trimming, and JSON string escaping (`tam_str_cat_json`). |
| **`tam_exec`** | `tam_exec`, `tam_exec_opts` | Subprocess execution without a shell: a wall-clock deadline, standard input from `/dev/null` or supplied bytes, an output cap, stdout/stderr capture, and the whole process group killed on timeout. |
| **`tam_fs`** | `tam_fs_*` | Safe filesystem primitives, whole-file reading, atomic writes via temporary file and rename (`tam_fs_write_atomic`), and `mkdir -p` (`tam_fs_mkdir_p`). |
| **`tam_path`** | `tam_path_*`, `tam_xdg_path` | Path joining, joining under a root that refuses `..` and absolute paths, and per-command XDG state, cache, data, config and runtime paths (`<base>/tamlinux/<command>/`). |
| **`tam_ini`** | `tam_ini_*` | Lightweight INI and key-value file parser for user configuration and desktop state. |
| **`tam_term`** | `tam_log_*` | Colorized terminal logging and terminal escape sequence utilities. |
| **`tam_portal`** | `tam_portal_*` in `<tam_portal.h>` | Native desktop portal integration (FileChooser via D-Bus session bus). A separate library, `libtam-portal`. |

### Two static libraries

`libtam` builds two static archives:

- **`libtam.a`** is the core. It needs only libc.
- **`libtam-portal.a`** holds the desktop portal. It needs D-Bus, so it is
  separate: a program that does not use the portal never links or loads
  D-Bus.

Programs link `libtam` statically, so each carries only the objects it uses
(a few kilobytes) and is tied to the exact `libtam` it was built and tested
with. Tamlinux packages rebuild every program from pinned sources when
`libtam` changes, so a shared library would save nothing there. Version
0.3.0 and earlier also built a shared `libtam.so`; `make install` and
`make uninstall` remove it.

## Quickstart

### Building & Testing

```bash
# Build libtam.a and libtam-portal.a
make

# Run the test suite under AddressSanitizer and UndefinedBehaviorSanitizer
make test

# Install to system
sudo make install PREFIX=/usr/local

# Or install a development build for your user, and remove it again
make install PREFIX=$HOME/.local/dev
make uninstall PREFIX=$HOME/.local/dev
```

`make install` also writes pkg-config files, `libtam.pc` and
`libtam-portal.pc`, in `$(PREFIX)/lib/pkgconfig/`. Programs that use
`libtam`, such as `tam`, find it with `pkg-config libtam`; programs that use
the portal, such as `tamlinux-tools`' `tam-file-select`, use
`pkg-config libtam-portal`, which adds `libtam` and D-Bus. For a prefix outside pkg-config's default search path,
add it once in your shell profile:

```bash
export PKG_CONFIG_PATH="$HOME/.local/dev/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
```

### Usage Example

```c
#include <tam.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    /* 1. Dynamic string formatting */
    tam_str_t s = tam_str_new("Tamlinux");
    s = tam_str_cat_printf(s, " v%s", tam_version());
    tam_log_info("Running: %s", s);

    /* 2. Subprocess execution with timeout (no shell) */
    const char *const cmd[] = {"uname", "-r", NULL};
    tam_exec_result_t res = tam_exec(cmd, 1000); /* 1000ms deadline */
    if (res.exit_code == 0) {
        tam_log_info("Kernel: %s", res.stdout_buf);
    }
    tam_exec_result_free(&res);

    /* 3. Atomic write to this command's state directory */
    char *path = tam_xdg_path(TAM_XDG_STATE, "example", "status", true);
    if (path != NULL) {
        tam_fs_write_atomic(path, s, tam_str_len(s), 0644);
        free(path);
    }

    tam_str_free(s);
    return 0;
}
```

## License

GNU General Public License v3.0 (GPLv3). See [`LICENSE`](LICENSE) for details.
