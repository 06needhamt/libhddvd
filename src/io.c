#include "internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif
int hddvd_path_exists(const char *p)
{
#ifdef _WIN32
    return p && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return p && stat(p, &st) == 0;
#endif
}
int hddvd_path_is_dir(const char *p)
{
#ifdef _WIN32
    DWORD attributes;
    if (!p)
        return 0;
    attributes = GetFileAttributesA(p);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
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
