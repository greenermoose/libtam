#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *rskip(char *s) {
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)(*(end - 1)))) {
        end--;
    }
    *end = '\0';
    return s;
}

static char *lskip(const char *s) {
    while (*s && isspace((unsigned char)(*s))) {
        s++;
    }
    return (char *)s;
}

int tam_ini_parse_string(const char *string, tam_ini_handler_t handler, void *user) {
    if (!string || !handler) return -1;

    char *copy = strdup(string);
    if (!copy) return -1;

    char section[128] = "";
    char *line = copy;
    int line_num = 0;

    while (*line) {
        line_num++;
        char *next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        char *start = lskip(line);
        if (*start && *start != ';' && *start != '#') {
            if (*start == '[') {
                char *end = strchr(start + 1, ']');
                if (end) {
                    *end = '\0';
                    strncpy(section, start + 1, sizeof(section) - 1);
                    section[sizeof(section) - 1] = '\0';
                }
            } else {
                char *eq = strchr(start, '=');
                if (!eq) {
                    eq = strchr(start, ':');
                }
                if (eq) {
                    *eq = '\0';
                    char *name = rskip(start);
                    char *value = lskip(eq + 1);
                    value = rskip(value);
                    if (handler(user, section, name, value) != 0) {
                        free(copy);
                        return line_num;
                    }
                }
            }
        }

        line = next ? next : line + strlen(line);
    }

    free(copy);
    return 0;
}

int tam_ini_parse_file(const char *filename, tam_ini_handler_t handler, void *user) {
    size_t len = 0;
    char *content = tam_fs_read_file(filename, &len);
    if (!content) return -1;

    int ret = tam_ini_parse_string(content, handler, user);
    free(content);
    return ret;
}
