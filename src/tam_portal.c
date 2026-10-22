#include "tam_portal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <dbus/dbus.h>

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *tam_portal_uri_to_path(const char *uri) {
    if (!uri) return NULL;
    const char *p = uri;
    if (strncmp(p, "file://localhost/", 17) == 0) {
        p += 16; /* Keep leading '/' */
    } else if (strncmp(p, "file:///", 8) == 0) {
        p += 7; /* Keep leading '/' */
    } else if (strncmp(p, "file://", 7) == 0) {
        p += 7;
    }

    size_t len = strlen(p);
    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] == '%' && i + 2 < len) {
            int h1 = hex_val(p[i + 1]);
            int h2 = hex_val(p[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                out[o++] = (char)((h1 << 4) | h2);
                i += 2;
                continue;
            }
        }
        out[o++] = p[i];
    }
    out[o] = '\0';
    return out;
}

static void append_dict_string(DBusMessageIter *dict_iter, const char *key, const char *val) {
    DBusMessageIter entry_iter, var_iter;
    dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
    dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &var_iter);
    dbus_message_iter_append_basic(&var_iter, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&entry_iter, &var_iter);
    dbus_message_iter_close_container(dict_iter, &entry_iter);
}

static void append_dict_bool(DBusMessageIter *dict_iter, const char *key, dbus_bool_t val) {
    DBusMessageIter entry_iter, var_iter;
    dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
    dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "b", &var_iter);
    dbus_message_iter_append_basic(&var_iter, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&entry_iter, &var_iter);
    dbus_message_iter_close_container(dict_iter, &entry_iter);
}

static void append_filters(DBusMessageIter *dict_iter, const char *extensions) {
    if (!extensions || !*extensions) return;

    /* Parse space-separated extensions */
    char *ext_copy = strdup(extensions);
    if (!ext_copy) return;

    char **exts = NULL;
    size_t count = 0;
    size_t cap = 0;

    char *saveptr = NULL;
    char *token = strtok_r(ext_copy, " \t\r\n", &saveptr);
    while (token) {
        while (*token == '.') token++;
        if (*token) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 8;
                char **next_exts = realloc(exts, cap * sizeof(char *));
                if (!next_exts) break;
                exts = next_exts;
            }
            char *clean_ext = strdup(token);
            if (clean_ext) {
                for (char *c = clean_ext; *c; c++) *c = (char)tolower((unsigned char)*c);
                exts[count++] = clean_ext;
            }
        }
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    free(ext_copy);

    if (count == 0) {
        free(exts);
        return;
    }

    /* Build label: "*.ext1 *.ext2 ..." */
    size_t label_len = 0;
    for (size_t i = 0; i < count; i++) {
        label_len += strlen(exts[i]) + 3;
    }
    char *label = malloc(label_len + 1);
    if (!label) {
        for (size_t i = 0; i < count; i++) free(exts[i]);
        free(exts);
        return;
    }
    label[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        if (i > 0) strcat(label, " ");
        strcat(label, "*.");
        strcat(label, exts[i]);
    }

    /* 1. filters: a(sa(us)) */
    {
        DBusMessageIter entry_iter, var_iter, filter_arr_iter, filter_struct_iter, pat_arr_iter;
        const char *key = "filters";
        const char *lbl = label;
        dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "a(sa(us))", &var_iter);
        dbus_message_iter_open_container(&var_iter, DBUS_TYPE_ARRAY, "(sa(us))", &filter_arr_iter);
        dbus_message_iter_open_container(&filter_arr_iter, DBUS_TYPE_STRUCT, NULL, &filter_struct_iter);
        dbus_message_iter_append_basic(&filter_struct_iter, DBUS_TYPE_STRING, &lbl);
        dbus_message_iter_open_container(&filter_struct_iter, DBUS_TYPE_ARRAY, "(us)", &pat_arr_iter);

        for (size_t i = 0; i < count; i++) {
            char pat_lower[128], pat_upper[128];
            snprintf(pat_lower, sizeof(pat_lower), "*.%s", exts[i]);
            snprintf(pat_upper, sizeof(pat_upper), "*.%s", exts[i]);
            for (char *c = pat_upper + 2; *c; c++) *c = (char)toupper((unsigned char)*c);

            dbus_uint32_t type = 0;
            const char *pat_l = pat_lower;
            DBusMessageIter p_struct_l;
            dbus_message_iter_open_container(&pat_arr_iter, DBUS_TYPE_STRUCT, NULL, &p_struct_l);
            dbus_message_iter_append_basic(&p_struct_l, DBUS_TYPE_UINT32, &type);
            dbus_message_iter_append_basic(&p_struct_l, DBUS_TYPE_STRING, &pat_l);
            dbus_message_iter_close_container(&pat_arr_iter, &p_struct_l);

            const char *pat_u = pat_upper;
            DBusMessageIter p_struct_u;
            dbus_message_iter_open_container(&pat_arr_iter, DBUS_TYPE_STRUCT, NULL, &p_struct_u);
            dbus_message_iter_append_basic(&p_struct_u, DBUS_TYPE_UINT32, &type);
            dbus_message_iter_append_basic(&p_struct_u, DBUS_TYPE_STRING, &pat_u);
            dbus_message_iter_close_container(&pat_arr_iter, &p_struct_u);
        }

        dbus_message_iter_close_container(&filter_struct_iter, &pat_arr_iter);
        dbus_message_iter_close_container(&filter_arr_iter, &filter_struct_iter);
        dbus_message_iter_close_container(&var_iter, &filter_arr_iter);
        dbus_message_iter_close_container(&entry_iter, &var_iter);
        dbus_message_iter_close_container(dict_iter, &entry_iter);
    }

    /* 2. current_filter: (sa(us)) */
    {
        DBusMessageIter entry_iter, var_iter, filter_struct_iter, pat_arr_iter;
        const char *key = "current_filter";
        const char *lbl = label;
        dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "(sa(us))", &var_iter);
        dbus_message_iter_open_container(&var_iter, DBUS_TYPE_STRUCT, NULL, &filter_struct_iter);
        dbus_message_iter_append_basic(&filter_struct_iter, DBUS_TYPE_STRING, &lbl);
        dbus_message_iter_open_container(&filter_struct_iter, DBUS_TYPE_ARRAY, "(us)", &pat_arr_iter);

        for (size_t i = 0; i < count; i++) {
            char pat_lower[128], pat_upper[128];
            snprintf(pat_lower, sizeof(pat_lower), "*.%s", exts[i]);
            snprintf(pat_upper, sizeof(pat_upper), "*.%s", exts[i]);
            for (char *c = pat_upper + 2; *c; c++) *c = (char)toupper((unsigned char)*c);

            dbus_uint32_t type = 0;
            const char *pat_l = pat_lower;
            DBusMessageIter p_struct_l;
            dbus_message_iter_open_container(&pat_arr_iter, DBUS_TYPE_STRUCT, NULL, &p_struct_l);
            dbus_message_iter_append_basic(&p_struct_l, DBUS_TYPE_UINT32, &type);
            dbus_message_iter_append_basic(&p_struct_l, DBUS_TYPE_STRING, &pat_l);
            dbus_message_iter_close_container(&pat_arr_iter, &p_struct_l);

            const char *pat_u = pat_upper;
            DBusMessageIter p_struct_u;
            dbus_message_iter_open_container(&pat_arr_iter, DBUS_TYPE_STRUCT, NULL, &p_struct_u);
            dbus_message_iter_append_basic(&p_struct_u, DBUS_TYPE_UINT32, &type);
            dbus_message_iter_append_basic(&p_struct_u, DBUS_TYPE_STRING, &pat_u);
            dbus_message_iter_close_container(&pat_arr_iter, &p_struct_u);
        }

        dbus_message_iter_close_container(&filter_struct_iter, &pat_arr_iter);
        dbus_message_iter_close_container(&var_iter, &filter_struct_iter);
        dbus_message_iter_close_container(&entry_iter, &var_iter);
        dbus_message_iter_close_container(dict_iter, &entry_iter);
    }

    free(label);
    for (size_t i = 0; i < count; i++) free(exts[i]);
    free(exts);
}

tam_portal_file_result_t tam_portal_file_choose(const tam_portal_file_options_t *options) {
    tam_portal_file_result_t result;
    memset(&result, 0, sizeof(result));
    result.status = 2; /* Default to error */

    const char *title = (options && options->title) ? options->title : "Select file";
    bool multiple = options ? options->multiple : false;
    bool directory = options ? options->directory : false;
    const char *extensions = options ? options->extensions : NULL;
    int timeout_ms = (options && options->timeout_ms > 0) ? options->timeout_ms : 600000;

    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err)) {
        result.status = 2;
        result.error_message = strdup(err.message ? err.message : "Failed to connect to D-Bus session bus");
        dbus_error_free(&err);
        return result;
    }

    const char *unique_name = dbus_bus_get_unique_name(conn);
    if (!unique_name) {
        result.status = 2;
        result.error_message = strdup("Failed to get D-Bus unique name");
        dbus_connection_unref(conn);
        return result;
    }

    /* Normalize unique name: skip ':' and replace '.' with '_' */
    const char *p_unique = unique_name;
    if (*p_unique == ':') p_unique++;
    char sender_norm[128];
    snprintf(sender_norm, sizeof(sender_norm), "%s", p_unique);
    for (char *c = sender_norm; *c; c++) {
        if (*c == '.') *c = '_';
    }

    /* token = "tamlinux%d" */
    char token[64];
    snprintf(token, sizeof(token), "tamlinux%d", (int)getpid());

    char predicted[256];
    snprintf(predicted, sizeof(predicted), "/org/freedesktop/portal/desktop/request/%s/%s", sender_norm, token);

    /* Subscribe up-front to the predicted response signal */
    char match_rule[512];
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Request',member='Response',path='%s'",
             predicted);
    dbus_bus_add_match(conn, match_rule, &err);
    if (dbus_error_is_set(&err)) {
        result.status = 2;
        result.error_message = strdup(err.message);
        dbus_error_free(&err);
        dbus_connection_unref(conn);
        return result;
    }
    dbus_connection_flush(conn);

    /* Prepare OpenFile method call */
    DBusMessage *msg = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser",
        "OpenFile"
    );
    if (!msg) {
        result.status = 2;
        result.error_message = strdup("Out of memory creating D-Bus message");
        dbus_connection_unref(conn);
        return result;
    }

    DBusMessageIter args_iter;
    dbus_message_iter_init_append(msg, &args_iter);

    const char *parent_window = "";
    dbus_message_iter_append_basic(&args_iter, DBUS_TYPE_STRING, &parent_window);
    dbus_message_iter_append_basic(&args_iter, DBUS_TYPE_STRING, &title);

    DBusMessageIter dict_iter;
    dbus_message_iter_open_container(&args_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);

    append_dict_string(&dict_iter, "handle_token", token);
    append_dict_bool(&dict_iter, "multiple", multiple ? TRUE : FALSE);

    if (directory) {
        append_dict_bool(&dict_iter, "directory", TRUE);
    } else if (extensions && *extensions) {
        append_filters(&dict_iter, extensions);
    }

    dbus_message_iter_close_container(&args_iter, &dict_iter);

    /* Send method call synchronously */
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, 10000, &err);
    dbus_message_unref(msg);

    if (!reply || dbus_error_is_set(&err)) {
        result.status = 2;
        result.error_message = strdup(err.message ? err.message : "Portal OpenFile method call failed");
        dbus_error_free(&err);
        dbus_connection_unref(conn);
        return result;
    }

    /* Extract actual request handle returned by OpenFile */
    char *actual_handle = NULL;
    DBusMessageIter reply_iter;
    if (dbus_message_iter_init(reply, &reply_iter)) {
        int arg_type = dbus_message_iter_get_arg_type(&reply_iter);
        if (arg_type == DBUS_TYPE_OBJECT_PATH || arg_type == DBUS_TYPE_STRING) {
            const char *h = NULL;
            dbus_message_iter_get_basic(&reply_iter, &h);
            if (h) actual_handle = strdup(h);
        }
    }
    dbus_message_unref(reply);

    /* If the handle returned by portal differs from predicted, subscribe to it too */
    if (actual_handle && strcmp(actual_handle, predicted) != 0) {
        char alt_match[512];
        snprintf(alt_match, sizeof(alt_match),
                 "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Request',member='Response',path='%s'",
                 actual_handle);
        dbus_bus_add_match(conn, alt_match, &err);
        if (!dbus_error_is_set(&err)) {
            dbus_connection_flush(conn);
        }
        dbus_error_free(&err);
    }

    /* Event loop: wait for Response signal until timeout */
    bool got_response = false;
    dbus_uint32_t response_code = 1;
    char **raw_uris = NULL;
    size_t raw_uri_count = 0;
    size_t raw_uri_cap = 0;

    struct timespec start_time, cur_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (!got_response) {
        clock_gettime(CLOCK_MONOTONIC, &cur_time);
        int elapsed_ms = (int)((cur_time.tv_sec - start_time.tv_sec) * 1000 +
                               (cur_time.tv_nsec - start_time.tv_nsec) / 1000000);
        if (elapsed_ms >= timeout_ms) {
            result.status = 2;
            result.error_message = strdup("Timed out waiting for file chooser response");
            break;
        }

        DBusMessage *incoming = dbus_connection_pop_message(conn);
        if (!incoming) {
            int wait_step = timeout_ms - elapsed_ms;
            if (wait_step > 250) wait_step = 250;
            if (!dbus_connection_read_write(conn, wait_step)) {
                result.status = 2;
                result.error_message = strdup("D-Bus connection lost");
                break;
            }
            incoming = dbus_connection_pop_message(conn);
        }

        if (!incoming) {
            continue;
        }

        if (dbus_message_is_signal(incoming, "org.freedesktop.portal.Request", "Response")) {
            const char *sig_path = dbus_message_get_path(incoming);
            if (sig_path && (strcmp(sig_path, predicted) == 0 ||
                             (actual_handle && strcmp(sig_path, actual_handle) == 0))) {
                got_response = true;

                DBusMessageIter sig_iter;
                if (dbus_message_iter_init(incoming, &sig_iter) &&
                    dbus_message_iter_get_arg_type(&sig_iter) == DBUS_TYPE_UINT32) {
                    dbus_message_iter_get_basic(&sig_iter, &response_code);
                    dbus_message_iter_next(&sig_iter);

                    if (dbus_message_iter_get_arg_type(&sig_iter) == DBUS_TYPE_ARRAY) {
                        DBusMessageIter res_dict_iter;
                        dbus_message_iter_recurse(&sig_iter, &res_dict_iter);
                        while (dbus_message_iter_get_arg_type(&res_dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                            DBusMessageIter res_entry_iter;
                            dbus_message_iter_recurse(&res_dict_iter, &res_entry_iter);
                            const char *r_key = NULL;
                            dbus_message_iter_get_basic(&res_entry_iter, &r_key);
                            dbus_message_iter_next(&res_entry_iter);

                            if (r_key && strcmp(r_key, "uris") == 0 &&
                                dbus_message_iter_get_arg_type(&res_entry_iter) == DBUS_TYPE_VARIANT) {
                                DBusMessageIter uri_var_iter;
                                dbus_message_iter_recurse(&res_entry_iter, &uri_var_iter);
                                if (dbus_message_iter_get_arg_type(&uri_var_iter) == DBUS_TYPE_ARRAY) {
                                    DBusMessageIter uri_arr_iter;
                                    dbus_message_iter_recurse(&uri_var_iter, &uri_arr_iter);
                                    while (dbus_message_iter_get_arg_type(&uri_arr_iter) == DBUS_TYPE_STRING) {
                                        const char *uri_str = NULL;
                                        dbus_message_iter_get_basic(&uri_arr_iter, &uri_str);
                                        if (uri_str) {
                                            if (raw_uri_count >= raw_uri_cap) {
                                                raw_uri_cap = raw_uri_cap ? raw_uri_cap * 2 : 8;
                                                char **next_uris = realloc(raw_uris, raw_uri_cap * sizeof(char *));
                                                if (!next_uris) break;
                                                raw_uris = next_uris;
                                            }
                                            raw_uris[raw_uri_count++] = strdup(uri_str);
                                        }
                                        dbus_message_iter_next(&uri_arr_iter);
                                    }
                                }
                            }
                            dbus_message_iter_next(&res_dict_iter);
                        }
                    }
                }
            }
        }
        dbus_message_unref(incoming);
    }

    if (actual_handle) free(actual_handle);

    if (got_response) {
        if (response_code == 0 && raw_uri_count > 0) {
            result.status = 0;
            result.count = raw_uri_count;
            result.paths = calloc(raw_uri_count + 1, sizeof(char *));
            if (result.paths) {
                for (size_t i = 0; i < raw_uri_count; i++) {
                    result.paths[i] = tam_portal_uri_to_path(raw_uris[i]);
                }
            }
        } else {
            result.status = 1; /* Nothing picked / cancelled */
            result.count = 0;
            result.paths = NULL;
        }
    }

    if (raw_uris) {
        for (size_t i = 0; i < raw_uri_count; i++) {
            free(raw_uris[i]);
        }
        free(raw_uris);
    }

    dbus_connection_unref(conn);
    return result;
}

void tam_portal_file_result_free(tam_portal_file_result_t *res) {
    if (!res) return;
    if (res->paths) {
        for (size_t i = 0; i < res->count; i++) {
            free(res->paths[i]);
        }
        free(res->paths);
        res->paths = NULL;
    }
    if (res->error_message) {
        free(res->error_message);
        res->error_message = NULL;
    }
    res->count = 0;
    res->status = 0;
}
