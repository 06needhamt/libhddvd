#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const char *p = "test.xpl";
    FILE *f = fopen(p, "wb");
    char *ref = NULL;
    assert(f);
    fputs("<?xml version='1.0'?><p:playlist xmlns:p='urn:test'><p:start><p:application "
          "ref='file:///ADV_OBJ/app.aca'/></p:start></p:playlist>",
          f);
    fclose(f);
    assert(hddvd_xpl_find_start_ref(p, &ref) == HDDVD_OK);
    assert(strcmp(ref, "file:///ADV_OBJ/app.aca") == 0);
    hddvd_free(ref);
    remove(p);
    return 0;
}
