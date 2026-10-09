#include "tam.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

static tam_log_level_t g_log_level = TAM_LOG_INFO;

void tam_log_set_level(tam_log_level_t level) {
    g_log_level = level;
}

static void tam_log_v(tam_log_level_t level, const char *prefix, const char *color_code, const char *fmt, va_list ap) {
    if (level < g_log_level) {
        return;
    }

    FILE *out = (level >= TAM_LOG_WARN) ? stderr : stdout;
    int is_tty = isatty(fileno(out));

    if (is_tty && color_code) {
        fprintf(out, "%s[%s]\033[0m ", color_code, prefix);
    } else {
        fprintf(out, "[%s] ", prefix);
    }

    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    fflush(out);
}

void tam_log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    tam_log_v(TAM_LOG_INFO, "INFO", "\033[1;32m", fmt, ap);
    va_end(ap);
}

void tam_log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    tam_log_v(TAM_LOG_WARN, "WARN", "\033[1;33m", fmt, ap);
    va_end(ap);
}

void tam_log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    tam_log_v(TAM_LOG_ERROR, "ERROR", "\033[1;31m", fmt, ap);
    va_end(ap);
}

void tam_log_debug(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    tam_log_v(TAM_LOG_DEBUG, "DEBUG", "\033[1;34m", fmt, ap);
    va_end(ap);
}
