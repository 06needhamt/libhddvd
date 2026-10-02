#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
static int dump_xmf(void *o, const char *n, const void *b, size_t z)
{
    return hddvd_xmf_dump(b, z, n, o);
}
static int report_application(const char *root, const char *manifest)
{
    hddvd_ctx *context = hddvd_open(root);
    hddvd_application *application = NULL;
    size_t i;
    int rc;
    if (!context)
        return HDDVD_EIO;
    rc = hddvd_application_open(context, manifest, &application);
    if (rc)
        fprintf(stderr, "Application inspection failed: %s\n", hddvd_strerror(rc));
    for (i = 0; !rc && i < hddvd_application_dependency_count(application); i++)
    {
        hddvd_dependency dependency;
        hddvd_application_get_dependency(application, i, &dependency);
        printf("Dependency kind=%d id=%s uri=%s status=%s\n", (int)dependency.kind,
               dependency.id ? dependency.id : "-", dependency.uri,
               hddvd_strerror(dependency.status));
    }
    for (i = 0; !rc && i < hddvd_application_requirement_count(application); i++)
    {
        hddvd_requirement requirement;
        hddvd_application_get_requirement(application, i, &requirement);
        printf("Requirement kind=%d namespace=%s name=%s occurrences=%zu supported=%s\n",
               (int)requirement.kind, requirement.namespace_uri ? requirement.namespace_uri : "-",
               requirement.name, requirement.occurrences, requirement.supported ? "yes" : "no");
    }
    hddvd_application_close(application);
    hddvd_close(context);
    return rc;
}
int main(int argc, char **argv)
{
    void *b = NULL;
    size_t n = 0;
    int rc;
    if (argc == 3)
        return report_application(argv[1], argv[2]) ? 1 : 0;
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s archive.aca\n       %s DISC_ROOT MANIFEST_URI\n", argv[0],
                argv[0]);
        return 2;
    }
    rc = hddvd_aca_visit_xmf(argv[1], dump_xmf, stdout);
    if (rc)
    {
        fprintf(stderr, "XMF inspection failed: %s\n", hddvd_strerror(rc));
        return 1;
    }
    rc = hddvd_aca_read_member(argv[1], "main.xmu", &b, &n);
    if (rc == HDDVD_OK)
    {
        rc = hddvd_xmu_dump(b, n, "main.xmu", stdout);
        hddvd_free(b);
    }
    return rc == HDDVD_ENOTFOUND ? 0 : (rc ? 1 : 0);
}
