/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Standard Content IFO tables. Layout notes and evidence:
   docs/formats/ifo-map.md. Every table starts on a sector named by a
   pointer in the IFO's first sector and is read from one in-memory copy of
   the IFO, so each record is checked against its table's stated end and
   the file. */
#define IFO_SECTOR 2048u
#define VTS_LAST_SECTOR 0x0cu
#define VTS_MENU_EVO 0xc0u
#define VTS_TITLE_EVO 0xc4u
#define VTS_PTT_TABLE 0xc8u
#define VTS_PGC_TABLE 0xccu
#define VTS_TIME_MAP_TABLE 0xd4u
#define VTS_CELL_ADDRESS_TABLE 0xe0u
#define VTS_VOBU_MAP 0xe4u
#define VTS_MENU_TABLE 0xd0u
#define VMG_HEADER_END 0x80u
#define VMG_FIRST_PLAY 0x84u
#define VMG_TITLE_TABLE 0xc4u
#define VMG_MENU_TABLE 0xc8u
#define VMG_TITLE_SET_TABLE 0xd0u
#define MENU_UNIT_POINTER 8u
#define TABLE_HEADER 8u
#define PGC_SEARCH_POINTER 12u
#define PGC_TABLE_OFFSETS 0xa8u
#define PGC_HEADER 0xb0u
/* DVD-Video's next, previous and parent PGC numbers and still time, which
   hddvd_std_nav reads only when told to guess (unconfirmed for HD DVD). */
#define PGC_DVD_LINKS 0x9cu
#define CELL_RECORD 28u
#define CELL_POSITION 4u
#define CELL_ADDRESS 12u
#define COMMAND 8u
/* Pre, post and cell counts, then the table's last byte (4 bytes). */
#define COMMAND_HEADER 10u
#define VMG_TITLE 16u
/* Stream attributes in the first sector: video, then the audio count and
   8 audio records, then the subpicture count and 32 records (6 bytes each,
   as in DVD-Video; only one has been seen). */
#define VTS_VIDEO_ATTRIBUTES 0x214u
#define VTS_AUDIO_COUNT 0x218u
#define VTS_AUDIO_RECORDS 0x21au
#define AUDIO_RECORD 8u
#define MAX_AUDIO 8u
#define VTS_SUBPICTURE_COUNT 0x25au
#define VTS_SUBPICTURE_RECORDS 0x25cu
#define SUBPICTURE_RECORD 6u
#define MAX_SUBPICTURES 32u

typedef struct vts_pgc
{
    hddvd_vts_pgc info;
    uint16_t *programs;
    hddvd_vts_cell *cells;
    /* Pre, post, then cell commands. */
    unsigned char *commands;
    uint8_t map_unit;
    size_t map_count;
    hddvd_vts_map_entry *map;
    hddvd_pgc_links links;
} vts_pgc;

/* A menu language unit and its PGCs. */
typedef struct menu_unit
{
    hddvd_menu_unit info;
    hddvd_menu_pgc *categories;
    vts_pgc *pgcs;
} menu_unit;

struct hddvd_vts
{
    hddvd_vts_info info;
    size_t title_count;
    /* Title t's chapters are chapters[chapter_start[t] .. chapter_start[t + 1]). */
    size_t *chapter_start;
    hddvd_vts_chapter *chapters;
    size_t pgc_count;
    vts_pgc *pgcs;
    int has_time_maps;
    size_t evob_cell_count;
    hddvd_vts_evob_cell *evob_cells;
    size_t vobu_count;
    uint32_t *vobus;
    hddvd_video_attributes video;
    size_t stream_count[2];
    hddvd_stream_attributes audio[MAX_AUDIO];
    hddvd_stream_attributes subpictures[MAX_SUBPICTURES];
    size_t menu_unit_count;
    menu_unit *menu_units;
};

struct hddvd_vmg
{
    uint32_t last_sector;
    size_t title_set_count;
    size_t title_count;
    hddvd_vmg_title *titles;
    size_t menu_unit_count;
    menu_unit *menu_units;
    int has_first_play;
    vts_pgc first_play;
};

/* The IFO in memory. */
typedef struct ifo
{
    unsigned char *data;
    size_t size;
} ifo;

/* A table: where it starts in the IFO and how many bytes it spans. */
typedef struct table
{
    size_t base;
    size_t length;
} table;

static int get_command(const vts_pgc *, hddvd_vts_command_kind, size_t, uint8_t[8]);

static uint16_t be16(const unsigned char *b)
{
    return (uint16_t)(b[0] << 8 | b[1]);
}

static uint32_t be32(const unsigned char *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

/* Nonzero when [offset, offset + size) lies inside the table. */
static int fits(const table *t, size_t offset, size_t size)
{
    return offset <= t->length && size <= t->length - offset;
}

static const unsigned char *at(const ifo *f, const table *t, size_t offset)
{
    return f->data + t->base + offset;
}

static int read_ifo(hddvd_stream *stream, const hddvd_config_limits *config, hddvd_ifo_kind kind,
                    ifo *out)
{
    hddvd_ifo_header header;
    size_t got = 0;
    int rc;
    memset(out, 0, sizeof(*out));
    rc = hddvd_ifo_read_header(stream, &header);
    if (rc)
        return rc;
    if (header.kind != kind)
        return HDDVD_ENOTSUP;
    if (header.size > config->max_resource_bytes || header.size > SIZE_MAX)
        return HDDVD_ELIMIT;
    out->size = (size_t)header.size;
    out->data = malloc(out->size);
    if (!out->data)
        return HDDVD_ENOMEM;
    rc = hddvd_stream_read_at(stream, 0, out->data, out->size, &got);
    if (!rc && got != out->size)
        rc = HDDVD_EPARSE;
    if (rc)
    {
        free(out->data);
        out->data = NULL;
    }
    return rc;
}

/* Locates the table that the pointer at `field` names. Tables with a count
   and end address (`end_at` 4) span end + 1 bytes; the VOBU map keeps its
   end address first (`end_at` 0). A zero pointer is absent: ENOTFOUND. */
static int locate(const ifo *f, size_t field, size_t end_at, table *out)
{
    uint32_t sector = be32(f->data + field);
    size_t base;
    uint32_t end;
    if (!sector)
        return HDDVD_ENOTFOUND;
    if (sector >= f->size / IFO_SECTOR)
        return HDDVD_EPARSE;
    base = (size_t)sector * IFO_SECTOR;
    if (f->size - base < end_at + 4)
        return HDDVD_EPARSE;
    end = be32(f->data + base + end_at);
    if (end >= f->size - base)
        return HDDVD_EPARSE;
    out->base = base;
    out->length = (size_t)end + 1;
    return HDDVD_OK;
}

static int bcd(unsigned char b, unsigned limit, uint8_t *out)
{
    unsigned value = (unsigned)(b >> 4) * 10u + (b & 15u);
    if ((b >> 4) > 9 || (b & 15) > 9 || value >= limit)
        return HDDVD_EPARSE;
    *out = (uint8_t)value;
    return HDDVD_OK;
}

static int parse_time(const unsigned char *b, hddvd_vts_time *time)
{
    memset(time, 0, sizeof(*time));
    time->rate = (uint8_t)(b[3] >> 6);
    time->flag = (uint8_t)(b[2] >> 7);
    if (bcd(b[0], 100, &time->hours) || bcd(b[1], 60, &time->minutes) ||
        bcd((unsigned char)(b[2] & 0x7f), 60, &time->seconds) ||
        bcd((unsigned char)(b[3] & 0x3f), 100, &time->frames))
        return HDDVD_EPARSE;
    return HDDVD_OK;
}

int hddvd_vts_time_to_ticks(const hddvd_vts_time *time, hddvd_time *out)
{
    uint64_t frames;
    if (!time || !out)
        return HDDVD_EINVAL;
    if (time->rate != 2)
        return HDDVD_ENOTSUP;
    if (time->minutes >= 60 || time->seconds >= 60 || time->frames >= 30)
        return HDDVD_EINVAL;
    frames =
        (((uint64_t)time->hours * 60u + time->minutes) * 60u + time->seconds) * 30u + time->frames;
    out->ticks = frames * 3003u;
    return HDDVD_OK;
}

static void pgc_free(vts_pgc *pgc)
{
    free(pgc->programs);
    free(pgc->cells);
    free(pgc->commands);
    free(pgc->map);
}

static void menus_free(menu_unit *units, size_t count)
{
    size_t i, j;
    for (i = 0; units && i < count; i++)
    {
        for (j = 0; units[i].pgcs && j < units[i].info.pgc_count; j++)
            pgc_free(&units[i].pgcs[j]);
        free(units[i].pgcs);
        free(units[i].categories);
    }
    free(units);
}

void hddvd_vts_close(hddvd_vts *vts)
{
    size_t i;
    if (!vts)
        return;
    for (i = 0; i < vts->pgc_count; i++)
        pgc_free(&vts->pgcs[i]);
    menus_free(vts->menu_units, vts->menu_unit_count);
    free(vts->pgcs);
    free(vts->chapter_start);
    free(vts->chapters);
    free(vts->evob_cells);
    free(vts->vobus);
    free(vts);
}

static int parse_vobu_map(const ifo *f, hddvd_vts *vts)
{
    table t;
    size_t i;
    int rc = locate(f, VTS_VOBU_MAP, 0, &t);
    if (rc)
        return rc == HDDVD_ENOTFOUND ? HDDVD_EPARSE : rc;
    if (t.length < 8 || (t.length - 4) % 4)
        return HDDVD_EPARSE;
    vts->vobu_count = (t.length - 4) / 4;
    vts->vobus = malloc(vts->vobu_count * sizeof(*vts->vobus));
    if (!vts->vobus)
        return HDDVD_ENOMEM;
    for (i = 0; i < vts->vobu_count; i++)
    {
        vts->vobus[i] = be32(at(f, &t, 4 + i * 4));
        if (i && vts->vobus[i] <= vts->vobus[i - 1])
            return HDDVD_EPARSE;
    }
    return HDDVD_OK;
}

static int parse_cell_addresses(const ifo *f, hddvd_vts *vts)
{
    table t;
    size_t i;
    int rc = locate(f, VTS_CELL_ADDRESS_TABLE, 4, &t);
    if (rc)
        return rc == HDDVD_ENOTFOUND ? HDDVD_EPARSE : rc;
    if (t.length < TABLE_HEADER + CELL_ADDRESS || (t.length - TABLE_HEADER) % CELL_ADDRESS)
        return HDDVD_EPARSE;
    vts->evob_cell_count = (t.length - TABLE_HEADER) / CELL_ADDRESS;
    vts->evob_cells = malloc(vts->evob_cell_count * sizeof(*vts->evob_cells));
    if (!vts->evob_cells)
        return HDDVD_ENOMEM;
    for (i = 0; i < vts->evob_cell_count; i++)
    {
        const unsigned char *r = at(f, &t, TABLE_HEADER + i * CELL_ADDRESS);
        hddvd_vts_evob_cell *c = &vts->evob_cells[i];
        c->evob_id = be16(r);
        c->cell_id = r[3];
        c->first_sector = be32(r + 4);
        c->last_sector = be32(r + 8);
        /* Extents are in disc order and do not overlap. */
        if (c->last_sector < c->first_sector ||
            (i && c->first_sector <= vts->evob_cells[i - 1].last_sector))
            return HDDVD_EPARSE;
    }
    vts->info.title_evo_sectors = vts->evob_cells[vts->evob_cell_count - 1].last_sector + 1;
    return HDDVD_OK;
}

static int find_vobu(const hddvd_vts *vts, uint32_t sector, size_t *index)
{
    size_t low = 0, high = vts->vobu_count;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        if (vts->vobus[middle] < sector)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == vts->vobu_count || vts->vobus[low] != sector)
        return HDDVD_EPARSE;
    *index = low;
    return HDDVD_OK;
}

/* A cell must start and end on VOBUs, end where the next VOBU (or the EVO)
   starts, and lie inside its own address-table entry. */
static int check_cell(const hddvd_vts *vts, const hddvd_vts_cell *cell)
{
    size_t first, last, i;
    uint32_t end;
    if (cell->first_vobu_sector > cell->last_vobu_sector ||
        cell->last_vobu_sector > cell->last_sector ||
        find_vobu(vts, cell->first_vobu_sector, &first) ||
        find_vobu(vts, cell->last_vobu_sector, &last))
        return HDDVD_EPARSE;
    end = last + 1 < vts->vobu_count ? vts->vobus[last + 1] : vts->info.title_evo_sectors;
    if (cell->last_sector + 1 != end)
        return HDDVD_EPARSE;
    for (i = 0; i < vts->evob_cell_count; i++)
    {
        const hddvd_vts_evob_cell *c = &vts->evob_cells[i];
        if (c->evob_id == cell->evob_id && c->cell_id == cell->cell_id &&
            c->first_sector <= cell->first_vobu_sector && cell->last_sector <= c->last_sector)
            return HDDVD_OK;
    }
    return HDDVD_EPARSE;
}

/* vts is NULL for menu and first-play PGCs, whose cells are not checked
   against the title EVO's tables. */
static int parse_pgc(const ifo *f, const table *t, size_t offset, hddvd_vts *vts, vts_pgc *pgc)
{
    const unsigned char *h;
    size_t commands_at, programs_at, cells_at, positions_at, total, i;
    if (!fits(t, offset, PGC_HEADER))
        return HDDVD_EPARSE;
    h = at(f, t, offset);
    pgc->info.program_count = be16(h);
    pgc->info.cell_count = be16(h + 2);
    if (parse_time(h + 4, &pgc->info.playback_time))
        return HDDVD_EPARSE;
    if (pgc->info.program_count > pgc->info.cell_count)
        return HDDVD_EPARSE;
    pgc->links.next = be16(h + PGC_DVD_LINKS);
    pgc->links.previous = be16(h + PGC_DVD_LINKS + 2);
    pgc->links.parent = be16(h + PGC_DVD_LINKS + 4);
    pgc->links.still = h[PGC_DVD_LINKS + 6];
    commands_at = offset + be16(h + PGC_TABLE_OFFSETS);
    programs_at = offset + be16(h + PGC_TABLE_OFFSETS + 2);
    cells_at = offset + be16(h + PGC_TABLE_OFFSETS + 4);
    positions_at = offset + be16(h + PGC_TABLE_OFFSETS + 6);
    if (be16(h + PGC_TABLE_OFFSETS))
    {
        if (!fits(t, commands_at, COMMAND_HEADER))
            return HDDVD_EPARSE;
        pgc->info.pre_command_count = be16(at(f, t, commands_at));
        pgc->info.post_command_count = be16(at(f, t, commands_at + 2));
        pgc->info.cell_command_count = be16(at(f, t, commands_at + 4));
        total = (size_t)pgc->info.pre_command_count + pgc->info.post_command_count +
                pgc->info.cell_command_count;
        /* The end address must agree with the counts. */
        if (!fits(t, commands_at + COMMAND_HEADER, total * COMMAND) ||
            be32(at(f, t, commands_at + 6)) != COMMAND_HEADER + total * COMMAND - 1)
            return HDDVD_EPARSE;
        if (total)
        {
            pgc->commands = malloc(total * COMMAND);
            if (!pgc->commands)
                return HDDVD_ENOMEM;
            memcpy(pgc->commands, at(f, t, commands_at + COMMAND_HEADER), total * COMMAND);
        }
    }
    if (!pgc->info.cell_count)
        return HDDVD_OK;
    if (!be16(h + PGC_TABLE_OFFSETS + 2) || !be16(h + PGC_TABLE_OFFSETS + 4) ||
        !be16(h + PGC_TABLE_OFFSETS + 6) ||
        !fits(t, programs_at, (size_t)pgc->info.program_count * 2) ||
        !fits(t, cells_at, (size_t)pgc->info.cell_count * CELL_RECORD) ||
        !fits(t, positions_at, (size_t)pgc->info.cell_count * CELL_POSITION))
        return HDDVD_EPARSE;
    pgc->programs = malloc(((size_t)pgc->info.program_count + 1) * sizeof(*pgc->programs));
    pgc->cells = malloc((size_t)pgc->info.cell_count * sizeof(*pgc->cells));
    if (!pgc->programs || !pgc->cells)
        return HDDVD_ENOMEM;
    /* Programs start at cell 1 and at increasing cells after it. */
    for (i = 0; i < pgc->info.program_count; i++)
    {
        pgc->programs[i] = be16(at(f, t, programs_at + i * 2));
        if ((i == 0 && pgc->programs[i] != 1) || (i && pgc->programs[i] <= pgc->programs[i - 1]) ||
            pgc->programs[i] > pgc->info.cell_count)
            return HDDVD_EPARSE;
    }
    for (i = 0; i < pgc->info.cell_count; i++)
    {
        const unsigned char *r = at(f, t, cells_at + i * CELL_RECORD);
        const unsigned char *p = at(f, t, positions_at + i * CELL_POSITION);
        hddvd_vts_cell *cell = &pgc->cells[i];
        memset(cell, 0, sizeof(*cell));
        cell->category = be32(r);
        if (parse_time(r + 4, &cell->playback_time))
            return HDDVD_EPARSE;
        cell->first_vobu_sector = be32(r + 8);
        cell->first_ilvu_end_sector = be32(r + 12);
        cell->last_vobu_sector = be32(r + 16);
        cell->last_sector = be32(r + 20);
        cell->command_flags = r[24];
        cell->command = r[25];
        cell->evob_id = be16(p);
        cell->cell_id = p[3];
        if (cell->command > pgc->info.cell_command_count || (vts && check_cell(vts, cell)))
            return HDDVD_EPARSE;
    }
    return HDDVD_OK;
}

static int parse_pgcs(const ifo *f, hddvd_vts *vts)
{
    table t;
    size_t i, count;
    int rc = locate(f, VTS_PGC_TABLE, 4, &t);
    if (rc)
        return rc == HDDVD_ENOTFOUND ? HDDVD_EPARSE : rc;
    count = be16(at(f, &t, 0));
    if (!count || !fits(&t, TABLE_HEADER, count * PGC_SEARCH_POINTER))
        return HDDVD_EPARSE;
    /* The count is kept only once there is an array for close to free. */
    vts->pgcs = calloc(count, sizeof(*vts->pgcs));
    if (!vts->pgcs)
        return HDDVD_ENOMEM;
    vts->pgc_count = count;
    for (i = 0; i < vts->pgc_count; i++)
    {
        const unsigned char *srp = at(f, &t, TABLE_HEADER + i * PGC_SEARCH_POINTER);
        vts->pgcs[i].info.entry = (uint8_t)(srp[0] >> 7);
        vts->pgcs[i].info.title = srp[1];
        rc = parse_pgc(f, &t, be32(srp + 8), vts, &vts->pgcs[i]);
        if (rc)
            return rc;
    }
    return HDDVD_OK;
}

/* A menu PGC unit table: language unit pointers (code, extension byte,
   existence flags, offset), each unit a PGC table with 12-byte search
   pointers. Absent tables leave no units. */
static int parse_menus(const ifo *f, size_t field, menu_unit **out, size_t *out_count)
{
    table t;
    size_t i, j, count;
    menu_unit *units;
    int rc = locate(f, field, 4, &t);
    *out = NULL;
    *out_count = 0;
    if (rc == HDDVD_ENOTFOUND)
        return HDDVD_OK;
    if (rc)
        return rc;
    count = be16(at(f, &t, 0));
    if (!count || !fits(&t, TABLE_HEADER, count * MENU_UNIT_POINTER))
        return HDDVD_EPARSE;
    units = calloc(count, sizeof(*units));
    if (!units)
        return HDDVD_ENOMEM;
    *out = units;
    *out_count = count;
    for (i = 0; i < count; i++)
    {
        const unsigned char *p = at(f, &t, TABLE_HEADER + i * MENU_UNIT_POINTER);
        size_t offset = be32(p + 4);
        table unit;
        menu_unit *u = &units[i];
        if (p[0] && p[1])
        {
            u->info.language[0] = (char)p[0];
            u->info.language[1] = (char)p[1];
        }
        u->info.language_extension = p[2];
        u->info.existence = p[3];
        if (offset < TABLE_HEADER + count * MENU_UNIT_POINTER || !fits(&t, offset, TABLE_HEADER))
            return HDDVD_EPARSE;
        unit.base = t.base + offset;
        unit.length = (size_t)be32(at(f, &t, offset + 4)) + 1;
        if (!unit.length || !fits(&t, offset, unit.length))
            return HDDVD_EPARSE;
        u->info.pgc_count = be16(at(f, &unit, 0));
        if (!u->info.pgc_count ||
            !fits(&unit, TABLE_HEADER, (size_t)u->info.pgc_count * PGC_SEARCH_POINTER))
            return HDDVD_EPARSE;
        u->categories = calloc(u->info.pgc_count, sizeof(*u->categories));
        u->pgcs = calloc(u->info.pgc_count, sizeof(*u->pgcs));
        if (!u->categories || !u->pgcs)
            return HDDVD_ENOMEM;
        for (j = 0; j < u->info.pgc_count; j++)
        {
            const unsigned char *srp = at(f, &unit, TABLE_HEADER + j * PGC_SEARCH_POINTER);
            memcpy(u->categories[j].category, srp, 4);
            u->categories[j].entry = (uint8_t)(srp[0] >> 7);
            u->categories[j].menu_id = (uint8_t)(srp[1] >> 4);
            rc = parse_pgc(f, &unit, be32(srp + 8), NULL, &u->pgcs[j]);
            if (rc)
                return rc;
        }
    }
    return HDDVD_OK;
}

static int parse_chapters(const ifo *f, hddvd_vts *vts)
{
    table t;
    size_t i, j, total = 0;
    int rc = locate(f, VTS_PTT_TABLE, 4, &t);
    if (rc)
        return rc == HDDVD_ENOTFOUND ? HDDVD_EPARSE : rc;
    vts->title_count = be16(at(f, &t, 0));
    if (!vts->title_count || !fits(&t, TABLE_HEADER, vts->title_count * 4))
        return HDDVD_EPARSE;
    vts->chapter_start = malloc((vts->title_count + 1) * sizeof(*vts->chapter_start));
    if (!vts->chapter_start)
        return HDDVD_ENOMEM;
    /* Each title's chapters run from its offset to the next title's offset,
       or to the end of the table. */
    for (i = 0; i <= vts->title_count; i++)
    {
        size_t offset = i < vts->title_count ? be32(at(f, &t, TABLE_HEADER + i * 4)) : t.length;
        if (offset < TABLE_HEADER + vts->title_count * 4 || offset > t.length || offset % 4 ||
            (i && offset <= vts->chapter_start[i - 1]))
            return HDDVD_EPARSE;
        vts->chapter_start[i] = offset;
    }
    total = (t.length - vts->chapter_start[0]) / 4;
    vts->chapters = malloc(total * sizeof(*vts->chapters));
    if (!vts->chapters)
        return HDDVD_ENOMEM;
    for (i = 0; i < total; i++)
    {
        const unsigned char *r = at(f, &t, vts->chapter_start[0] + i * 4);
        hddvd_vts_chapter *c = &vts->chapters[i];
        c->pgc = be16(r);
        c->program = be16(r + 2);
        if (!c->pgc || c->pgc > vts->pgc_count || !c->program ||
            c->program > vts->pgcs[c->pgc - 1].info.program_count)
            return HDDVD_EPARSE;
    }
    /* Byte offsets become chapter indexes. */
    for (i = 0, j = vts->chapter_start[0]; i <= vts->title_count; i++)
        vts->chapter_start[i] = (vts->chapter_start[i] - j) / 4;
    return HDDVD_OK;
}

static int in_pgc_cells(const vts_pgc *pgc, uint32_t sector)
{
    size_t i;
    for (i = 0; i < pgc->info.cell_count; i++)
        if (pgc->cells[i].first_vobu_sector <= sector && sector <= pgc->cells[i].last_sector)
            return 1;
    return 0;
}

static int parse_time_maps(const ifo *f, hddvd_vts *vts)
{
    table t;
    size_t i, j, index;
    int rc = locate(f, VTS_TIME_MAP_TABLE, 4, &t);
    if (rc == HDDVD_ENOTFOUND)
        return HDDVD_OK;
    if (rc)
        return rc;
    if (be16(at(f, &t, 0)) != vts->pgc_count || !fits(&t, TABLE_HEADER, vts->pgc_count * 4))
        return HDDVD_EPARSE;
    vts->has_time_maps = 1;
    for (i = 0; i < vts->pgc_count; i++)
    {
        vts_pgc *pgc = &vts->pgcs[i];
        size_t offset = be32(at(f, &t, TABLE_HEADER + i * 4));
        if (!fits(&t, offset, 4))
            return HDDVD_EPARSE;
        pgc->map_unit = *at(f, &t, offset);
        pgc->map_count = be16(at(f, &t, offset + 2));
        if ((!pgc->map_unit && pgc->map_count) || !fits(&t, offset + 4, pgc->map_count * 4))
            return HDDVD_EPARSE;
        if (!pgc->map_count)
            continue;
        pgc->map = malloc(pgc->map_count * sizeof(*pgc->map));
        if (!pgc->map)
            return HDDVD_ENOMEM;
        for (j = 0; j < pgc->map_count; j++)
        {
            uint32_t value = be32(at(f, &t, offset + 4 + j * 4));
            pgc->map[j].sector = value & 0x7fffffffu;
            pgc->map[j].discontinuity = (int)(value >> 31);
            if (find_vobu(vts, pgc->map[j].sector, &index) ||
                !in_pgc_cells(pgc, pgc->map[j].sector))
                return HDDVD_EPARSE;
        }
    }
    return HDDVD_OK;
}

static int parse_attributes(const ifo *f, hddvd_vts *vts)
{
    size_t i;
    hddvd_decode_video_attributes(f->data + VTS_VIDEO_ATTRIBUTES, &vts->video);
    vts->stream_count[HDDVD_STREAM_AUDIO] = be16(f->data + VTS_AUDIO_COUNT);
    vts->stream_count[HDDVD_STREAM_SUBPICTURE] = be16(f->data + VTS_SUBPICTURE_COUNT);
    if (vts->stream_count[HDDVD_STREAM_AUDIO] > MAX_AUDIO ||
        vts->stream_count[HDDVD_STREAM_SUBPICTURE] > MAX_SUBPICTURES)
        return HDDVD_EPARSE;
    for (i = 0; i < vts->stream_count[HDDVD_STREAM_AUDIO]; i++)
        hddvd_decode_stream_attributes(f->data + VTS_AUDIO_RECORDS + i * AUDIO_RECORD, AUDIO_RECORD,
                                       4, &vts->audio[i]);
    for (i = 0; i < vts->stream_count[HDDVD_STREAM_SUBPICTURE]; i++)
        hddvd_decode_stream_attributes(f->data + VTS_SUBPICTURE_RECORDS + i * SUBPICTURE_RECORD,
                                       SUBPICTURE_RECORD, 2, &vts->subpictures[i]);
    return HDDVD_OK;
}

int hddvd_vts_open_with_config(hddvd_stream *stream, const hddvd_config_limits *config,
                               hddvd_vts **out)
{
    ifo f;
    hddvd_vts *vts;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!stream || !hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    rc = read_ifo(stream, config, HDDVD_IFO_STANDARD_VTS, &f);
    if (rc)
        return rc;
    vts = calloc(1, sizeof(*vts));
    if (!vts)
    {
        free(f.data);
        return HDDVD_ENOMEM;
    }
    vts->info.last_sector = be32(f.data + VTS_LAST_SECTOR);
    vts->info.ifo_sectors = (uint32_t)(f.size / IFO_SECTOR);
    vts->info.menu_evo_sector = be32(f.data + VTS_MENU_EVO);
    vts->info.title_evo_sector = be32(f.data + VTS_TITLE_EVO);
    rc = vts->info.title_evo_sector < vts->info.ifo_sectors ? HDDVD_EPARSE : HDDVD_OK;
    if (!rc)
        rc = parse_attributes(&f, vts);
    if (!rc)
        rc = parse_vobu_map(&f, vts);
    if (!rc)
        rc = parse_cell_addresses(&f, vts);
    if (!rc && (vts->vobus[vts->vobu_count - 1] >= vts->info.title_evo_sectors ||
                (uint64_t)vts->info.title_evo_sector + vts->info.title_evo_sectors >
                    (uint64_t)vts->info.last_sector + 1))
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = parse_pgcs(&f, vts);
    if (!rc)
        rc = parse_chapters(&f, vts);
    if (!rc)
        rc = parse_time_maps(&f, vts);
    if (!rc)
        rc = parse_menus(&f, VTS_MENU_TABLE, &vts->menu_units, &vts->menu_unit_count);
    free(f.data);
    if (rc)
    {
        hddvd_vts_close(vts);
        return rc;
    }
    *out = vts;
    return HDDVD_OK;
}

int hddvd_vts_open(hddvd_stream *stream, hddvd_vts **out)
{
    hddvd_config_limits config;
    hddvd_config_limits_default(&config);
    return hddvd_vts_open_with_config(stream, &config, out);
}

int hddvd_vts_get_info(const hddvd_vts *vts, hddvd_vts_info *out)
{
    if (!vts || !out)
        return HDDVD_EINVAL;
    *out = vts->info;
    return HDDVD_OK;
}

size_t hddvd_vts_title_count(const hddvd_vts *vts)
{
    return vts ? vts->title_count : 0;
}

size_t hddvd_vts_chapter_count(const hddvd_vts *vts, size_t title)
{
    if (!vts || title >= vts->title_count)
        return 0;
    return vts->chapter_start[title + 1] - vts->chapter_start[title];
}

int hddvd_vts_get_chapter(const hddvd_vts *vts, size_t title, size_t chapter,
                          hddvd_vts_chapter *out)
{
    if (!vts || !out || title >= vts->title_count || chapter >= hddvd_vts_chapter_count(vts, title))
        return HDDVD_EINVAL;
    *out = vts->chapters[vts->chapter_start[title] + chapter];
    return HDDVD_OK;
}

size_t hddvd_vts_pgc_count(const hddvd_vts *vts)
{
    return vts ? vts->pgc_count : 0;
}

int hddvd_vts_get_pgc(const hddvd_vts *vts, size_t pgc, hddvd_vts_pgc *out)
{
    if (!vts || !out || pgc >= vts->pgc_count)
        return HDDVD_EINVAL;
    *out = vts->pgcs[pgc].info;
    return HDDVD_OK;
}

int hddvd_vts_get_program(const hddvd_vts *vts, size_t pgc, size_t program, uint16_t *first_cell)
{
    if (!vts || !first_cell || pgc >= vts->pgc_count ||
        program >= vts->pgcs[pgc].info.program_count)
        return HDDVD_EINVAL;
    *first_cell = vts->pgcs[pgc].programs[program];
    return HDDVD_OK;
}

int hddvd_vts_get_cell(const hddvd_vts *vts, size_t pgc, size_t cell, hddvd_vts_cell *out)
{
    if (!vts || !out || pgc >= vts->pgc_count || cell >= vts->pgcs[pgc].info.cell_count)
        return HDDVD_EINVAL;
    *out = vts->pgcs[pgc].cells[cell];
    return HDDVD_OK;
}

int hddvd_vts_get_command(const hddvd_vts *vts, size_t pgc, hddvd_vts_command_kind kind,
                          size_t index, uint8_t command[8])
{
    if (!vts || !command || pgc >= vts->pgc_count)
        return HDDVD_EINVAL;
    return get_command(&vts->pgcs[pgc], kind, index, command);
}

int hddvd_vts_get_time_map(const hddvd_vts *vts, size_t pgc, uint8_t *unit_seconds, size_t *count)
{
    if (!vts || !unit_seconds || !count || pgc >= vts->pgc_count)
        return HDDVD_EINVAL;
    if (!vts->has_time_maps)
        return HDDVD_ENOTFOUND;
    *unit_seconds = vts->pgcs[pgc].map_unit;
    *count = vts->pgcs[pgc].map_count;
    return HDDVD_OK;
}

int hddvd_vts_get_time_map_entry(const hddvd_vts *vts, size_t pgc, size_t index,
                                 hddvd_vts_map_entry *out)
{
    if (!vts || !out || pgc >= vts->pgc_count || index >= vts->pgcs[pgc].map_count)
        return HDDVD_EINVAL;
    *out = vts->pgcs[pgc].map[index];
    return HDDVD_OK;
}

size_t hddvd_vts_evob_cell_count(const hddvd_vts *vts)
{
    return vts ? vts->evob_cell_count : 0;
}

int hddvd_vts_get_evob_cell(const hddvd_vts *vts, size_t index, hddvd_vts_evob_cell *out)
{
    if (!vts || !out || index >= vts->evob_cell_count)
        return HDDVD_EINVAL;
    *out = vts->evob_cells[index];
    return HDDVD_OK;
}

size_t hddvd_vts_vobu_count(const hddvd_vts *vts)
{
    return vts ? vts->vobu_count : 0;
}

int hddvd_vts_get_vobu(const hddvd_vts *vts, size_t index, uint32_t *sector)
{
    if (!vts || !sector || index >= vts->vobu_count)
        return HDDVD_EINVAL;
    *sector = vts->vobus[index];
    return HDDVD_OK;
}

void hddvd_vmg_close(hddvd_vmg *vmg)
{
    if (!vmg)
        return;
    menus_free(vmg->menu_units, vmg->menu_unit_count);
    pgc_free(&vmg->first_play);
    free(vmg->titles);
    free(vmg);
}

int hddvd_vmg_open_with_config(hddvd_stream *stream, const hddvd_config_limits *config,
                               hddvd_vmg **out)
{
    ifo f;
    table t, sets;
    hddvd_vmg *vmg;
    size_t i;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!stream || !hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    rc = read_ifo(stream, config, HDDVD_IFO_VMG, &f);
    if (rc)
        return rc;
    vmg = calloc(1, sizeof(*vmg));
    if (!vmg)
    {
        free(f.data);
        return HDDVD_ENOMEM;
    }
    vmg->last_sector = be32(f.data + VTS_LAST_SECTOR);
    rc = locate(&f, VMG_TITLE_SET_TABLE, 4, &sets);
    if (!rc)
        vmg->title_set_count = be16(at(&f, &sets, 0));
    else if (rc == HDDVD_ENOTFOUND)
        rc = HDDVD_OK;
    if (!rc)
        rc = locate(&f, VMG_TITLE_TABLE, 4, &t);
    if (rc == HDDVD_ENOTFOUND)
        rc = HDDVD_EPARSE;
    if (!rc)
    {
        vmg->title_count = be16(at(&f, &t, 0));
        if (!vmg->title_count || !fits(&t, TABLE_HEADER, vmg->title_count * VMG_TITLE))
            rc = HDDVD_EPARSE;
    }
    if (!rc && !(vmg->titles = malloc(vmg->title_count * sizeof(*vmg->titles))))
        rc = HDDVD_ENOMEM;
    for (i = 0; !rc && i < vmg->title_count; i++)
    {
        const unsigned char *r = at(&f, &t, TABLE_HEADER + i * VMG_TITLE);
        hddvd_vmg_title *title = &vmg->titles[i];
        title->playback_type = r[0];
        title->angle_count = r[1];
        title->chapter_count = be16(r + 4);
        title->title_set = be16(r + 8);
        title->title_set_title = be16(r + 10);
        title->title_set_sector = be32(r + 12);
        if (!title->angle_count || !title->chapter_count || !title->title_set ||
            !title->title_set_title || title->title_set_sector <= vmg->last_sector ||
            (vmg->title_set_count && title->title_set > vmg->title_set_count))
            rc = HDDVD_EPARSE;
    }
    if (!rc)
        rc = parse_menus(&f, VMG_MENU_TABLE, &vmg->menu_units, &vmg->menu_unit_count);
    if (!rc && be32(f.data + VMG_FIRST_PLAY))
    {
        /* The first-play PGC lies in the header, which ends at the byte the
           field at 0x80 names. */
        table header;
        header.base = 0;
        header.length = (size_t)be32(f.data + VMG_HEADER_END) + 1;
        if (header.length < VMG_FIRST_PLAY + 4 || header.length > f.size ||
            be32(f.data + VMG_FIRST_PLAY) < VMG_FIRST_PLAY + 4)
            rc = HDDVD_EPARSE;
        else
            rc = parse_pgc(&f, &header, be32(f.data + VMG_FIRST_PLAY), NULL, &vmg->first_play);
        vmg->has_first_play = !rc;
    }
    free(f.data);
    if (rc)
    {
        hddvd_vmg_close(vmg);
        return rc;
    }
    *out = vmg;
    return HDDVD_OK;
}

int hddvd_vmg_open(hddvd_stream *stream, hddvd_vmg **out)
{
    hddvd_config_limits config;
    hddvd_config_limits_default(&config);
    return hddvd_vmg_open_with_config(stream, &config, out);
}

uint32_t hddvd_vmg_last_sector(const hddvd_vmg *vmg)
{
    return vmg ? vmg->last_sector : 0;
}

size_t hddvd_vmg_title_set_count(const hddvd_vmg *vmg)
{
    return vmg ? vmg->title_set_count : 0;
}

size_t hddvd_vmg_title_count(const hddvd_vmg *vmg)
{
    return vmg ? vmg->title_count : 0;
}

int hddvd_vmg_get_title(const hddvd_vmg *vmg, size_t index, hddvd_vmg_title *out)
{
    if (!vmg || !out || index >= vmg->title_count)
        return HDDVD_EINVAL;
    *out = vmg->titles[index];
    return HDDVD_OK;
}

int hddvd_vts_get_video_attributes(const hddvd_vts *vts, hddvd_video_attributes *out)
{
    if (!vts || !out)
        return HDDVD_EINVAL;
    *out = vts->video;
    return HDDVD_OK;
}

size_t hddvd_vts_stream_count(const hddvd_vts *vts, hddvd_stream_kind kind)
{
    return vts && (kind == HDDVD_STREAM_AUDIO || kind == HDDVD_STREAM_SUBPICTURE)
               ? vts->stream_count[kind]
               : 0;
}

int hddvd_vts_get_stream_attributes(const hddvd_vts *vts, hddvd_stream_kind kind, size_t index,
                                    hddvd_stream_attributes *out)
{
    if (!vts || !out || index >= hddvd_vts_stream_count(vts, kind))
        return HDDVD_EINVAL;
    *out = kind == HDDVD_STREAM_AUDIO ? vts->audio[index] : vts->subpictures[index];
    return HDDVD_OK;
}

/* Menus */

static int get_command(const vts_pgc *pgc, hddvd_vts_command_kind kind, size_t index,
                       uint8_t command[8])
{
    const hddvd_vts_pgc *info = &pgc->info;
    size_t before, count;
    switch (kind)
    {
        case HDDVD_VTS_PRE_COMMAND:
            before = 0;
            count = info->pre_command_count;
            break;
        case HDDVD_VTS_POST_COMMAND:
            before = info->pre_command_count;
            count = info->post_command_count;
            break;
        case HDDVD_VTS_CELL_COMMAND:
            before = (size_t)info->pre_command_count + info->post_command_count;
            count = info->cell_command_count;
            break;
        default:
            return HDDVD_EINVAL;
    }
    if (index >= count)
        return HDDVD_EINVAL;
    memcpy(command, pgc->commands + (before + index) * COMMAND, COMMAND);
    return HDDVD_OK;
}

static const vts_pgc *menu_pgc(const menu_unit *units, size_t count, size_t unit, size_t pgc)
{
    if (!units || unit >= count || pgc >= units[unit].info.pgc_count)
        return NULL;
    return &units[unit].pgcs[pgc];
}

static int get_menu_unit(const menu_unit *units, size_t count, size_t unit, hddvd_menu_unit *out)
{
    if (!units || !out || unit >= count)
        return HDDVD_EINVAL;
    *out = units[unit].info;
    return HDDVD_OK;
}

static int get_menu_pgc(const menu_unit *units, size_t count, size_t unit, size_t pgc,
                        hddvd_menu_pgc *category, hddvd_vts_pgc *out)
{
    const vts_pgc *p = menu_pgc(units, count, unit, pgc);
    if (!p || (!category && !out))
        return HDDVD_EINVAL;
    if (category)
        *category = units[unit].categories[pgc];
    if (out)
        *out = p->info;
    return HDDVD_OK;
}

static int get_menu_cell(const menu_unit *units, size_t count, size_t unit, size_t pgc, size_t cell,
                         hddvd_vts_cell *out)
{
    const vts_pgc *p = menu_pgc(units, count, unit, pgc);
    if (!p || !out || cell >= p->info.cell_count)
        return HDDVD_EINVAL;
    *out = p->cells[cell];
    return HDDVD_OK;
}

size_t hddvd_vts_menu_unit_count(const hddvd_vts *vts)
{
    return vts ? vts->menu_unit_count : 0;
}

int hddvd_vts_get_menu_unit(const hddvd_vts *vts, size_t unit, hddvd_menu_unit *out)
{
    return vts ? get_menu_unit(vts->menu_units, vts->menu_unit_count, unit, out) : HDDVD_EINVAL;
}

int hddvd_vts_get_menu_pgc(const hddvd_vts *vts, size_t unit, size_t pgc, hddvd_menu_pgc *category,
                           hddvd_vts_pgc *out)
{
    return vts ? get_menu_pgc(vts->menu_units, vts->menu_unit_count, unit, pgc, category, out)
               : HDDVD_EINVAL;
}

int hddvd_vts_get_menu_command(const hddvd_vts *vts, size_t unit, size_t pgc,
                               hddvd_vts_command_kind kind, size_t index, uint8_t command[8])
{
    const vts_pgc *p = vts ? menu_pgc(vts->menu_units, vts->menu_unit_count, unit, pgc) : NULL;
    return p && command ? get_command(p, kind, index, command) : HDDVD_EINVAL;
}

int hddvd_vts_get_menu_cell(const hddvd_vts *vts, size_t unit, size_t pgc, size_t cell,
                            hddvd_vts_cell *out)
{
    return vts ? get_menu_cell(vts->menu_units, vts->menu_unit_count, unit, pgc, cell, out)
               : HDDVD_EINVAL;
}

size_t hddvd_vmg_menu_unit_count(const hddvd_vmg *vmg)
{
    return vmg ? vmg->menu_unit_count : 0;
}

int hddvd_vmg_get_menu_unit(const hddvd_vmg *vmg, size_t unit, hddvd_menu_unit *out)
{
    return vmg ? get_menu_unit(vmg->menu_units, vmg->menu_unit_count, unit, out) : HDDVD_EINVAL;
}

int hddvd_vmg_get_menu_pgc(const hddvd_vmg *vmg, size_t unit, size_t pgc, hddvd_menu_pgc *category,
                           hddvd_vts_pgc *out)
{
    return vmg ? get_menu_pgc(vmg->menu_units, vmg->menu_unit_count, unit, pgc, category, out)
               : HDDVD_EINVAL;
}

int hddvd_vmg_get_menu_command(const hddvd_vmg *vmg, size_t unit, size_t pgc,
                               hddvd_vts_command_kind kind, size_t index, uint8_t command[8])
{
    const vts_pgc *p = vmg ? menu_pgc(vmg->menu_units, vmg->menu_unit_count, unit, pgc) : NULL;
    return p && command ? get_command(p, kind, index, command) : HDDVD_EINVAL;
}

int hddvd_vmg_get_menu_cell(const hddvd_vmg *vmg, size_t unit, size_t pgc, size_t cell,
                            hddvd_vts_cell *out)
{
    return vmg ? get_menu_cell(vmg->menu_units, vmg->menu_unit_count, unit, pgc, cell, out)
               : HDDVD_EINVAL;
}

int hddvd_vmg_get_first_play(const hddvd_vmg *vmg, hddvd_vts_pgc *out)
{
    if (!vmg || !out)
        return HDDVD_EINVAL;
    if (!vmg->has_first_play)
        return HDDVD_ENOTFOUND;
    *out = vmg->first_play.info;
    return HDDVD_OK;
}

int hddvd_vmg_get_first_play_command(const hddvd_vmg *vmg, hddvd_vts_command_kind kind,
                                     size_t index, uint8_t command[8])
{
    if (!vmg || !command)
        return HDDVD_EINVAL;
    if (!vmg->has_first_play)
        return HDDVD_ENOTFOUND;
    return get_command(&vmg->first_play, kind, index, command);
}

int hddvd_vts_get_pgc_links_internal(const hddvd_vts *vts, int menu, size_t unit, size_t pgc,
                                     hddvd_pgc_links *out)
{
    const vts_pgc *p;
    if (!vts || !out)
        return HDDVD_EINVAL;
    p = menu ? menu_pgc(vts->menu_units, vts->menu_unit_count, unit, pgc)
             : (pgc < vts->pgc_count ? &vts->pgcs[pgc] : NULL);
    if (!p)
        return HDDVD_EINVAL;
    *out = p->links;
    return HDDVD_OK;
}

int hddvd_vmg_get_pgc_links_internal(const hddvd_vmg *vmg, int first_play, size_t unit, size_t pgc,
                                     hddvd_pgc_links *out)
{
    const vts_pgc *p;
    if (!vmg || !out)
        return HDDVD_EINVAL;
    p = first_play ? (vmg->has_first_play ? &vmg->first_play : NULL)
                   : menu_pgc(vmg->menu_units, vmg->menu_unit_count, unit, pgc);
    if (!p)
        return HDDVD_EINVAL;
    *out = p->links;
    return HDDVD_OK;
}

int hddvd_vmg_get_first_play_cell_internal(const hddvd_vmg *vmg, size_t cell, hddvd_vts_cell *out)
{
    if (!vmg || !out || !vmg->has_first_play || cell >= vmg->first_play.info.cell_count)
        return HDDVD_EINVAL;
    *out = vmg->first_play.cells[cell];
    return HDDVD_OK;
}
