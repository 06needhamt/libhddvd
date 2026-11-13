#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

/* hddvd_std_nav's opt-in guesses (HDDVD_STD_NAV_GUESS_*) on the synthetic
   disc in tests/fixtures/guesses (see generate_objects.py), which is laid
   out as DVD-Video would be where HD DVD's layout is unknown:
   - first play is one cell of HV000M01.EVO (sectors 8 to 11), then
     JumpSS to the title menu;
   - the title menu (VMG PGC 1, sectors 0 to 3, still until a button, the
     audio packet of sector 2 scrambled) has two buttons: 1 plays title 1,
     2 links to its next PGC, 2. PGC 2 (sectors 4 to 7) has two buttons: 1
     goes up to its parent, PGC 1, and 2 does nothing;
   - title 1 is PGC 1, cells of 8, 4, 4 and 4 sectors, cells 2 and 3 being
     an angle block, whose next PGC is PGC 2 (one cell of 8 sectors, whose
     command links to its previous PGC, 1);
   - the title set's root menu (HV001M01.EVO, one cell) has one button,
     which resumes. */
#define SECTOR 2048u
#define DISC HDDVD_FIXTURE_DIR "/guesses"
#define COPY "std-nav-guesses-disc"
#define ALL                                                                                        \
    (HDDVD_STD_NAV_GUESS_PGC_LINKS | HDDVD_STD_NAV_GUESS_ANGLES | HDDVD_STD_NAV_GUESS_MENU_CELLS)

static void playing(const hddvd_std_nav_step *step, size_t chapter, unsigned first,
                    unsigned sectors, unsigned pgc, unsigned cell)
{
    assert(step->kind == HDDVD_STD_NAV_PLAY && !step->reason);
    assert(step->title == 0 && step->chapter == chapter);
    assert(step->offset == (uint64_t)first * SECTOR && step->size == (uint64_t)sectors * SECTOR);
    assert(step->title_set == 1 && step->pgc == pgc && step->cell == cell);
}

static void menu(const hddvd_std_nav *nav, const hddvd_std_nav_step *step, unsigned set,
                 unsigned pgc, unsigned first, uint8_t still, size_t buttons)
{
    hddvd_std_nav_menu_cell cell;
    char uri[64];
    assert(step->kind == HDDVD_STD_NAV_MENU && !step->reason);
    assert(step->title == SIZE_MAX && step->offset == 0 && step->size == 4u * SECTOR);
    assert(step->title_set == set && step->pgc == pgc && step->cell == 1);
    assert(hddvd_std_nav_get_menu_cell(nav, &cell) == HDDVD_OK);
    snprintf(uri, sizeof(uri), "file:///dvddisc/HVDVD_TS/HV%03uM01.EVO", set);
    assert(!strcmp(cell.evo_uri, uri));
    assert(cell.first_sector == first && cell.sector_count == 4 && cell.still == still);
    assert(hddvd_std_nav_button_count(nav) == buttons);
}

static void unsupported(const hddvd_std_nav_step *step, const char *reason)
{
    assert(step->kind == HDDVD_STD_NAV_UNSUPPORTED && !strcmp(step->reason, reason));
}

static int provide(void *opaque, const char *evo_uri, uint32_t sector, uint8_t data[2048])
{
    int *calls = opaque;
    assert(!strcmp(evo_uri, "file:///dvddisc/HVDVD_TS/HV000M01.EVO") && sector == 2);
    (*calls)++;
    data[20] &= 0xcf;
    return HDDVD_OK;
}

/* Without guesses the disc plays as before: menu cells are refused, every
   cell of the angle block plays, a PGC without post commands ends
   playback, and LinkPrevPGC is not modelled. */
static void without(hddvd_ctx *ctx)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step;
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    unsupported(&step, "menu cells are not decoded");
    assert(hddvd_std_nav_select_title(nav, 0, 0, &step) == HDDVD_OK);
    playing(&step, 0, 0, 8, 1, 1);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 8, 4, 1, 2);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 12, 4, 1, 3);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 16, 4, 1, 4);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    assert(step.kind == HDDVD_STD_NAV_STOP && !strcmp(step.reason, "end of the program chain"));
    assert(hddvd_std_nav_select_title(nav, 0, 1, &step) == HDDVD_OK);
    playing(&step, 1, 20, 8, 2, 1);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    unsupported(&step, "link instruction is not modelled");
    hddvd_std_nav_close(nav);
}

static void with(hddvd_ctx *ctx)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step, again;
    hddvd_std_nav_button button;
    unsigned char data[4 * SECTOR];
    size_t got;
    int calls = 0;
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_set_options(nav, ALL) == HDDVD_OK);

    /* First play's cell, then the title menu. */
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    menu(nav, &step, 0, 1, 8, 0, 0);
    assert(hddvd_std_nav_read_menu(nav, 0, data, sizeof(data), &got) == HDDVD_OK);
    assert(got == sizeof(data) && data[3] == 0xba);
    assert(hddvd_std_nav_selected_button(nav) == 0);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    menu(nav, &step, 0, 1, 0, 255, 2);
    assert(hddvd_std_nav_get_button(nav, 0, &button) == HDDVD_OK);
    assert(button.x == 100 && button.y == 300 && button.width == 200 && button.height == 50);
    assert(button.up == 0 && button.down == 2 && button.right == 2 && !button.auto_action);
    assert(hddvd_std_nav_get_button(nav, 1, &button) == HDDVD_OK);
    assert(button.x == 400 && button.up == 1 && button.left == 1);
    assert(hddvd_std_nav_get_button(nav, 2, &button) == HDDVD_EINVAL);

    /* The cell reads as one stream; its scrambled sector needs a provider. */
    assert(hddvd_std_nav_read_menu(nav, 0, data, sizeof(data), &got) == HDDVD_EPROTECTED);
    assert(got == 2 * SECTOR);
    assert(hddvd_titles_set_sector_provider(hddvd_std_nav_titles(nav), provide, &calls) ==
           HDDVD_OK);
    assert(hddvd_std_nav_read_menu(nav, 100, data, sizeof(data), &got) == HDDVD_OK);
    assert(got == sizeof(data) - 100 && calls == 1);
    assert(data[SECTOR - 100 + 3] == 0xba);
    assert(hddvd_std_nav_read_menu(nav, 4u * SECTOR, data, 10, &got) == HDDVD_OK && got == 0);

    /* Button 1 is selected first; 2 links to the next PGC, whose button 2
       does nothing and whose button 1 goes back up. */
    assert(hddvd_std_nav_selected_button(nav) == 1);
    assert(hddvd_std_nav_select_button(nav, 0) == HDDVD_EINVAL);
    assert(hddvd_std_nav_select_button(nav, 3) == HDDVD_EINVAL);
    assert(hddvd_std_nav_select_button(nav, 2) == HDDVD_OK);
    assert(hddvd_std_nav_selected_button(nav) == 2);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_OK);
    menu(nav, &step, 0, 2, 4, 255, 2);
    assert(hddvd_std_nav_select_button(nav, 2) == HDDVD_OK);
    assert(hddvd_std_nav_activate_button(nav, &again) == HDDVD_OK);
    assert(!memcmp(&again, &step, sizeof(step)));
    menu(nav, &again, 0, 2, 4, 255, 2);
    assert(hddvd_std_nav_select_button(nav, 1) == HDDVD_OK);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_OK);
    menu(nav, &step, 0, 1, 0, 255, 2);

    /* Button 1 plays title 1. The angle block plays one cell: angle 1 at
       first (SPRM3 is 0), and the cell after the block follows it. */
    assert(hddvd_std_nav_select_button(nav, 1) == HDDVD_OK);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_OK);
    playing(&step, 0, 0, 8, 1, 1);
    assert(hddvd_std_nav_get_menu_cell(nav, &(hddvd_std_nav_menu_cell){0}) == HDDVD_EINVAL);
    assert(hddvd_std_nav_read_menu(nav, 0, data, 1, &got) == HDDVD_EINVAL);
    assert(hddvd_std_nav_button_count(nav) == 0);
    assert(hddvd_std_nav_selected_button(nav) == 0);
    assert(hddvd_std_nav_select_button(nav, 1) == HDDVD_EINVAL);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_EINVAL);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 8, 4, 1, 2);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 16, 4, 1, 4);

    /* PGC 1 has no post commands: its next PGC plays, whose cell command
       links back to its previous PGC. */
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 1, 20, 8, 2, 1);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 0, 8, 1, 1);

    /* Angle 2 plays the block's second cell. */
    assert(hddvd_std_nav_set_angle(nav, 0) == HDDVD_EINVAL);
    assert(hddvd_std_nav_set_angle(nav, 10) == HDDVD_EINVAL);
    assert(hddvd_std_nav_set_angle(nav, 2) == HDDVD_OK);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 12, 4, 1, 3);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 16, 4, 1, 4);
    /* An angle the block lacks plays its first. */
    assert(hddvd_std_nav_set_angle(nav, 5) == HDDVD_OK);
    assert(hddvd_std_nav_select_title(nav, 0, 0, &step) == HDDVD_OK);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 8, 4, 1, 2);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    playing(&step, 0, 16, 4, 1, 4);

    /* The title set's root menu is a menu cell; its button resumes. */
    assert(hddvd_std_nav_menu(nav, 3, &step) == HDDVD_OK);
    menu(nav, &step, 1, 1, 0, 255, 1);
    assert(hddvd_std_nav_get_button(nav, 0, &button) == HDDVD_OK);
    assert(button.x == 10 && button.y == 10 && button.width == 100 && button.height == 40);
    assert(hddvd_std_nav_read_menu(nav, 0, data, sizeof(data), &got) == HDDVD_OK);
    assert(got == sizeof(data));
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_OK);
    playing(&step, 0, 16, 4, 1, 4);
    hddvd_std_nav_close(nav);
}

static void copy(const char *from, const char *to)
{
    char path[512];
    FILE *in, *out;
    int c;
    snprintf(path, sizeof(path), DISC "/%s", from);
    in = fopen(path, "rb");
    snprintf(path, sizeof(path), COPY "/HVDVD_TS/%s", to);
    out = fopen(path, "wb");
    assert(in && out);
    while ((c = fgetc(in)) != EOF)
        assert(fputc(c, out) != EOF);
    fclose(in);
    assert(!fclose(out));
}

/* A copy of the disc with another VMG or title set IFO, and with or
   without the VMG's menu EVO. */
static hddvd_ctx *variant(const char *vmg, const char *vts, int menu_evo)
{
    hddvd_ctx *ctx;
    char path[64];
    MKDIR(COPY);
    MKDIR(COPY "/HVDVD_TS");
    copy(vmg, "HV000I01.IFO");
    copy(vts, "HV001I01.IFO");
    copy("HVDVD_TS/HV001T01.EVO", "HV001T01.EVO");
    copy("HVDVD_TS/HV001M01.EVO", "HV001M01.EVO");
    snprintf(path, sizeof(path), COPY "/HVDVD_TS/HV000M01.EVO");
    if (menu_evo)
        copy("HVDVD_TS/HV000M01.EVO", "HV000M01.EVO");
    else
        remove(path);
    ctx = hddvd_open(COPY);
    assert(ctx);
    return ctx;
}

static void failures(void)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step;
    hddvd_error_detail detail;
    unsigned char data[16];
    size_t got = 1;
    hddvd_ctx *ctx;

    assert(hddvd_std_nav_set_options(NULL, 0) == HDDVD_EINVAL);
    assert(hddvd_std_nav_set_angle(NULL, 1) == HDDVD_EINVAL);
    assert(hddvd_std_nav_get_menu_cell(NULL, NULL) == HDDVD_EINVAL);
    assert(hddvd_std_nav_read_menu(NULL, 0, data, 1, &got) == HDDVD_EINVAL && got == 0);
    assert(hddvd_std_nav_button_count(NULL) == 0);
    assert(hddvd_std_nav_get_button(NULL, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_std_nav_selected_button(NULL) == 0);
    assert(hddvd_std_nav_select_button(NULL, 1) == HDDVD_EINVAL);
    assert(hddvd_std_nav_activate_button(NULL, &step) == HDDVD_EINVAL);

    /* Interleaved angle cells are refused. */
    ctx = variant("HVDVD_TS/HV000I01.IFO", "angle-interleaved.IFO", 1);
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_set_options(nav, 8) == HDDVD_EINVAL);
    assert(hddvd_std_nav_set_options(nav, ALL) == HDDVD_OK);
    assert(hddvd_std_nav_select_title(nav, 0, 0, &step) == HDDVD_OK);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    unsupported(&step, "interleaved angle cells are not read");
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);

    /* A next PGC that does not exist. */
    ctx = variant("bad-next.IFO", "HVDVD_TS/HV001I01.IFO", 1);
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_set_options(nav, ALL) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    assert(hddvd_std_nav_select_button(nav, 2) == HDDVD_OK);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_OK);
    unsupported(&step, "link to a program chain that does not exist");
    /* Without the PGC link guess, the link is not followed at all. */
    assert(hddvd_std_nav_set_options(nav, HDDVD_STD_NAV_GUESS_MENU_CELLS) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    assert(hddvd_std_nav_next(nav, &step) == HDDVD_OK);
    assert(hddvd_std_nav_select_button(nav, 2) == HDDVD_OK);
    assert(hddvd_std_nav_activate_button(nav, &step) == HDDVD_OK);
    unsupported(&step, "link instruction is not modelled");
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);

    /* A first-play cell past the end of its EVO: the step stands (cells are
       not checked against the file), and reading it fails with the EVO
       named. */
    ctx = variant("fp-past-end.IFO", "HVDVD_TS/HV001I01.IFO", 1);
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_set_options(nav, ALL) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    menu(nav, &step, 0, 1, 12, 0, 0);
    assert(hddvd_std_nav_read_menu(nav, 0, data, sizeof(data), &got) == HDDVD_EPARSE);
    assert(got == 0);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK);
    assert(!strcmp(detail.resource, "file:///dvddisc/HVDVD_TS/HV000M01.EVO"));
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);

    /* Without the menu EVO the menu step stands, with no buttons, and
       reading it fails with the EVO named. */
    ctx = variant("HVDVD_TS/HV000I01.IFO", "HVDVD_TS/HV001I01.IFO", 0);
    assert(hddvd_std_nav_open(ctx, &nav) == HDDVD_OK);
    assert(hddvd_std_nav_set_options(nav, ALL) == HDDVD_OK);
    assert(hddvd_std_nav_start(nav, &step) == HDDVD_OK);
    menu(nav, &step, 0, 1, 8, 0, 0);
    assert(hddvd_std_nav_read_menu(nav, 0, data, sizeof(data), &got) == HDDVD_ENOTFOUND);
    assert(got == 0);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK);
    assert(!strcmp(detail.resource, "file:///dvddisc/HVDVD_TS/HV000M01.EVO"));
    hddvd_std_nav_close(nav);
    hddvd_close(ctx);
}

int main(void)
{
    hddvd_ctx *ctx = hddvd_open(DISC);
    assert(ctx);
    without(ctx);
    with(ctx);
    hddvd_close(ctx);
    failures();
    return 0;
}
