/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Standard Content navigation: follows first play, menus, jumps and links
   through the command VM until a cell of a title is to be played, as
   libdvdnav does for DVD-Video (docs/formats/ifo-map.md). */
#define MAX_TRANSFERS 1024u
#define SPRM_TITLE 0x84u
#define SPRM_TITLE_SET_TITLE 0x85u
#define SPRM_PGC 0x86u
#define SPRM_ANGLE 0x83u
#define SPRM_BUTTON 0x88u
#define SECTOR 2048u
/* DVD-Video's highlight information in a navigation pack's PCI: the button
   count at 0x60 + 17 and 18-byte button records from 0x8e, at most 36. */
#define PCI_HLI 0x60u
#define PCI_BUTTONS 0x8eu
#define BUTTON_RECORD 18u
#define MAX_BUTTONS 36u
#define GUESSES                                                                                    \
    (HDDVD_STD_NAV_GUESS_PGC_LINKS | HDDVD_STD_NAV_GUESS_ANGLES | HDDVD_STD_NAV_GUESS_MENU_CELLS)

typedef enum nav_domain
{
    DOMAIN_NONE = 0,
    DOMAIN_FIRST_PLAY,
    DOMAIN_VMG_MENU,
    DOMAIN_VTS_MENU,
    DOMAIN_TITLE
} nav_domain;

/* What to do next: run a PGC's pre commands, play (or run past) a cell,
   or run the post commands. */
typedef enum nav_stage
{
    STAGE_ENTER,
    STAGE_CELL,
    STAGE_POST,
    STAGE_DONE
} nav_stage;

typedef struct nav_set
{
    uint16_t number;
    hddvd_vts *vts;
} nav_set;

/* set: index into sets (title set menus and titles). unit: the menu
   language unit. pgc and cell are 0-based. */
typedef struct nav_position
{
    nav_domain domain;
    size_t set;
    size_t unit;
    size_t pgc;
    size_t cell;
} nav_position;

struct hddvd_std_nav
{
    hddvd_ctx *ctx;
    hddvd_vmg *vmg;
    nav_set *sets;
    size_t set_count;
    hddvd_titles *titles;
    hddvd_vm *vm;
    nav_position at;
    /* The cell to start at when the pre commands of the PGC entered end. */
    size_t entry_cell;
    /* A PLAY step is outstanding. */
    int playing;
    /* The title set last played; title set menus are its menus. */
    size_t set;
    int has_resume;
    nav_position resume;
    char language[3];
    unsigned options;
    /* The last step that played something, repeated when a button goes
       nowhere. */
    hddvd_std_nav_step last;
    /* The menu cell of the last MENU step, and its buttons. */
    char menu_uri[64];
    uint32_t menu_first;
    uint32_t menu_count;
    uint8_t menu_still;
    hddvd_stream *menu_stream;
    char menu_stream_uri[64];
    size_t button_count;
    hddvd_std_nav_button buttons[MAX_BUTTONS];
    uint8_t button_commands[MAX_BUTTONS][8];
};

static int fail(const hddvd_std_nav *nav, int rc, const char *message)
{
    hddvd_ctx_set_error(nav->ctx, rc, HDDVD_SUBSYSTEM_DISC, "file:///dvddisc/HVDVD_TS/HV000I01.IFO",
                        message);
    return rc;
}

static nav_stage stop(hddvd_std_nav_step *step, hddvd_std_nav_kind kind, const char *reason)
{
    step->kind = kind;
    step->reason = reason;
    return STAGE_DONE;
}

static nav_stage unsupported(hddvd_std_nav_step *step, const char *reason)
{
    return stop(step, HDDVD_STD_NAV_UNSUPPORTED, reason);
}

static const hddvd_vts *vts_at(const hddvd_std_nav *nav, const nav_position *p)
{
    return nav->sets[p->set].vts;
}

static int pgc_info(const hddvd_std_nav *nav, const nav_position *p, hddvd_vts_pgc *out)
{
    hddvd_menu_pgc menu;
    switch (p->domain)
    {
        case DOMAIN_FIRST_PLAY:
            return hddvd_vmg_get_first_play(nav->vmg, out);
        case DOMAIN_VMG_MENU:
            return hddvd_vmg_get_menu_pgc(nav->vmg, p->unit, p->pgc, &menu, out);
        case DOMAIN_VTS_MENU:
            return hddvd_vts_get_menu_pgc(vts_at(nav, p), p->unit, p->pgc, &menu, out);
        case DOMAIN_TITLE:
            return hddvd_vts_get_pgc(vts_at(nav, p), p->pgc, out);
        default:
            return HDDVD_EINVAL;
    }
}

static int get_command(const hddvd_std_nav *nav, const nav_position *p, hddvd_vts_command_kind kind,
                       size_t index, uint8_t command[8])
{
    switch (p->domain)
    {
        case DOMAIN_FIRST_PLAY:
            return hddvd_vmg_get_first_play_command(nav->vmg, kind, index, command);
        case DOMAIN_VMG_MENU:
            return hddvd_vmg_get_menu_command(nav->vmg, p->unit, p->pgc, kind, index, command);
        case DOMAIN_VTS_MENU:
            return hddvd_vts_get_menu_command(vts_at(nav, p), p->unit, p->pgc, kind, index,
                                              command);
        case DOMAIN_TITLE:
            return hddvd_vts_get_command(vts_at(nav, p), p->pgc, kind, index, command);
        default:
            return HDDVD_EINVAL;
    }
}

static int cell_info(const hddvd_std_nav *nav, const nav_position *p, size_t cell,
                     hddvd_vts_cell *out)
{
    switch (p->domain)
    {
        case DOMAIN_FIRST_PLAY:
            return hddvd_vmg_get_first_play_cell_internal(nav->vmg, cell, out);
        case DOMAIN_VMG_MENU:
            return hddvd_vmg_get_menu_cell(nav->vmg, p->unit, p->pgc, cell, out);
        case DOMAIN_VTS_MENU:
            return hddvd_vts_get_menu_cell(vts_at(nav, p), p->unit, p->pgc, cell, out);
        case DOMAIN_TITLE:
            return hddvd_vts_get_cell(vts_at(nav, p), p->pgc, cell, out);
        default:
            return HDDVD_EINVAL;
    }
}

static int links_info(const hddvd_std_nav *nav, const nav_position *p, hddvd_pgc_links *out)
{
    switch (p->domain)
    {
        case DOMAIN_FIRST_PLAY:
            return hddvd_vmg_get_pgc_links_internal(nav->vmg, 1, 0, 0, out);
        case DOMAIN_VMG_MENU:
            return hddvd_vmg_get_pgc_links_internal(nav->vmg, 0, p->unit, p->pgc, out);
        case DOMAIN_VTS_MENU:
            return hddvd_vts_get_pgc_links_internal(vts_at(nav, p), 1, p->unit, p->pgc, out);
        case DOMAIN_TITLE:
            return hddvd_vts_get_pgc_links_internal(vts_at(nav, p), 0, 0, p->pgc, out);
        default:
            return HDDVD_EINVAL;
    }
}

/* Under the angle guess: DVD's block mode (1 first, 2 middle, 3 last) and
   type (1 angle) in the category's top byte. */
static unsigned block_mode(uint32_t category)
{
    return category >> 30;
}

static int in_angle_block(uint32_t category)
{
    return ((category >> 28) & 3u) == 1u && block_mode(category);
}

/* At the first cell of an angle block, moves to the cell of the angle in
   SPRM3. */
static int pick_angle(hddvd_std_nav *nav, const hddvd_vts_pgc *info)
{
    hddvd_vts_cell cell;
    uint16_t angle = 0;
    size_t first = nav->at.cell, count = 1;
    if (cell_info(nav, &nav->at, first, &cell))
        return HDDVD_EPARSE;
    if (!in_angle_block(cell.category) || block_mode(cell.category) != 1)
        return HDDVD_OK;
    while (first + count < info->cell_count)
    {
        if (cell_info(nav, &nav->at, first + count, &cell) || !in_angle_block(cell.category) ||
            block_mode(cell.category) == 1)
            break;
        count++;
        if (block_mode(cell.category) == 3)
            break;
    }
    hddvd_vm_get_register(nav->vm, SPRM_ANGLE, &angle);
    if (!angle || angle > count)
        angle = 1;
    nav->at.cell = first + angle - 1u;
    if (cell_info(nav, &nav->at, nav->at.cell, &cell))
        return HDDVD_EPARSE;
    /* 1: interleaved, which is not read. */
    return (cell.category >> 26) & 1u ? 1 : HDDVD_OK;
}

/* The cell after the current one; under the angle guess, after the angle
   block the current cell is in. */
static void advance_cell(hddvd_std_nav *nav)
{
    hddvd_vts_cell cell;
    if ((nav->options & HDDVD_STD_NAV_GUESS_ANGLES) && nav->at.domain == DOMAIN_TITLE)
        while (!cell_info(nav, &nav->at, nav->at.cell, &cell) && in_angle_block(cell.category) &&
               block_mode(cell.category) != 3 &&
               !cell_info(nav, &nav->at, nav->at.cell + 1, &cell) &&
               in_angle_block(cell.category) && block_mode(cell.category) != 1)
            nav->at.cell++;
    nav->at.cell++;
}

/* Runs the current PGC's commands of one kind: all of them, or for a cell
   command the one numbered `only` (1-based), as DVD players run a single
   cell command. */
static int run_commands(hddvd_std_nav *nav, hddvd_vts_command_kind kind, size_t only,
                        hddvd_vm_result *result)
{
    hddvd_vts_pgc info;
    uint8_t *commands;
    size_t count, first = only ? only - 1 : 0, i;
    int rc = pgc_info(nav, &nav->at, &info);
    if (rc)
        return fail(nav, HDDVD_EPARSE, "program chain does not exist");
    count = kind == HDDVD_VTS_PRE_COMMAND    ? info.pre_command_count
            : kind == HDDVD_VTS_POST_COMMAND ? info.post_command_count
                                             : info.cell_command_count;
    memset(result, 0, sizeof(*result));
    if (only > count || !count)
        return HDDVD_OK;
    if (only)
        count = only;
    commands = malloc(count * 8u);
    if (!commands)
        return HDDVD_ENOMEM;
    for (i = first; i < count && !rc; i++)
        rc = get_command(nav, &nav->at, kind, i, commands + (i - first) * 8u);
    if (!rc)
        rc = hddvd_vm_run(nav->vm, commands, count - first, 0, result);
    free(commands);
    if (rc)
        return fail(nav, rc, "navigation commands failed");
    return HDDVD_OK;
}

/* The menu language unit to use: the preferred language's, else the first. */
static size_t menu_unit(const hddvd_std_nav *nav, int vmg, size_t set)
{
    size_t count =
        vmg ? hddvd_vmg_menu_unit_count(nav->vmg) : hddvd_vts_menu_unit_count(nav->sets[set].vts);
    size_t i;
    for (i = 0; nav->language[0] && i < count; i++)
    {
        hddvd_menu_unit unit;
        int rc = vmg ? hddvd_vmg_get_menu_unit(nav->vmg, i, &unit)
                     : hddvd_vts_get_menu_unit(nav->sets[set].vts, i, &unit);
        if (!rc && !strcmp(unit.language, nav->language))
            return i;
    }
    return 0;
}

/* Finds the menu with `menu_id` in a menu domain; an entry PGC is preferred. */
static int find_menu(const hddvd_std_nav *nav, nav_domain domain, size_t set, unsigned menu_id,
                     nav_position *out)
{
    int vmg = domain == DOMAIN_VMG_MENU, found = 0;
    hddvd_menu_unit unit;
    size_t i;
    memset(out, 0, sizeof(*out));
    out->domain = domain;
    out->set = set;
    out->unit = menu_unit(nav, vmg, set);
    if (vmg ? hddvd_vmg_get_menu_unit(nav->vmg, out->unit, &unit)
            : hddvd_vts_get_menu_unit(nav->sets[set].vts, out->unit, &unit))
        return HDDVD_ENOTFOUND;
    for (i = 0; i < unit.pgc_count; i++)
    {
        hddvd_menu_pgc menu;
        hddvd_vts_pgc pgc;
        int rc = vmg ? hddvd_vmg_get_menu_pgc(nav->vmg, out->unit, i, &menu, &pgc)
                     : hddvd_vts_get_menu_pgc(nav->sets[set].vts, out->unit, i, &menu, &pgc);
        if (rc || menu.menu_id != menu_id || (found && !menu.entry))
            continue;
        out->pgc = i;
        found = 1;
        if (menu.entry)
            break;
    }
    return found ? HDDVD_OK : HDDVD_ENOTFOUND;
}

/* The index of the VMG title that is title `number` of title set `set`. */
static int find_title(const hddvd_std_nav *nav, size_t set, unsigned number, size_t *out)
{
    size_t i;
    for (i = 0; i < hddvd_vmg_title_count(nav->vmg); i++)
    {
        hddvd_vmg_title title;
        if (hddvd_vmg_get_title(nav->vmg, i, &title) == HDDVD_OK &&
            title.title_set == nav->sets[set].number && title.title_set_title == number)
        {
            *out = i;
            return HDDVD_OK;
        }
    }
    return HDDVD_ENOTFOUND;
}

/* Moves to a chapter of a VMG title: its PGC, and the cell its program
   starts with. */
static int go_chapter(hddvd_std_nav *nav, size_t title, size_t chapter, nav_position *out,
                      size_t *cell)
{
    hddvd_vmg_title entry;
    hddvd_vts_chapter where;
    uint16_t first;
    size_t set;
    if (hddvd_vmg_get_title(nav->vmg, title, &entry))
        return HDDVD_ENOTFOUND;
    for (set = 0; set < nav->set_count && nav->sets[set].number != entry.title_set; set++)
        ;
    if (set == nav->set_count ||
        hddvd_vts_get_chapter(nav->sets[set].vts, (size_t)entry.title_set_title - 1, chapter,
                              &where) ||
        !where.pgc || !where.program ||
        hddvd_vts_get_program(nav->sets[set].vts, (size_t)where.pgc - 1, (size_t)where.program - 1,
                              &first) ||
        !first)
        return HDDVD_ENOTFOUND;
    memset(out, 0, sizeof(*out));
    out->domain = DOMAIN_TITLE;
    out->set = set;
    out->pgc = (size_t)where.pgc - 1;
    *cell = (size_t)first - 1;
    return HDDVD_OK;
}

/* The program (0-based) holding the current cell of a title PGC. */
static int current_program(const hddvd_std_nav *nav, const hddvd_vts_pgc *info, size_t *out)
{
    size_t i, best = 0;
    int found = 0;
    for (i = 0; i < info->program_count; i++)
    {
        uint16_t first;
        if (hddvd_vts_get_program(vts_at(nav, &nav->at), nav->at.pgc, i, &first) || !first)
            return HDDVD_ENOTFOUND;
        if ((size_t)first - 1 <= nav->at.cell)
        {
            best = i;
            found = 1;
        }
    }
    *out = best;
    return found ? HDDVD_OK : HDDVD_ENOTFOUND;
}

/* Fills a PLAY step for the current cell of a title PGC. */
static nav_stage play(hddvd_std_nav *nav, const hddvd_vts_pgc *info, hddvd_std_nav_step *step)
{
    hddvd_title_extent extent;
    size_t title = 0, index = 0, count = hddvd_titles_count(nav->titles), i;
    uint16_t pgc = (uint16_t)(nav->at.pgc + 1);
    int found = 0;
    /* The title the PGC's search pointer names, else any title of the
       set that plays the cell. */
    if (find_title(nav, nav->at.set, info->title, &title) == HDDVD_OK &&
        hddvd_titles_find_cell_internal(nav->titles, title, pgc, nav->at.cell, &index) == HDDVD_OK)
        found = 1;
    for (i = 0; !found && i < count; i++)
    {
        hddvd_title_info entry;
        hddvd_titles_get_info(nav->titles, i, &entry);
        if (entry.title_set == nav->sets[nav->at.set].number &&
            hddvd_titles_find_cell_internal(nav->titles, i, pgc, nav->at.cell, &index) == HDDVD_OK)
        {
            title = i;
            found = 1;
        }
    }
    if (!found)
        return unsupported(step, "cell is not part of a listed title");
    hddvd_titles_get_extent(nav->titles, title, index, &extent);
    step->kind = HDDVD_STD_NAV_PLAY;
    step->title = title;
    step->offset = extent.offset;
    step->size = (uint64_t)extent.sector_count * 2048u;
    step->title_set = nav->sets[nav->at.set].number;
    step->pgc = pgc;
    step->cell = (uint16_t)(nav->at.cell + 1);
    hddvd_titles_chapter_at(nav->titles, title, extent.offset, &step->chapter);
    nav->playing = 1;
    nav->set = nav->at.set;
    hddvd_vm_set_register(nav->vm, SPRM_TITLE, (uint16_t)(title + 1));
    hddvd_vm_set_register(nav->vm, SPRM_TITLE_SET_TITLE, info->title);
    hddvd_vm_set_register(nav->vm, SPRM_PGC, pgc);
    return STAGE_DONE;
}

static uint16_t be16(const unsigned char *b)
{
    return (uint16_t)(b[0] << 8 | b[1]);
}

/* Reads sector `sector` (0-based) of the current menu cell, with the checks
   and the sector provider hddvd_titles_read uses. */
static int menu_sector(hddvd_std_nav *nav, uint32_t sector, unsigned char data[SECTOR])
{
    uint64_t at = ((uint64_t)nav->menu_first + sector) * SECTOR;
    size_t n = 0;
    int rc = HDDVD_OK;
    if (!nav->menu_stream || strcmp(nav->menu_stream_uri, nav->menu_uri))
    {
        hddvd_stream_close(nav->menu_stream);
        nav->menu_stream = NULL;
        rc = hddvd_open_resource(nav->ctx, nav->menu_uri, &nav->menu_stream);
        if (rc)
            return rc;
        memcpy(nav->menu_stream_uri, nav->menu_uri, sizeof(nav->menu_uri));
    }
    rc = hddvd_stream_read_at(nav->menu_stream, at, data, SECTOR, &n);
    if (!rc && n != SECTOR)
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = hddvd_titles_check_sector_internal(data);
    if (rc == HDDVD_EPROTECTED)
    {
        rc = hddvd_titles_provide_internal(nav->titles, nav->menu_uri, (uint32_t)(at / SECTOR),
                                           data);
        if (!rc)
            rc = hddvd_titles_check_sector_internal(data);
    }
    if (rc)
        hddvd_ctx_set_error(nav->ctx, rc, HDDVD_SUBSYSTEM_DISC, nav->menu_uri,
                            rc == HDDVD_EPROTECTED ? "encrypted menu sector"
                                                   : "menu sector cannot be read");
    return rc;
}

/* The buttons of the menu cell's first navigation pack: its PCI is the
   private-stream-2 packet whose first byte is 0, and the highlight
   information follows DVD-Video's layout. */
static void load_buttons(hddvd_std_nav *nav)
{
    unsigned char b[SECTOR];
    const unsigned char *pci = NULL;
    size_t at, length = 0, count, i;
    nav->button_count = 0;
    if (menu_sector(nav, 0, b))
        return;
    at = 14u + (b[13] & 7u);
    while (at + 7 <= SECTOR && !b[at] && !b[at + 1] && b[at + 2] == 1)
    {
        size_t size = (size_t)b[at + 4] << 8 | b[at + 5];
        if (b[at + 3] == 0xbf && !b[at + 6] && at + 6 + size <= SECTOR)
        {
            pci = b + at + 7;
            length = size - 1;
            break;
        }
        at += 6 + size;
    }
    if (!pci || length < PCI_BUTTONS || !(be16(pci + PCI_HLI) & 3u))
        return;
    count = pci[PCI_HLI + 17];
    if (count > MAX_BUTTONS || length < PCI_BUTTONS + count * BUTTON_RECORD)
        return;
    for (i = 0; i < count; i++)
    {
        const unsigned char *r = pci + PCI_BUTTONS + i * BUTTON_RECORD;
        hddvd_std_nav_button *button = &nav->buttons[i];
        unsigned x0 = (unsigned)(r[0] & 0x3f) << 4 | r[1] >> 4;
        unsigned x1 = (unsigned)(r[1] & 3) << 8 | r[2];
        unsigned y0 = (unsigned)(r[3] & 0x3f) << 4 | r[4] >> 4;
        unsigned y1 = (unsigned)(r[4] & 3) << 8 | r[5];
        uint8_t links[4];
        size_t k;
        memset(button, 0, sizeof(*button));
        button->x = x0;
        button->y = y0;
        button->width = x1 >= x0 ? x1 - x0 + 1 : 0;
        button->height = y1 >= y0 ? y1 - y0 + 1 : 0;
        for (k = 0; k < 4; k++)
        {
            links[k] = (uint8_t)(r[6 + k] & 0x3f);
            if (links[k] > count)
                links[k] = 0;
        }
        button->up = links[0];
        button->down = links[1];
        button->left = links[2];
        button->right = links[3];
        button->auto_action = (uint8_t)(r[3] >> 6 == 1);
        memcpy(nav->button_commands[i], r + 10, 8);
    }
    nav->button_count = count;
}

/* Fills a MENU step for the current cell of a menu or first-play PGC. */
static nav_stage menu_play(hddvd_std_nav *nav, hddvd_std_nav_step *step)
{
    hddvd_vts_cell cell;
    uint16_t set = nav->at.domain == DOMAIN_VTS_MENU ? nav->sets[nav->at.set].number : 0;
    if (cell_info(nav, &nav->at, nav->at.cell, &cell))
        return unsupported(step, "menu cell does not exist");
    if (cell.last_sector < cell.first_vobu_sector)
        return unsupported(step, "menu cell ends before it starts");
    snprintf(nav->menu_uri, sizeof(nav->menu_uri), "file:///dvddisc/HVDVD_TS/HV%03uM01.EVO",
             (unsigned)set);
    nav->menu_first = cell.first_vobu_sector;
    nav->menu_count = cell.last_sector - cell.first_vobu_sector + 1u;
    nav->menu_still = (uint8_t)(cell.category >> 8);
    step->kind = HDDVD_STD_NAV_MENU;
    step->title = SIZE_MAX;
    step->chapter = 0;
    step->offset = 0;
    step->size = (uint64_t)nav->menu_count * SECTOR;
    step->title_set = set;
    step->pgc = (uint16_t)(nav->at.pgc + 1);
    step->cell = (uint16_t)(nav->at.cell + 1);
    nav->playing = 1;
    load_buttons(nav);
    return STAGE_DONE;
}

/* What the current cell does: a title cell plays (the cell of the angle
   chosen, under that guess); a menu cell plays only under its guess. */
static nav_stage enter_cell(hddvd_std_nav *nav, const hddvd_vts_pgc *info, hddvd_std_nav_step *step)
{
    if (nav->at.domain != DOMAIN_TITLE)
        return nav->options & HDDVD_STD_NAV_GUESS_MENU_CELLS
                   ? menu_play(nav, step)
                   : unsupported(step, "menu cells are not decoded");
    if (nav->options & HDDVD_STD_NAV_GUESS_ANGLES)
    {
        int rc = pick_angle(nav, info);
        if (rc == 1)
            return unsupported(step, "interleaved angle cells are not read");
        if (rc)
            return unsupported(step, "angle block cell does not exist");
    }
    return play(nav, info, step);
}

/* Under the PGC link guess: moves to the current PGC's next, previous or
   parent PGC (0, 1, 2). */
static nav_stage link_pgc(hddvd_std_nav *nav, unsigned which, hddvd_std_nav_step *step)
{
    hddvd_pgc_links links;
    hddvd_vts_pgc info;
    nav_position to = nav->at;
    uint16_t number;
    if (!(nav->options & HDDVD_STD_NAV_GUESS_PGC_LINKS))
        return unsupported(step, "link instruction is not modelled");
    if (nav->at.domain == DOMAIN_FIRST_PLAY || links_info(nav, &nav->at, &links))
        return unsupported(step, "the first-play program chain has no PGC links");
    number = which == 0 ? links.next : which == 1 ? links.previous : links.parent;
    to.pgc = number ? (size_t)number - 1 : SIZE_MAX;
    if (!number || pgc_info(nav, &to, &info))
        return unsupported(step, "link to a program chain that does not exist");
    nav->at = to;
    nav->entry_cell = 0;
    return STAGE_ENTER;
}

/* The title set commands refer to: the one whose menu or title is
   running, else the one last played. */
static size_t current_set(const hddvd_std_nav *nav)
{
    return nav->at.domain == DOMAIN_VTS_MENU || nav->at.domain == DOMAIN_TITLE ? nav->at.set
                                                                               : nav->set;
}

/* Acts on what a command list ended with. `after` is the stage to go on
   with when it ran to its end. */
static nav_stage dispatch(hddvd_std_nav *nav, const hddvd_vm_result *r, nav_stage after,
                          hddvd_std_nav_step *step)
{
    hddvd_vts_pgc info;
    nav_position to;
    size_t title, cell = 0, program;
    switch (r->action)
    {
        case HDDVD_VM_END:
            return after;
        case HDDVD_VM_EXIT:
            return stop(step, HDDVD_STD_NAV_STOP, "Exit command");
        case HDDVD_VM_UNSUPPORTED:
            return unsupported(step, "navigation command is not modelled");
        case HDDVD_VM_JUMP_TITLE:
        case HDDVD_VM_JUMP_VTS_TITLE:
        case HDDVD_VM_JUMP_VTS_CHAPTER:
            if (r->action == HDDVD_VM_JUMP_TITLE)
                title = r->target ? (size_t)r->target - 1 : SIZE_MAX;
            else if (find_title(nav, current_set(nav), r->target, &title))
                title = SIZE_MAX;
            if (title == SIZE_MAX || go_chapter(nav, title,
                                                r->action == HDDVD_VM_JUMP_VTS_CHAPTER && r->chapter
                                                    ? (size_t)r->chapter - 1
                                                    : 0,
                                                &to, &cell))
                return unsupported(step, "jump to a title or chapter that does not exist");
            nav->at = to;
            /* JumpVTS_PTT starts at the chapter's program; the title jumps
               start at the PGC's first cell. */
            nav->entry_cell = r->action == HDDVD_VM_JUMP_VTS_CHAPTER ? cell : 0;
            return STAGE_ENTER;
        case HDDVD_VM_CALL_SYSTEM:
        case HDDVD_VM_JUMP_SYSTEM:
            if (r->action == HDDVD_VM_CALL_SYSTEM && nav->at.domain == DOMAIN_TITLE)
            {
                nav->resume = nav->at;
                if (r->resume_cell)
                    nav->resume.cell = (size_t)r->resume_cell - 1;
                nav->has_resume = 1;
            }
            memset(&to, 0, sizeof(to));
            if (r->domain == HDDVD_VM_FIRST_PLAY)
            {
                to.domain = DOMAIN_FIRST_PLAY;
                if (hddvd_vmg_get_first_play(nav->vmg, &info))
                    return unsupported(step, "jump to a first-play program chain the disc lacks");
            }
            else if (r->domain == HDDVD_VM_VMG_PGC)
            {
                to.domain = DOMAIN_VMG_MENU;
                to.unit = menu_unit(nav, 1, 0);
                to.pgc = r->target ? (size_t)r->target - 1 : SIZE_MAX;
                if (pgc_info(nav, &to, &info))
                    return unsupported(step, "jump to a menu that does not exist");
            }
            else if (find_menu(nav,
                               r->domain == HDDVD_VM_VMG_MENU ? DOMAIN_VMG_MENU : DOMAIN_VTS_MENU,
                               current_set(nav), r->target, &to))
                return unsupported(step, "jump to a menu that does not exist");
            nav->at = to;
            nav->entry_cell = 0;
            return STAGE_ENTER;
        case HDDVD_VM_LINK:
            break;
        default:
            return unsupported(step, "navigation command is not modelled");
    }

    if (pgc_info(nav, &nav->at, &info))
        return unsupported(step, "program chain does not exist");
    switch (r->link)
    {
        case HDDVD_VM_LINK_PGC:
            to = nav->at;
            to.pgc = r->target ? (size_t)r->target - 1 : SIZE_MAX;
            if (nav->at.domain == DOMAIN_FIRST_PLAY || pgc_info(nav, &to, &info))
                return unsupported(step, "link to a program chain that does not exist");
            nav->at = to;
            nav->entry_cell = 0;
            return STAGE_ENTER;
        case HDDVD_VM_LINK_CELL:
            if (!r->target || r->target > info.cell_count)
                return unsupported(step, "link to a cell that does not exist");
            nav->at.cell = (size_t)r->target - 1;
            return STAGE_CELL;
        case HDDVD_VM_LINK_PROGRAM:
        case HDDVD_VM_LINK_CHAPTER:
            if (nav->at.domain != DOMAIN_TITLE)
                return unsupported(step, "program link in a menu (menu cells are not decoded)");
            if (r->link == HDDVD_VM_LINK_CHAPTER)
            {
                if (!r->target || find_title(nav, nav->at.set, info.title, &title) ||
                    go_chapter(nav, title, (size_t)r->target - 1, &to, &cell))
                    return unsupported(step, "link to a chapter that does not exist");
                nav->at = to;
            }
            else
            {
                uint16_t first;
                if (!r->target ||
                    hddvd_vts_get_program(vts_at(nav, &nav->at), nav->at.pgc, (size_t)r->target - 1,
                                          &first) ||
                    !first)
                    return unsupported(step, "link to a program that does not exist");
                cell = (size_t)first - 1;
            }
            nav->at.cell = cell;
            return STAGE_CELL;
        case HDDVD_VM_LINK_INSTRUCTION:
            break;
        default:
            return unsupported(step, "link is not modelled");
    }

    /* LinkSIns, numbered as in DVD-Video. */
    switch (r->target)
    {
        case 0:
            return after;
        case 1:
            return STAGE_CELL;
        case 2:
            advance_cell(nav);
            return STAGE_CELL;
        case 3:
            if (nav->at.cell)
                nav->at.cell--;
            return STAGE_CELL;
        case 5:
        case 6:
        case 7:
        {
            uint16_t first;
            if (nav->at.domain != DOMAIN_TITLE || current_program(nav, &info, &program))
                return unsupported(step, "program link in a menu (menu cells are not decoded)");
            if (r->target == 6)
                program++;
            else if (r->target == 7 && program)
                program--;
            /* Past the last program: the post commands. */
            if (program >= info.program_count)
                return STAGE_POST;
            if (hddvd_vts_get_program(vts_at(nav, &nav->at), nav->at.pgc, program, &first) ||
                !first)
                return unsupported(step, "link to a program that does not exist");
            nav->at.cell = (size_t)first - 1;
            return STAGE_CELL;
        }
        case 9:
            nav->entry_cell = 0;
            return STAGE_ENTER;
        case 10:
        case 11:
        case 12:
            return link_pgc(nav, r->target - 10u, step);
        case 13:
            return STAGE_POST;
        case 16:
            if (!nav->has_resume)
                return unsupported(step, "resume with nothing to resume");
            nav->at = nav->resume;
            nav->has_resume = 0;
            return STAGE_CELL;
        default:
            return unsupported(step, "link instruction is not modelled");
    }
}

/* Runs from `stage` until a cell plays or navigation ends. */
static int follow(hddvd_std_nav *nav, nav_stage stage, hddvd_std_nav_step *step)
{
    unsigned transfers = 0;
    memset(step, 0, sizeof(*step));
    nav->playing = 0;
    nav->button_count = 0;
    while (stage != STAGE_DONE)
    {
        hddvd_vm_result result;
        hddvd_vts_pgc info;
        int rc;
        if (++transfers > MAX_TRANSFERS)
            return fail(nav, HDDVD_ELIMIT, "navigation commands never reach a title");
        switch (stage)
        {
            case STAGE_ENTER:
                rc = run_commands(nav, HDDVD_VTS_PRE_COMMAND, 0, &result);
                if (rc)
                    return rc;
                nav->at.cell = nav->entry_cell;
                stage = dispatch(nav, &result, STAGE_CELL, step);
                break;
            case STAGE_CELL:
                if (pgc_info(nav, &nav->at, &info))
                    return fail(nav, HDDVD_EPARSE, "program chain does not exist");
                stage = nav->at.cell >= info.cell_count ? STAGE_POST : enter_cell(nav, &info, step);
                break;
            case STAGE_POST:
                rc = run_commands(nav, HDDVD_VTS_POST_COMMAND, 0, &result);
                if (rc)
                    return rc;
                /* The next PGC's number is not decoded, so a PGC whose post
                   commands do not leave it ends playback, unless its next
                   PGC is guessed. */
                stage = dispatch(nav, &result, STAGE_DONE, step);
                if (stage == STAGE_DONE && !step->kind)
                {
                    hddvd_pgc_links links;
                    if ((nav->options & HDDVD_STD_NAV_GUESS_PGC_LINKS) &&
                        nav->at.domain != DOMAIN_FIRST_PLAY && !links_info(nav, &nav->at, &links) &&
                        links.next)
                        stage = link_pgc(nav, 0, step);
                    else
                        stop(step, HDDVD_STD_NAV_STOP, "end of the program chain");
                }
                break;
            default:
                break;
        }
    }
    if (step->kind == HDDVD_STD_NAV_PLAY || step->kind == HDDVD_STD_NAV_MENU)
        nav->last = *step;
    hddvd_clear_error(nav->ctx);
    return HDDVD_OK;
}

void hddvd_std_nav_close(hddvd_std_nav *nav)
{
    size_t i;
    if (!nav)
        return;
    for (i = 0; nav->sets && i < nav->set_count; i++)
        hddvd_vts_close(nav->sets[i].vts);
    free(nav->sets);
    hddvd_vmg_close(nav->vmg);
    hddvd_titles_close(nav->titles);
    hddvd_vm_destroy(nav->vm);
    hddvd_stream_close(nav->menu_stream);
    free(nav);
}

static int open_ifo(hddvd_ctx *ctx, unsigned number, hddvd_stream **stream, char uri[64])
{
    snprintf(uri, 64, "file:///dvddisc/HVDVD_TS/HV%03uI01.IFO", number);
    return hddvd_open_resource(ctx, uri, stream);
}

int hddvd_std_nav_open(hddvd_ctx *ctx, hddvd_std_nav **out)
{
    hddvd_std_nav *nav;
    hddvd_stream *stream = NULL;
    char uri[64];
    size_t i, count;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx)
        return HDDVD_EINVAL;
    nav = calloc(1, sizeof(*nav));
    if (!nav)
        return HDDVD_ENOMEM;
    nav->ctx = ctx;
    /* The titles first: opening them checks the whole disc. */
    rc = hddvd_titles_open(ctx, &nav->titles);
    if (!rc)
        rc = hddvd_vm_create(1, &nav->vm);
    if (!rc)
        rc = open_ifo(ctx, 0, &stream, uri);
    if (!rc)
    {
        rc = hddvd_vmg_open_with_config(stream, &ctx->limits, &nav->vmg);
        hddvd_stream_close(stream);
    }
    count = rc ? 0 : hddvd_vmg_title_count(nav->vmg);
    if (!rc && !(nav->sets = calloc(count ? count : 1, sizeof(*nav->sets))))
        rc = HDDVD_ENOMEM;
    for (i = 0; !rc && i < count; i++)
    {
        hddvd_vmg_title title;
        size_t s;
        hddvd_vmg_get_title(nav->vmg, i, &title);
        for (s = 0; s < nav->set_count && nav->sets[s].number != title.title_set; s++)
            ;
        if (s < nav->set_count)
            continue;
        rc = open_ifo(ctx, title.title_set, &stream, uri);
        if (rc)
            break;
        nav->sets[s].number = title.title_set;
        rc = hddvd_vts_open_with_config(stream, &ctx->limits, &nav->sets[s].vts);
        hddvd_stream_close(stream);
        nav->set_count++;
    }
    if (rc)
    {
        hddvd_std_nav_close(nav);
        return rc;
    }
    hddvd_clear_error(ctx);
    *out = nav;
    return HDDVD_OK;
}

hddvd_titles *hddvd_std_nav_titles(const hddvd_std_nav *nav)
{
    return nav ? nav->titles : NULL;
}

hddvd_vm *hddvd_std_nav_vm(const hddvd_std_nav *nav)
{
    return nav ? nav->vm : NULL;
}

int hddvd_std_nav_set_language(hddvd_std_nav *nav, const char *language)
{
    if (!nav || (language && strlen(language) != 2))
        return HDDVD_EINVAL;
    memset(nav->language, 0, sizeof(nav->language));
    if (language)
        memcpy(nav->language, language, 2);
    return HDDVD_OK;
}

int hddvd_std_nav_select_title(hddvd_std_nav *nav, size_t title, size_t chapter,
                               hddvd_std_nav_step *step)
{
    nav_position to;
    size_t cell;
    if (!nav || !step || go_chapter(nav, title, chapter, &to, &cell))
        return HDDVD_EINVAL;
    nav->at = to;
    nav->entry_cell = cell;
    nav->has_resume = 0;
    return follow(nav, STAGE_ENTER, step);
}

int hddvd_std_nav_seek(hddvd_std_nav *nav, size_t title, uint64_t offset, hddvd_std_nav_step *step)
{
    hddvd_vmg_title entry;
    nav_position to;
    uint16_t pgc;
    size_t cell, set;
    if (!nav || !step || title >= hddvd_titles_count(nav->titles) ||
        hddvd_vmg_get_title(nav->vmg, title, &entry) ||
        hddvd_titles_cell_at_internal(nav->titles, title, offset, &pgc, &cell))
        return HDDVD_EINVAL;
    for (set = 0; set < nav->set_count && nav->sets[set].number != entry.title_set; set++)
        ;
    if (set == nav->set_count)
        return HDDVD_EINVAL;
    memset(&to, 0, sizeof(to));
    to.domain = DOMAIN_TITLE;
    to.set = set;
    to.pgc = (size_t)pgc - 1;
    to.cell = cell;
    nav->at = to;
    nav->has_resume = 0;
    /* No commands run: the cell plays as if playback had reached it. */
    return follow(nav, STAGE_CELL, step);
}

int hddvd_std_nav_start(hddvd_std_nav *nav, hddvd_std_nav_step *step)
{
    hddvd_vts_pgc info;
    if (!nav || !step)
        return HDDVD_EINVAL;
    /* A disc without a first-play PGC starts with its first title. */
    if (hddvd_vmg_get_first_play(nav->vmg, &info))
        return hddvd_std_nav_select_title(nav, 0, 0, step);
    memset(&nav->at, 0, sizeof(nav->at));
    nav->at.domain = DOMAIN_FIRST_PLAY;
    nav->entry_cell = 0;
    nav->has_resume = 0;
    return follow(nav, STAGE_ENTER, step);
}

int hddvd_std_nav_next(hddvd_std_nav *nav, hddvd_std_nav_step *step)
{
    hddvd_vts_cell cell;
    hddvd_vm_result result;
    nav_stage stage;
    int rc;
    if (!nav || !step || !nav->playing)
        return HDDVD_EINVAL;
    if (cell_info(nav, &nav->at, nav->at.cell, &cell))
        return fail(nav, HDDVD_EPARSE, "cell does not exist");
    memset(&result, 0, sizeof(result));
    rc = cell.command ? run_commands(nav, HDDVD_VTS_CELL_COMMAND, cell.command, &result) : HDDVD_OK;
    if (rc)
        return rc;
    memset(step, 0, sizeof(*step));
    nav->playing = 0;
    nav->button_count = 0;
    /* Without a cell command, or after one that ran through, the next
       cell plays. */
    if (result.action == HDDVD_VM_END ||
        (result.action == HDDVD_VM_LINK && result.link == HDDVD_VM_LINK_INSTRUCTION &&
         !result.target))
    {
        advance_cell(nav);
        return follow(nav, STAGE_CELL, step);
    }
    stage = dispatch(nav, &result, STAGE_CELL, step);
    if (stage == STAGE_DONE)
    {
        hddvd_clear_error(nav->ctx);
        return HDDVD_OK;
    }
    rc = follow(nav, stage, step);
    return rc;
}

int hddvd_std_nav_menu(hddvd_std_nav *nav, unsigned menu_id, hddvd_std_nav_step *step)
{
    nav_position to;
    if (!nav || !step)
        return HDDVD_EINVAL;
    if (find_menu(nav, menu_id == 2 ? DOMAIN_VMG_MENU : DOMAIN_VTS_MENU, nav->set, menu_id, &to))
        return HDDVD_ENOTFOUND;
    /* As CallSS: a title that was playing can be resumed. */
    if (nav->playing && nav->at.domain == DOMAIN_TITLE)
    {
        nav->resume = nav->at;
        nav->has_resume = 1;
    }
    nav->at = to;
    nav->entry_cell = 0;
    return follow(nav, STAGE_ENTER, step);
}

int hddvd_std_nav_set_options(hddvd_std_nav *nav, unsigned options)
{
    if (!nav || (options & ~GUESSES))
        return HDDVD_EINVAL;
    nav->options = options;
    return HDDVD_OK;
}

int hddvd_std_nav_set_angle(hddvd_std_nav *nav, unsigned angle)
{
    if (!nav || angle < 1 || angle > 9)
        return HDDVD_EINVAL;
    return hddvd_vm_set_register(nav->vm, SPRM_ANGLE, (uint16_t)angle);
}

static int menu_playing(const hddvd_std_nav *nav)
{
    return nav && nav->playing && nav->at.domain != DOMAIN_TITLE;
}

int hddvd_std_nav_get_menu_cell(const hddvd_std_nav *nav, hddvd_std_nav_menu_cell *out)
{
    if (!menu_playing(nav) || !out)
        return HDDVD_EINVAL;
    out->evo_uri = nav->menu_uri;
    out->first_sector = nav->menu_first;
    out->sector_count = nav->menu_count;
    out->still = nav->menu_still;
    return HDDVD_OK;
}

int hddvd_std_nav_read_menu(hddvd_std_nav *nav, uint64_t offset, void *buffer, size_t size,
                            size_t *got)
{
    unsigned char sector[SECTOR], *out = buffer;
    uint64_t end;
    size_t done = 0;
    if (got)
        *got = 0;
    if (!menu_playing(nav) || !got || (!buffer && size))
        return HDDVD_EINVAL;
    end = (uint64_t)nav->menu_count * SECTOR;
    while (done < size && offset < end)
    {
        size_t skip = (size_t)(offset % SECTOR), take = SECTOR - skip;
        int rc = menu_sector(nav, (uint32_t)(offset / SECTOR), sector);
        if (rc)
        {
            *got = done;
            return rc;
        }
        if (take > size - done)
            take = size - done;
        memcpy(out + done, sector + skip, take);
        done += take;
        offset += take;
    }
    *got = done;
    hddvd_clear_error(nav->ctx);
    return HDDVD_OK;
}

size_t hddvd_std_nav_button_count(const hddvd_std_nav *nav)
{
    return menu_playing(nav) ? nav->button_count : 0;
}

int hddvd_std_nav_get_button(const hddvd_std_nav *nav, size_t index, hddvd_std_nav_button *out)
{
    if (!menu_playing(nav) || !out || index >= nav->button_count)
        return HDDVD_EINVAL;
    *out = nav->buttons[index];
    return HDDVD_OK;
}

unsigned hddvd_std_nav_selected_button(const hddvd_std_nav *nav)
{
    uint16_t value = 0;
    unsigned button;
    if (!menu_playing(nav) || !nav->button_count)
        return 0;
    hddvd_vm_get_register(nav->vm, SPRM_BUTTON, &value);
    button = value / 1024u;
    return button && button <= nav->button_count ? button : 1;
}

int hddvd_std_nav_select_button(hddvd_std_nav *nav, unsigned button)
{
    if (!menu_playing(nav) || !button || button > nav->button_count)
        return HDDVD_EINVAL;
    return hddvd_vm_set_register(nav->vm, SPRM_BUTTON, (uint16_t)(button * 1024u));
}

int hddvd_std_nav_activate_button(hddvd_std_nav *nav, hddvd_std_nav_step *step)
{
    hddvd_vm_result result;
    hddvd_std_nav_step again;
    nav_stage stage;
    unsigned button;
    int rc;
    if (!menu_playing(nav) || !step || !nav->button_count)
        return HDDVD_EINVAL;
    button = hddvd_std_nav_selected_button(nav);
    rc = hddvd_vm_run(nav->vm, nav->button_commands[button - 1], 1, 0, &result);
    if (rc)
        return fail(nav, rc, "button command failed");
    again = nav->last;
    memset(step, 0, sizeof(*step));
    stage = dispatch(nav, &result, STAGE_DONE, step);
    if (stage == STAGE_DONE)
    {
        /* A command that goes nowhere: the menu stays. */
        if (!step->kind)
            *step = again;
        else
            nav->playing = 0;
        hddvd_clear_error(nav->ctx);
        return HDDVD_OK;
    }
    return follow(nav, stage, step);
}
