#include "internal.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int resolve_component(const char *directory, const char *name, char **out)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    char *match = NULL;
    if (!dir) return HDDVD_ENOTFOUND;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (hddvd_ascii_equal_ci(entry->d_name, name)) {
#ifndef _WIN32
            struct stat status;
            char *candidate = hddvd_path_join(directory, entry->d_name);
            if (!candidate) { closedir(dir); return HDDVD_ENOMEM; }
            if (lstat(candidate, &status) != 0 || S_ISLNK(status.st_mode)) {
                free(candidate); continue;
            }
            free(candidate);
#endif
            if (match) { free(match); closedir(dir); return HDDVD_EPARSE; }
            match = hddvd_path_join(directory, entry->d_name);
            if (!match) { closedir(dir); return HDDVD_ENOMEM; }
        }
    }
    closedir(dir);
    if (!match) return HDDVD_ENOTFOUND;
    *out = match;
    return HDDVD_OK;
}

static int resolve_path(const char *root, const char *relative, char **out)
{
    char *current = hddvd_strdup(root);
    const char *cursor = relative;
    int rc = HDDVD_OK;
    if (!current) return HDDVD_ENOMEM;
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        char *component = malloc(length + 1), *next;
        if (!component) { rc = HDDVD_ENOMEM; break; }
        memcpy(component, cursor, length); component[length] = 0;
        rc = resolve_component(current, component, &next);
        free(component);
        if (rc) break;
        free(current); current = next;
        if (!slash) break;
        cursor = slash + 1;
    }
    if (rc) { free(current); return rc; }
    *out = current;
    return HDDVD_OK;
}

int hddvd_open_resource(const hddvd_ctx *ctx, const char *text, hddvd_stream **out)
{
    hddvd_uri *uri;
    const char *relative;
    char *path;
    int rc;
    if (!ctx || !text || !out) return HDDVD_EINVAL;
    *out = NULL;
    rc = hddvd_uri_parse(text, &uri);
    if (rc) return rc;
    relative = hddvd_uri_archive_name(uri) ? hddvd_uri_archive_name(uri)
                                           : hddvd_uri_disc_path(uri);
    rc = resolve_path(ctx->root, relative, &path);
    if (!rc && hddvd_uri_archive_name(uri)) {
        hddvd_aca *archive;
        rc = hddvd_aca_open(path, &archive);
        if (!rc) { rc = hddvd_aca_open_member(archive, hddvd_uri_member_name(uri), out); hddvd_aca_close(archive); }
    } else if (!rc) rc = hddvd_stream_open_file(path, out);
    free(path);
    hddvd_uri_free(uri);
    return rc;
}
