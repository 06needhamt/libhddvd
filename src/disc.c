#include "internal.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static hddvd_file_kind kind(const char *p)
{
    if (hddvd_ascii_endswith_ci(p, ".IFO"))
        return HDDVD_FILE_IFO;
    if (hddvd_ascii_endswith_ci(p, ".BUP"))
        return HDDVD_FILE_BUP;
    if (hddvd_ascii_endswith_ci(p, ".VTI"))
        return HDDVD_FILE_VTI;
    if (hddvd_ascii_endswith_ci(p, ".MAP"))
        return HDDVD_FILE_MAP;
    if (hddvd_ascii_endswith_ci(p, ".EVO"))
        return HDDVD_FILE_EVO;
    if (hddvd_ascii_endswith_ci(p, ".XPL"))
        return HDDVD_FILE_XPL;
    if (hddvd_ascii_endswith_ci(p, ".ACA"))
        return HDDVD_FILE_ACA;
    if (hddvd_ascii_endswith_ci(p, "DISCID.DAT"))
        return HDDVD_FILE_DISCID;
    return HDDVD_FILE_UNKNOWN;
}
const char *hddvd_file_kind_name(hddvd_file_kind k)
{
    static const char *n[] = {"unknown", "IFO", "BUP", "VTI", "MAP", "EVO", "XPL", "ACA", "DISCID"};
    return k <= HDDVD_FILE_DISCID ? n[k] : n[0];
}
static int scan_dir(const char *dir, hddvd_file_cb cb, void *op)
{
    hddvd_directory *d = hddvd_directory_open(dir);
    const char *name;
    if (!d)
        return HDDVD_ENOTFOUND;
    while ((name = hddvd_directory_read(d)))
    {
        char *p;
        struct hddvd_stat st;
        hddvd_file_info fi;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        p = hddvd_path_join(dir, name);
        if (!p)
        {
            hddvd_directory_close(d);
            return HDDVD_ENOMEM;
        }
        if (hddvd_stat(p, &st) == 0 && HDDVD_ISREG(st.st_mode))
        {
            memset(&fi, 0, sizeof(fi));
            fi.kind = kind(name);
            fi.size = (uint64_t)st.st_size;
            snprintf(fi.path, sizeof(fi.path), "%s", p);
            if (fi.kind != HDDVD_FILE_UNKNOWN && cb && cb(op, &fi))
            {
                free(p);
                hddvd_directory_close(d);
                return HDDVD_OK;
            }
        }
        free(p);
    }
    hddvd_directory_close(d);
    return HDDVD_OK;
}
int hddvd_scan_files(const hddvd_ctx *c, hddvd_file_cb cb, void *op)
{
    char *a, *h;
    int ra, rh, rc;
    if (!c || !cb)
    {
        hddvd_ctx_set_error(c, HDDVD_EINVAL, HDDVD_SUBSYSTEM_DISC, NULL,
                            "invalid disc scan request");
        return HDDVD_EINVAL;
    }
    a = hddvd_path_join(c->root, "ADV_OBJ");
    h = hddvd_path_join(c->root, "HVDVD_TS");
    if (!a || !h)
    {
        free(a);
        free(h);
        hddvd_ctx_set_error(c, HDDVD_ENOMEM, HDDVD_SUBSYSTEM_DISC, c->root, NULL);
        return HDDVD_ENOMEM;
    }
    ra = scan_dir(a, cb, op);
    rh = scan_dir(h, cb, op);
    free(a);
    free(h);
    rc = (ra == HDDVD_OK || rh == HDDVD_OK) ? HDDVD_OK : HDDVD_ENOTFOUND;
    if (rc == HDDVD_OK)
        hddvd_clear_error((hddvd_ctx *)c);
    else
        hddvd_ctx_set_error(c, rc, HDDVD_SUBSYSTEM_DISC, c->root,
                            "HD DVD content directories were not found");
    return rc;
}
