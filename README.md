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
| **`tam_str`** | `tam_str_*` | Safe dynamic string manipulation with auto-growth, format appending (`tam_str_cat_printf`), and trimming. |
| **`tam_exec`** | `tam_exec` | Subprocess execution with **mandatory timeouts**, stdout/stderr capture, and clean signal termination. |
| **`tam_fs`** | `tam_fs_*` | Safe filesystem primitives, whole-file reading, and atomic writes via temporary file and rename (`tam_fs_write_atomic`). |
| **`tam_ini`** | `tam_ini_*` | Lightweight INI and key-value file parser for user configuration and desktop state. |
| **`tam_term`** | `tam_log_*` | Colorized terminal logging and terminal escape sequence utilities. |
| **`tam_portal`** | `tam_portal_*` | Native desktop portal integration (FileChooser via D-Bus session bus). |

## Quickstart

### Building & Testing

```bash
# Build static (libtam.a) and shared (libtam.so) libraries
make

# Run the test suite under AddressSanitizer and UndefinedBehaviorSanitizer
make test

# Install to system
sudo make install PREFIX=/usr/local
```

### Usage Example

```c
#include <tam.h>
#include <stdio.h>

int main(void) {
    /* 1. Dynamic string formatting */
    tam_str_t s = tam_str_new("Tamlinux");
    s = tam_str_cat_printf(s, " v%s", tam_version());
    tam_log_info("Running: %s", s);

    /* 2. Subprocess execution with timeout */
    const char *const cmd[] = {"uname", "-r", NULL};
    tam_exec_result_t res = tam_exec(cmd, 1000); /* 1000ms timeout */
    if (res.exit_code == 0) {
        tam_log_info("Kernel: %s", res.stdout_buf);
    }
    tam_exec_result_free(&res);

    /* 3. Atomic file writing */
    tam_fs_write_atomic("/tmp/tam-status.txt", s, tam_str_len(s), 0644);

    tam_str_free(s);
    return 0;
}
```

## License

GNU General Public License v3.0 (GPLv3). See [`LICENSE`](LICENSE) for details.
