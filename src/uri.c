#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct hddvd_uri
{
    char *path;
    char *archive;
    char *member;
};

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        c = (unsigned char)(c + ('a' - 'A'));
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static int prefix_ci(const char *s, const char *prefix)
{
    while (*prefix)
    {
        unsigned char a = (unsigned char)*s;
        unsigned char b = (unsigned char)*prefix;
        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b + ('a' - 'A'));
        }
        if (!*s || a != b) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static int decode_segment(const char *start, size_t length, char **out)
{
    char *decoded = malloc(length + 1);
    size_t i = 0, j = 0;
    if (!decoded) {
        return HDDVD_ENOMEM;
    }
    while (i < length)
    {
        unsigned char c = (unsigned char)start[i++];
        if (c == '%')
        {
            int hi, lo;
            if (i + 1 >= length || (hi = hex_value((unsigned char)start[i])) < 0 ||
                (lo = hex_value((unsigned char)start[i + 1])) < 0)
            {
                free(decoded);
                return HDDVD_EPARSE;
            }
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        }
        if (!c || c == '/' || c == '\\' || c == '?' || c == '#' || c < 0x20)
        {
            free(decoded);
            return HDDVD_EPARSE;
        }
        decoded[j++] = (char)c;
    }
    decoded[j] = 0;
    if (!strcmp(decoded, ".") || !strcmp(decoded, "..") || !*decoded)
    {
        free(decoded);
        return HDDVD_EPARSE;
    }
    *out = decoded;
    return HDDVD_OK;
}

int hddvd_uri_parse(const char *text, hddvd_uri **out)
{
    const char prefix[] = "file:///dvddisc/";
    const char *cursor, *slash;
    char *normalized = NULL;
    size_t used = 0, capacity;
    hddvd_uri *uri;
    int rc = HDDVD_OK;

    if (!text || !out) {
        return HDDVD_EINVAL;
    }
    *out = NULL;
    if (!prefix_ci(text, prefix)) {
        return HDDVD_EPARSE;
    }
    cursor = text + sizeof(prefix) - 1;
    if (!*cursor) {
        return HDDVD_EPARSE;
    }
    capacity = strlen(cursor) + 1;
    normalized = malloc(capacity);
    if (!normalized) {
        return HDDVD_ENOMEM;
    }
    while (*cursor)
    {
        char *segment;
        size_t length;
        slash = strchr(cursor, '/');
        length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        rc = decode_segment(cursor, length, &segment);
        if (rc) {
            goto fail;
        }
        if (used) {
            normalized[used++] = '/';
        }
        memcpy(normalized + used, segment, strlen(segment));
        used += strlen(segment);
        free(segment);
        if (!slash) {
            break;
        }
        cursor = slash + 1;
        if (!*cursor)
        {
            rc = HDDVD_EPARSE;
            goto fail;
        }
    }
    normalized[used] = 0;
    uri = calloc(1, sizeof(*uri));
    if (!uri)
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    uri->path = normalized;
    cursor = normalized;
    while (*cursor)
    {
        slash = strchr(cursor, '/');
        if (slash)
        {
            size_t component = (size_t)(slash - cursor);
            if (component >= 4 && cursor[component - 4] == '.' &&
                (cursor[component - 3] == 'a' || cursor[component - 3] == 'A') &&
                (cursor[component - 2] == 'c' || cursor[component - 2] == 'C') &&
                (cursor[component - 1] == 'a' || cursor[component - 1] == 'A'))
            {
                size_t archive_length = (size_t)(slash - normalized);
                uri->archive = malloc(archive_length + 1);
                if (!uri->archive)
                {
                    hddvd_uri_free(uri);
                    return HDDVD_ENOMEM;
                }
                memcpy(uri->archive, normalized, archive_length);
                uri->archive[archive_length] = 0;
                uri->member = hddvd_strdup(slash + 1);
                if (!uri->member)
                {
                    hddvd_uri_free(uri);
                    return HDDVD_ENOMEM;
                }
                if (strchr(uri->member, '/'))
                {
                    hddvd_uri_free(uri);
                    return HDDVD_EPARSE;
                }
                break;
            }
        }
        if (!slash) {
            break;
        }
        cursor = slash + 1;
    }
    *out = uri;
    return HDDVD_OK;
fail:
    free(normalized);
    return rc;
}

void hddvd_uri_free(hddvd_uri *uri)
{
    if (uri)
    {
        free(uri->path);
        free(uri->archive);
        free(uri->member);
        free(uri);
    }
}
const char *hddvd_uri_disc_path(const hddvd_uri *uri)
{
    return uri ? uri->path : NULL;
}
const char *hddvd_uri_archive_name(const hddvd_uri *uri)
{
    return uri ? uri->archive : NULL;
}
const char *hddvd_uri_member_name(const hddvd_uri *uri)
{
    return uri ? uri->member : NULL;
}
