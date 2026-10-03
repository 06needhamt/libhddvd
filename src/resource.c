#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int resolve_component(const char *directory, const char *name, char **out)
{
    hddvd_directory *dir = hddvd_directory_open(directory);
    const char *entry;
    char *match = NULL;
    if (!dir)
        return HDDVD_ENOTFOUND;
    while ((entry = hddvd_directory_read(dir)) != NULL)
    {
        if (!strcmp(entry, ".") || !strcmp(entry, ".."))
            continue;
        if (hddvd_ascii_equal_ci(entry, name))
        {
#ifndef _WIN32
            struct stat status;
            char *candidate = hddvd_path_join(directory, entry);
            if (!candidate)
            {
                hddvd_directory_close(dir);
                return HDDVD_ENOMEM;
            }
            if (lstat(candidate, &status) != 0 || S_ISLNK(status.st_mode))
            {
                free(candidate);
                continue;
            }
            free(candidate);
#endif
            if (match)
            {
                free(match);
                hddvd_directory_close(dir);
                return HDDVD_EPARSE;
            }
            match = hddvd_path_join(directory, entry);
            if (!match)
            {
                hddvd_directory_close(dir);
                return HDDVD_ENOMEM;
            }
        }
    }
    hddvd_directory_close(dir);
    if (!match)
        return HDDVD_ENOTFOUND;
    *out = match;
    return HDDVD_OK;
}

static int resolve_path(const char *root, const char *relative, char **out)
{
    char *current = hddvd_strdup(root);
    const char *cursor = relative;
    int rc = HDDVD_OK;
    if (!current)
        return HDDVD_ENOMEM;
    while (*cursor)
    {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        char *component = malloc(length + 1), *next;
        if (!component)
        {
            rc = HDDVD_ENOMEM;
            break;
        }
        memcpy(component, cursor, length);
        component[length] = 0;
        rc = resolve_component(current, component, &next);
        free(component);
        if (rc)
            break;
        free(current);
        current = next;
        if (!slash)
            break;
        cursor = slash + 1;
    }
    if (rc)
    {
        free(current);
        return rc;
    }
    *out = current;
    return HDDVD_OK;
}

int hddvd_open_resource(const hddvd_ctx *ctx, const char *text, hddvd_stream **out)
{
    hddvd_uri *uri;
    const char *relative;
    char *path = NULL;
    int is_archive;
    int rc;
    if (!ctx || !text || !out)
    {
        hddvd_ctx_set_error(ctx, HDDVD_EINVAL, HDDVD_SUBSYSTEM_RESOURCE, text,
                            "invalid resource request");
        return HDDVD_EINVAL;
    }
    *out = NULL;
    rc = hddvd_uri_parse(text, &uri);
    if (rc)
    {
        hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_URI, text, "invalid HD DVD resource URI");
        return rc;
    }
    relative = hddvd_uri_archive_name(uri) ? hddvd_uri_archive_name(uri) : hddvd_uri_disc_path(uri);
    is_archive = hddvd_uri_archive_name(uri) != NULL;
    rc = resolve_path(ctx->root, relative, &path);
    if (!rc && hddvd_uri_archive_name(uri))
    {
        hddvd_aca *archive;
        hddvd_stream *archive_stream = NULL;
        hddvd_limits archive_limits;
        archive_limits.max_aca_entries = ctx->limits.max_archive_members;
        archive_limits.max_aca_member_size = ctx->limits.max_archive_member_bytes;
        rc = hddvd_stream_open_file(path, &archive_stream);
        if (!rc)
            rc = hddvd_aca_open_stream_with_limits(archive_stream, &archive_limits, &archive);
        hddvd_stream_close(archive_stream);
        if (!rc)
        {
            rc = hddvd_aca_open_member(archive, hddvd_uri_member_name(uri), out);
            hddvd_aca_close(archive);
        }
    }
    else if (!rc)
        rc = hddvd_stream_open_file(path, out);
    if (!rc && (*out)->size > ctx->limits.max_resource_bytes)
    {
        hddvd_stream_close(*out);
        *out = NULL;
        rc = HDDVD_ELIMIT;
    }
    free(path);
    hddvd_uri_free(uri);
    if (rc == HDDVD_OK)
        hddvd_clear_error((hddvd_ctx *)ctx);
    else if (rc == HDDVD_ENOTFOUND)
        hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_RESOURCE, text,
                            "resource was not found within the disc root");
    else if (rc == HDDVD_EPARSE)
        hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_RESOURCE, text,
                            "resource name is ambiguous or malformed");
    else
        hddvd_ctx_set_error(ctx, rc, is_archive ? HDDVD_SUBSYSTEM_ARCHIVE : HDDVD_SUBSYSTEM_STREAM,
                            text, NULL);
    return rc;
}
