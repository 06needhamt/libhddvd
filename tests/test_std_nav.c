#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

/* The original Standard Content disc in tests/fixtures/standard (see
   generate_objects.py):
   - first play sets GPRM0 to 1 and jumps to the title menu;
   - the title menu links to VMG PGC 2 when GPRM0 is 1, else plays title 1;
   - VMG PGC 2 calls the title set's root menu, which sets GPRM2 to 7 and
     jumps to title 2;
   - title 1 is PGC 1 (cells of 12 and 8 sectors; the second cell's command
     jumps to first play), title 2 is PGC 2 (16 sectors); both PGCs' post
     command plays title 1. */
#define SECTOR 2048u
#define COPY "std-nav-disc"

static void copy(const char *from, const char *to)
{
    char path[512];
    FILE *in, *out;
    int c;
    snprintf(path, sizeof(path), "%s/standard/%s", HDDVD_FIXTURE_DIR, from);
    in = fopen(path, "rb");
    snprintf(path, sizeof(path), COPY "/HVDVD_TS/%s", to);
    out = fopen(path, "wb");
    assert(in && out);
    while ((c = fgetc(in)) != EOF)
        assert(fputc(c, out) != EOF);
    fclose(in);
    assert(!fclose(out));
}

/* A copy of the disc with another VMG or title set IFO. */
static hddvd_ctx *variant(const char *vmg, const char *vts)
{
    hddvd_ctx *ctx;
    MKDIR(COPY);
    MKDIR(COPY "/HVDVD_TS");
    copy(vmg, "HV000I01.IFO");
    copy(vts, "HV001I01.IFO");
    copy("HVDVD_TS/HV001T01.EVO", "HV001T01.EVO");
    ctx = hddvd_open(COPY);
    assert(ctx);
    return ctx;
}

static void playing(const hddvd_std_nav_step *step, size_t title, size_t chapter, unsigned first,
                    unsigned sectors, unsigned pgc, unsigned cell)
{
    assert(step->kind == HDDVD_STD_NAV_PLAY && !step->reason);
    assert(step->title == title && step->chapter == chapter);
    assert(step->offset == (uint64_t)first * SECTOR && step->size == (uint64_t)sectors * SECTOR);
    assert(step->title_set == 1 && step->pgc == pgc && step->cell == cell);
}

static uint16_t reg(const hddvd_std_nav *nav, unsigned number)
{
    uint16_t value = 0xffff;
    assert(hddvd_vm_get_register(hddvd_std_nav_vm(nav), number, &value) == HDDVD_OK);
    return value;
}

static void as_authored(hddvd_ctx *ctx)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step;
    unsigned char sector[SECTOR];
    size_t got;
    int round;

    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK && nav);
    assert(hddvd_titles_count(hddvd_std_nav_titles(nav)) == 2);
    /* Nothing is playing yet. */
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_EINVAL);

    for (round = 0; round < 2; round++)
    {
        /* First play, the title menu, VMG PGC 2, the root menu, title 2. */
        assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
        playing(&step, 1, 0, 0, 16, 2, 1);
        assert(reg(nav, 0) == 1 && reg(nav, 2) == 7);
        assert(reg(nav, 0x84) == 2 && reg(nav, 0x85) == 2 && reg(nav, 0x86) == 2);
        /* The step's bytes are the title's. */
        assert(hddvd_titles_read(hddvd_std_nav_titles(nav), step.title, step.offset, sector, SECTOR,
                                 &got) == HDDVD_OK &&
               got == SECTOR && sector[3] == 0xba);
        /* Its post command plays title 1, cell by cell. */
        assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
        playing(&step, 0, 0, 0, 12, 1, 1);
        assert(reg(nav, 0x84) == 1 && reg(nav, 0x86) == 1);
        assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
        playing(&step, 0, 1, 12, 8, 1, 2);
        /* The second cell's command returns to first play. */
        if (round == 0)
        {
            hddvd_std_nav_step again;
            assert(hddvd_std_nav_next(nav, &again) == HDDVD_OK);
            playing(&again, 1, 0, 0, 16, 2, 1);
        }
    }

    /* The user's choices. Title 1 chapter 2 starts at its second cell,
       after the PGC's pre commands. */
    assert(hddvd_std_nav_select_title(nav, 0, 1, &step) == HDDVD_OK);
    playing(&step, 0, 1, 12, 8, 1, 2);
    assert(hddvd_std_nav_select_title(nav, 1, 0, &step) == HDDVD_OK);
    playing(&step, 1, 0, 0, 16, 2, 1);
    assert(hddvd_std_nav_select_title(nav, 2, 0, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_select_title(nav, 0, 2, &step) == HDDVD_EINVAL);
    /* A refused choice leaves the cell playing. */
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 0, 0, 12, 1, 1);

    /* The title menu with GPRM0 cleared plays title 1; the root menu jumps
       to title 2. Menus come from the first unit when the language has
       none. */
    assert(hddvd_vm_set_register(hddvd_std_nav_vm(nav), 0, 0) == HDDVD_OK);
    assert(hddvd_std_nav_set_language(nav, "fr") == HDDVD_OK);
    assert(hddvd_std_nav_menu(nav, 2, &step) == HDDVD_OK);
    playing(&step, 0, 0, 0, 12, 1, 1);
    assert(hddvd_std_nav_set_language(nav, "en") == HDDVD_OK);
    assert(hddvd_vm_set_register(hddvd_std_nav_vm(nav), 2, 0) == HDDVD_OK);
    assert(hddvd_std_nav_menu(nav, 3, &step) == HDDVD_OK);
    playing(&step, 1, 0, 0, 16, 2, 1);
    assert(reg(nav, 2) == 7);
    assert(hddvd_std_nav_menu(nav, 5, &step) == HDDVD_ENOTFOUND);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 0, 0, 12, 1, 1);

    assert(hddvd_std_nav_set_language(nav, "eng") == HDDVD_EINVAL);
    assert(hddvd_std_nav_set_language(nav, NULL) == HDDVD_OK);
    assert(hddvd_std_nav_set_language(NULL, "en") == HDDVD_EINVAL);
    assert(hddvd_std_nav_start(NULL, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_start(nav, NULL) == HDDVD_EINVAL);
    assert(hddvd_std_nav_next(NULL, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_next(nav, NULL) == HDDVD_EINVAL);
    assert(hddvd_std_nav_select_title(NULL, 0, 0, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_select_title(nav, 0, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_std_nav_menu(NULL, 2, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_menu(nav, 2, NULL) == HDDVD_EINVAL);
    assert(!hddvd_std_nav_titles(NULL) && !hddvd_std_nav_vm(NULL));
    hddvd_std_nav_close(nav);
    hddvd_std_nav_close(NULL);
}

static void failures(void)
{
    hddvd_std_nav *nav = (hddvd_std_nav *)&nav;
    hddvd_std_nav_step step;
    hddvd_error_detail detail;
    hddvd_ctx *ctx;
    char root[512];

    /* Not a Standard Content disc. */
    snprintf(root, sizeof(root), "%s/app", HDDVD_FIXTURE_DIR);
    ctx = hddvd_open(root);
    assert(ctx && hddvd_std_nav_open(ctx, &nav) == HDDVD_ENOTFOUND && !nav);
    assert(hddvd_std_nav_open(ctx, NULL) == HDDVD_EINVAL);
    assert(hddvd_std_nav_open(NULL, &nav) == HDDVD_EINVAL);
    hddvd_close(ctx);

    /* Post commands that link to a cell the PGC lacks. */
    ctx = variant("HVDVD_TS/HV000I01.IFO", "bad-link.IFO");
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_select_title(nav, 1, 0, &step) == HDDVD_OK);
    assert(step.kind == HDDVD_STD_NAV_PLAY);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    assert(step.kind == HDDVD_STD_NAV_UNSUPPORTED && strstr(step.reason, "cell"));
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_EINVAL);
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);

    /* A title menu that links to itself never plays anything. */
    ctx = variant("menu-loop.IFO", "HVDVD_TS/HV001I01.IFO");
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_ELIMIT);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_ELIMIT);
    /* The titles still play. */
    assert(hddvd_std_nav_select_title(nav, 0, 0, &step) == HDDVD_OK);
    assert(step.kind == HDDVD_STD_NAV_PLAY);
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);

    /* A title menu that resumes: nothing to resume at first play; from a
       title, the cell that was playing. */
    ctx = variant("menu-resume.IFO", "HVDVD_TS/HV001I01.IFO");
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    assert(step.kind == HDDVD_STD_NAV_UNSUPPORTED && strstr(step.reason, "resume"));
    assert(hddvd_std_nav_select_title(nav, 0, 1, &step) == HDDVD_OK);
    playing(&step, 0, 1, 12, 8, 1, 2);
    assert(hddvd_std_nav_menu(nav, 2, &step) == HDDVD_OK);
    playing(&step, 0, 1, 12, 8, 1, 2);
    /* Resumed once; the menu again has the same cell to return to. */
    assert(hddvd_std_nav_menu(nav, 2, &step) == HDDVD_OK);
    playing(&step, 0, 1, 12, 8, 1, 2);
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);
}

int main(void)
{
    char root[512];
    hddvd_ctx *ctx;
    snprintf(root, sizeof(root), "%s/standard", HDDVD_FIXTURE_DIR);
    ctx = hddvd_open(root);
    assert(ctx);
    as_authored(ctx);
    hddvd_close(ctx);
    failures();
    return 0;
}
