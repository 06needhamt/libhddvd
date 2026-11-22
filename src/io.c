/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
int hddvd_path_exists(const char *p)
{
    struct hddvd_stat st;
    return p && hddvd_stat(p, &st) == 0;
}
int hddvd_path_is_dir(const char *p)
{
    struct hddvd_stat st;
    return p && hddvd_stat(p, &st) == 0 && HDDVD_ISDIR(st.st_mode);
}
char *hddvd_path_join(const char *a, const char *b)
{
    size_t na, nb;
    int sep;
    char *p;
    if (!a || !b)
        return NULL;
    na = strlen(a);
    nb = strlen(b);
    sep = na && a[na - 1] != '/' && a[na - 1] != '\\';
    p = malloc(na + nb + (size_t)sep + 1);
    if (!p)
        return NULL;
    memcpy(p, a, na);
    if (sep)
        p[na++] = '/';
    memcpy(p + na, b, nb + 1);
    return p;
}

/* Keep platform-specific directory handles out of the public API. */
#ifdef _WIN32
#include <io.h>
struct hddvd_directory
{
    intptr_t handle;
    struct __finddata64_t entry;
    int first;
};
#else
#include <dirent.h>
struct hddvd_directory
{
    DIR *handle;
};
#endif

hddvd_directory *hddvd_directory_open(const char *path)
{
#ifdef _WIN32
    char *pattern;
#endif
    hddvd_directory *directory = malloc(sizeof(*directory));
    if (!directory)
        return NULL;
#ifdef _WIN32
    pattern = hddvd_path_join(path, "*");
    if (!pattern)
    {
        free(directory);
        return NULL;
    }
    directory->handle = _findfirst64(pattern, &directory->entry);
    directory->first = 1;
    free(pattern);
    if (directory->handle == -1)
#else
    directory->handle = opendir(path);
    if (!directory->handle)
#endif
    {
        free(directory);
        return NULL;
    }
    return directory;
}

const char *hddvd_directory_read(hddvd_directory *directory)
{
#ifdef _WIN32
    if (directory->first)
        directory->first = 0;
    else if (_findnext64(directory->handle, &directory->entry) != 0)
        return NULL;
    return directory->entry.name;
#else
    struct dirent *entry = readdir(directory->handle);
    return entry ? entry->d_name : NULL;
#endif
}

void hddvd_directory_close(hddvd_directory *directory)
{
#ifdef _WIN32
    _findclose(directory->handle);
#else
    closedir(directory->handle);
#endif
    free(directory);
}
