#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
int hddvd_path_exists(const char *p)
{
    struct stat st;
    return p && stat(p, &st) == 0;
}
int hddvd_path_is_dir(const char *p)
{
    struct stat st;
    return p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
char *hddvd_path_join(const char *a, const char *b)
{
    size_t na, nb;
    int sep;
    char *p;
    if (!a || !b) {
        return NULL;
    }
    na = strlen(a);
    nb = strlen(b);
    sep = na && a[na - 1] != '/' && a[na - 1] != '\\';
    p = malloc(na + nb + (size_t)sep + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, a, na);
    if (sep) {
        p[na++] = '/';
    }
    memcpy(p + na, b, nb + 1);
    return p;
}
