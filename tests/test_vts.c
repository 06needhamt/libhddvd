#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original Standard Content fixtures in tests/fixtures/standard (see
   generate_objects.py): a title set whose PGC 1 has two cells and PGC 2 one,
   nine VOBUs of four sectors, and a VMG naming both titles. */
#define SECTOR 2048u
#define PTT (1 * SECTOR)
#define PGCIT (2 * SECTOR)
#define TMAPT (3 * SECTOR)
#define CADT (4 * SECTOR)
#define ADMAP (5 * SECTOR)
/* PGC 1 and its parts, from its search pointer and table offsets: three
   commands after a 10-byte header, two programs, two cells. */
#define PGC1 (PGCIT + 32u)
#define PGC1_COMMANDS (PGC1 + 0xb0u)
#define PGC1_PROGRAMS (PGC1 + 0xd2u)
#define PGC1_CELLS (PGC1 + 0xd6u)
#define PGC1_POSITIONS (PGC1_CELLS + 2u * 28u)
#define MAP1 (TMAPT + 16u)

static unsigned char *load(const char *name, size_t *size)
{
    char path[512];
    FILE *f;
    long end;
    unsigned char *bytes;
    snprintf(path, sizeof(path), "%s/standard/%s", HDDVD_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    assert(f && !fseek(f, 0, SEEK_END) && (end = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET));
    bytes = malloc((size_t)end);
    assert(bytes && fread(bytes, 1, (size_t)end, f) == (size_t)end);
    fclose(f);
    *size = (size_t)end;
    return bytes;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static int open_vts(const unsigned char *bytes, size_t size, hddvd_vts **out)
{
    hddvd_stream *stream = NULL;
    int rc;
    assert(hddvd_stream_open_memory(bytes, size, &stream) == HDDVD_OK);
    rc = hddvd_vts_open(stream, out);
    hddvd_stream_close(stream);
    return rc;
}

static int open_vmg(const unsigned char *bytes, size_t size, hddvd_vmg **out)
{
    hddvd_stream *stream = NULL;
    int rc;
    assert(hddvd_stream_open_memory(bytes, size, &stream) == HDDVD_OK);
    rc = hddvd_vmg_open(stream, out);
    hddvd_stream_close(stream);
    return rc;
}

/* Opens a mutated copy and returns the status. */
static int mutated(const unsigned char *bytes, size_t size, size_t offset, const void *value,
                   size_t length)
{
    unsigned char *copy = malloc(size);
    hddvd_vts *vts = NULL;
    int rc;
    assert(copy);
    memcpy(copy, bytes, size);
    memcpy(copy + offset, value, length);
    rc = open_vts(copy, size, &vts);
    assert(rc == HDDVD_OK || vts == NULL);
    hddvd_vts_close(vts);
    free(copy);
    return rc;
}

static int mutated32(const unsigned char *bytes, size_t size, size_t offset, uint32_t value)
{
    unsigned char b[4];
    put32(b, value);
    return mutated(bytes, size, offset, b, 4);
}

static int mutated16(const unsigned char *bytes, size_t size, size_t offset, unsigned value)
{
    unsigned char b[2];
    put16(b, value);
    return mutated(bytes, size, offset, b, 2);
}

static int mutated8(const unsigned char *bytes, size_t size, size_t offset, unsigned value)
{
    unsigned char b = (unsigned char)value;
    return mutated(bytes, size, offset, &b, 1);
}

static void title_set(void)
{
    size_t size;
    unsigned char *bytes = load("HVDVD_TS/HV001I01.IFO", &size);
    hddvd_vts *vts = NULL;
    hddvd_vts_info info;
    hddvd_vts_chapter chapter;
    hddvd_vts_pgc pgc;
    hddvd_vts_cell cell;
    hddvd_vts_map_entry entry;
    hddvd_vts_evob_cell extent;
    hddvd_time t;
    uint8_t command[8], unit;
    uint16_t first_cell;
    uint32_t sector;
    size_t count;

    assert(open_vts(bytes, size, &vts) == HDDVD_OK);
    assert(hddvd_vts_get_info(vts, &info) == HDDVD_OK);
    assert(info.ifo_sectors == 7 && info.title_evo_sector == 7 && info.title_evo_sectors == 36);
    assert(info.last_sector == 49 && info.menu_evo_sector == 0);

    assert(hddvd_vts_title_count(vts) == 2);
    assert(hddvd_vts_chapter_count(vts, 0) == 2 && hddvd_vts_chapter_count(vts, 1) == 1);
    assert(hddvd_vts_get_chapter(vts, 0, 1, &chapter) == HDDVD_OK);
    assert(chapter.pgc == 1 && chapter.program == 2);
    assert(hddvd_vts_get_chapter(vts, 1, 0, &chapter) == HDDVD_OK);
    assert(chapter.pgc == 2 && chapter.program == 1);

    assert(hddvd_vts_pgc_count(vts) == 2);
    assert(hddvd_vts_get_pgc(vts, 0, &pgc) == HDDVD_OK);
    assert(pgc.entry == 1 && pgc.title == 1 && pgc.program_count == 2 && pgc.cell_count == 2);
    assert(pgc.pre_command_count == 1 && pgc.post_command_count == 1 &&
           pgc.cell_command_count == 1);
    assert(pgc.playback_time.seconds == 2 && pgc.playback_time.frames == 15 &&
           pgc.playback_time.rate == 2 && pgc.playback_time.flag == 1);
    assert(hddvd_vts_time_to_ticks(&pgc.playback_time, &t) == HDDVD_OK && t.ticks == 75u * 3003u);
    assert(hddvd_vts_get_program(vts, 0, 1, &first_cell) == HDDVD_OK && first_cell == 2);

    assert(hddvd_vts_get_cell(vts, 0, 1, &cell) == HDDVD_OK);
    assert(cell.evob_id == 1 && cell.cell_id == 2 && cell.first_vobu_sector == 12 &&
           cell.last_vobu_sector == 16 && cell.last_sector == 19 && cell.command == 1 &&
           cell.command_flags == 0x10 && cell.category == 0x08000000u);
    assert(hddvd_vts_get_cell(vts, 1, 0, &cell) == HDDVD_OK);
    assert(cell.evob_id == 2 && cell.first_vobu_sector == 20 && cell.last_sector == 35);

    assert(hddvd_vts_get_command(vts, 0, HDDVD_VTS_PRE_COMMAND, 0, command) == HDDVD_OK);
    assert(command[0] == 0x71 && command[7] == 1);
    assert(hddvd_vts_get_command(vts, 0, HDDVD_VTS_POST_COMMAND, 0, command) == HDDVD_OK);
    assert(command[0] == 0x30 && command[1] == 0x02);
    assert(hddvd_vts_get_command(vts, 0, HDDVD_VTS_CELL_COMMAND, 0, command) == HDDVD_OK);
    assert(command[1] == 0x06);
    assert(hddvd_vts_get_command(vts, 1, HDDVD_VTS_CELL_COMMAND, 0, command) == HDDVD_EINVAL);
    {
        hddvd_video_attributes video;
        hddvd_stream_attributes stream;
        assert(hddvd_vts_get_video_attributes(vts, &video) == HDDVD_OK);
        assert(video.coding == 0 && video.raw[0] == 0x2b);
        assert(hddvd_vts_stream_count(vts, HDDVD_STREAM_AUDIO) == 1);
        assert(hddvd_vts_stream_count(vts, HDDVD_STREAM_SUBPICTURE) == 1);
        assert(hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_AUDIO, 0, &stream) == HDDVD_OK);
        assert(!strcmp(stream.language, "en") && stream.raw[0] == 0x1c && stream.raw[7] == 0x1f);
        assert(hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_SUBPICTURE, 0, &stream) ==
               HDDVD_OK);
        assert(!strcmp(stream.language, "en") && stream.raw[0] == 0x21 && stream.raw[6] == 0);
        assert(hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_AUDIO, 1, &stream) ==
               HDDVD_EINVAL);
        assert(hddvd_vts_get_stream_attributes(vts, (hddvd_stream_kind)2, 0, &stream) ==
               HDDVD_EINVAL);
        assert(hddvd_vts_stream_count(vts, (hddvd_stream_kind)2) == 0);
        assert(hddvd_vts_stream_count(NULL, HDDVD_STREAM_AUDIO) == 0);
        assert(hddvd_vts_get_video_attributes(NULL, &video) == HDDVD_EINVAL);
        assert(hddvd_vts_get_video_attributes(vts, NULL) == HDDVD_EINVAL);
        assert(hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_AUDIO, 0, NULL) == HDDVD_EINVAL);
    }
    assert(hddvd_vts_get_command(vts, 0, (hddvd_vts_command_kind)9, 0, command) == HDDVD_EINVAL);

    /* PGC 1: 1 s is in cell 1 (VOBU at sector 4), 2 s in cell 2 (sector 12). */
    assert(hddvd_vts_get_time_map(vts, 0, &unit, &count) == HDDVD_OK && unit == 1 && count == 2);
    assert(hddvd_vts_get_time_map_entry(vts, 0, 0, &entry) == HDDVD_OK);
    assert(entry.sector == 4 && entry.discontinuity);
    assert(hddvd_vts_get_time_map_entry(vts, 0, 1, &entry) == HDDVD_OK);
    assert(entry.sector == 12 && entry.discontinuity);
    assert(hddvd_vts_get_time_map_entry(vts, 1, 0, &entry) == HDDVD_OK);
    assert(entry.sector == 24 && !entry.discontinuity);
    assert(hddvd_vts_get_time_map_entry(vts, 1, 2, &entry) == HDDVD_EINVAL);

    assert(hddvd_vts_evob_cell_count(vts) == 3);
    assert(hddvd_vts_get_evob_cell(vts, 2, &extent) == HDDVD_OK);
    assert(extent.evob_id == 2 && extent.cell_id == 1 && extent.first_sector == 20 &&
           extent.last_sector == 35);
    assert(hddvd_vts_vobu_count(vts) == 9);
    assert(hddvd_vts_get_vobu(vts, 8, &sector) == HDDVD_OK && sector == 32);

    /* Out of range and invalid arguments. */
    assert(hddvd_vts_get_chapter(vts, 2, 0, &chapter) == HDDVD_EINVAL);
    assert(hddvd_vts_get_chapter(vts, 0, 2, &chapter) == HDDVD_EINVAL);
    assert(hddvd_vts_get_pgc(vts, 2, &pgc) == HDDVD_EINVAL);
    assert(hddvd_vts_get_program(vts, 0, 2, &first_cell) == HDDVD_EINVAL);
    assert(hddvd_vts_get_cell(vts, 0, 2, &cell) == HDDVD_EINVAL);
    assert(hddvd_vts_get_time_map(vts, 2, &unit, &count) == HDDVD_EINVAL);
    assert(hddvd_vts_get_evob_cell(vts, 3, &extent) == HDDVD_EINVAL);
    assert(hddvd_vts_get_vobu(vts, 9, &sector) == HDDVD_EINVAL);
    assert(hddvd_vts_get_info(vts, NULL) == HDDVD_EINVAL);
    assert(hddvd_vts_chapter_count(vts, 5) == 0);
    hddvd_vts_close(vts);
    assert(hddvd_vts_title_count(NULL) == 0 && hddvd_vts_pgc_count(NULL) == 0 &&
           hddvd_vts_vobu_count(NULL) == 0 && hddvd_vts_evob_cell_count(NULL) == 0);
    assert(hddvd_vts_open(NULL, &vts) == HDDVD_EINVAL);
    hddvd_vts_close(NULL);
    free(bytes);
}

static void title_set_errors(void)
{
    size_t size;
    unsigned char *bytes = load("HVDVD_TS/HV001I01.IFO", &size);
    unsigned char *vmg = NULL;
    size_t vmg_size;
    hddvd_vts *vts = NULL;
    hddvd_stream *stream = NULL;
    hddvd_config_limits config;
    uint8_t unit;
    size_t count;

    assert(mutated(bytes, size, 0, "STANDARD-VTX", 12) == HDDVD_ENOTSUP);
    vmg = load("HVDVD_TS/HV000I01.IFO", &vmg_size);
    assert(open_vts(vmg, vmg_size, &vts) == HDDVD_ENOTSUP);
    /* A table beyond the file or past its stated end. */
    assert(mutated32(bytes, size, 0xc8, 99) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, PTT + 4, SECTOR) == HDDVD_EPARSE);
    /* More PGCs than the table holds search pointers for (found by
       fuzz-vts: cleanup once walked PGCs that were never allocated). */
    assert(mutated16(bytes, size, PGCIT, 0x7fff) == HDDVD_EPARSE);
    /* Chapters naming a missing PGC or program. */
    assert(mutated16(bytes, size, PTT + 16, 3) == HDDVD_EPARSE);
    assert(mutated16(bytes, size, PTT + 18, 3) == HDDVD_EPARSE);
    /* Programs that do not start at cell 1. */
    assert(mutated16(bytes, size, PGC1_PROGRAMS, 2) == HDDVD_EPARSE);
    /* A cell starting off the VOBU map, or ending inside a VOBU. */
    assert(mutated32(bytes, size, PGC1_CELLS + 8, 1) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, PGC1_CELLS + 20, 10) == HDDVD_EPARSE);
    /* A cell outside its address-table entry. */
    assert(mutated8(bytes, size, PGC1_POSITIONS + 3, 2) == HDDVD_EPARSE);
    /* More audio or subpicture streams than the header holds records for. */
    assert(mutated16(bytes, size, 0x218, 9) == HDDVD_EPARSE);
    assert(mutated16(bytes, size, 0x25a, 33) == HDDVD_EPARSE);
    /* A command table whose end address disagrees with its counts. */
    assert(mutated32(bytes, size, PGC1_COMMANDS + 6, 10 + 3 * 8) == HDDVD_EPARSE);
    assert(mutated16(bytes, size, PGC1_COMMANDS + 2, 2) == HDDVD_EPARSE);
    /* A cell command beyond the PGC's count. */
    assert(mutated8(bytes, size, PGC1_CELLS + 28 + 25, 2) == HDDVD_EPARSE);
    /* Invalid BCD. */
    assert(mutated8(bytes, size, PGC1 + 5, 0x0a) == HDDVD_EPARSE);
    assert(mutated8(bytes, size, PGC1_CELLS + 7, 0x8a) == HDDVD_EPARSE);
    /* Time maps: one per PGC, entries inside the PGC's cells, and an empty
       unit only with no entries. */
    assert(mutated16(bytes, size, TMAPT, 3) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, MAP1 + 4, 20) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, MAP1 + 4, 5) == HDDVD_EPARSE);
    assert(mutated8(bytes, size, MAP1, 0) == HDDVD_EPARSE);
    /* VOBU map not increasing; overlapping cell extents. */
    assert(mutated32(bytes, size, ADMAP + 8, 0) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, CADT + 8 + 12 + 4, 5) == HDDVD_EPARSE);
    /* The title EVO inside the IFO. */
    assert(mutated32(bytes, size, 0xc4, 2) == HDDVD_EPARSE);
    /* Not whole sectors. */
    assert(open_vts(bytes, size - 1, &vts) == HDDVD_EPARSE);

    /* Without a time map table, the rest still opens. */
    {
        unsigned char *copy = malloc(size);
        assert(copy);
        memcpy(copy, bytes, size);
        put32(copy + 0xd4, 0);
        assert(open_vts(copy, size, &vts) == HDDVD_OK);
        assert(hddvd_vts_get_time_map(vts, 0, &unit, &count) == HDDVD_ENOTFOUND);
        hddvd_vts_close(vts);
        free(copy);
    }

    hddvd_config_limits_default(&config);
    config.max_resource_bytes = size - 1;
    assert(hddvd_stream_open_memory(bytes, size, &stream) == HDDVD_OK);
    assert(hddvd_vts_open_with_config(stream, &config, &vts) == HDDVD_ELIMIT && !vts);
    config.max_resource_bytes = 0;
    assert(hddvd_vts_open_with_config(stream, &config, &vts) == HDDVD_EINVAL);
    assert(hddvd_vts_open_with_config(stream, NULL, &vts) == HDDVD_EINVAL);
    hddvd_stream_close(stream);
    free(vmg);
    free(bytes);
}

static void times(void)
{
    hddvd_vts_time time;
    hddvd_time t;
    memset(&time, 0, sizeof(time));
    time.hours = 1;
    time.minutes = 2;
    time.seconds = 3;
    time.frames = 4;
    time.rate = 2;
    assert(hddvd_vts_time_to_ticks(&time, &t) == HDDVD_OK);
    assert(t.ticks == ((3600u + 120u + 3u) * 30u + 4u) * 3003u);
    time.rate = 1;
    assert(hddvd_vts_time_to_ticks(&time, &t) == HDDVD_ENOTSUP);
    time.rate = 2;
    time.frames = 30;
    assert(hddvd_vts_time_to_ticks(&time, &t) == HDDVD_EINVAL);
    assert(hddvd_vts_time_to_ticks(NULL, &t) == HDDVD_EINVAL);
    assert(hddvd_vts_time_to_ticks(&time, NULL) == HDDVD_EINVAL);
}

static int vmg_mutated32(const unsigned char *bytes, size_t size, size_t offset, uint32_t value,
                         size_t width)
{
    unsigned char *copy = malloc(size);
    hddvd_vmg *vmg = NULL;
    int rc;
    assert(copy);
    memcpy(copy, bytes, size);
    if (width == 4)
        put32(copy + offset, value);
    else if (width == 2)
        put16(copy + offset, value);
    else
        copy[offset] = (unsigned char)value;
    rc = open_vmg(copy, size, &vmg);
    assert(rc == HDDVD_OK || vmg == NULL);
    if (!rc)
        rc = (int)hddvd_vmg_title_set_count(vmg) == 0 ? 1 : 0;
    hddvd_vmg_close(vmg);
    free(copy);
    return rc;
}

static void video_manager(void)
{
    size_t size, vts_size;
    unsigned char *bytes = load("HVDVD_TS/HV000I01.IFO", &size);
    unsigned char *vts_bytes = load("HVDVD_TS/HV001I01.IFO", &vts_size);
    hddvd_vmg *vmg = NULL;
    hddvd_vmg_title title;
    assert(open_vmg(bytes, size, &vmg) == HDDVD_OK);
    assert(hddvd_vmg_last_sector(vmg) == 7 && hddvd_vmg_title_set_count(vmg) == 1);
    assert(hddvd_vmg_title_count(vmg) == 2);
    assert(hddvd_vmg_get_title(vmg, 0, &title) == HDDVD_OK);
    assert(title.title_set == 1 && title.title_set_title == 1 && title.chapter_count == 2 &&
           title.angle_count == 1 && title.title_set_sector == 8 && title.playback_type == 0x3c);
    assert(hddvd_vmg_get_title(vmg, 2, &title) == HDDVD_EINVAL);
    hddvd_vmg_close(vmg);
    assert(hddvd_vmg_title_count(NULL) == 0 && hddvd_vmg_last_sector(NULL) == 0);
    assert(open_vmg(vts_bytes, vts_size, &vmg) == HDDVD_ENOTSUP);

    /* A title naming a title set past the count, no angles or chapters, or
       starting inside the VMG; a missing title table. */
    assert(vmg_mutated32(bytes, size, SECTOR + 8 + 8, 2, 2) == HDDVD_EPARSE);
    assert(vmg_mutated32(bytes, size, SECTOR + 8 + 1, 0, 1) == HDDVD_EPARSE);
    assert(vmg_mutated32(bytes, size, SECTOR + 8 + 4, 0, 2) == HDDVD_EPARSE);
    assert(vmg_mutated32(bytes, size, SECTOR + 8 + 12, 7, 4) == HDDVD_EPARSE);
    assert(vmg_mutated32(bytes, size, 0xc4, 0, 4) == HDDVD_EPARSE);
    /* Without a title-set table, titles are not checked against a count. */
    assert(vmg_mutated32(bytes, size, 0xd0, 0, 4) == 1);
    free(vts_bytes);
    free(bytes);
}

/* The fixtures' menus: a title set root menu in sector 6, the VMG's title
   menu and a second PGC in sector 3, and the first-play PGC at 0x400. */
#define VTS_MENUS (6 * SECTOR)
#define VTS_MENU_UNIT (VTS_MENUS + 16u)
#define VMG_MENUS (3 * SECTOR)
#define VMG_MENU_UNIT (VMG_MENUS + 16u)

/* Runs a PGC's pre commands, as a host would on entering it. */
static void run_pre(hddvd_vm *vm, const hddvd_vmg *vmg, size_t unit, size_t p, hddvd_vm_result *r)
{
    uint8_t commands[4][8];
    hddvd_vts_pgc pgc;
    size_t i;
    if (unit == (size_t)-1)
        assert(hddvd_vmg_get_first_play(vmg, &pgc) == HDDVD_OK);
    else
        assert(hddvd_vmg_get_menu_pgc(vmg, unit, p, NULL, &pgc) == HDDVD_OK);
    assert(pgc.pre_command_count <= 4);
    for (i = 0; i < pgc.pre_command_count; i++)
        assert((unit == (size_t)-1
                    ? hddvd_vmg_get_first_play_command(vmg, HDDVD_VTS_PRE_COMMAND, i, commands[i])
                    : hddvd_vmg_get_menu_command(vmg, unit, p, HDDVD_VTS_PRE_COMMAND, i,
                                                 commands[i])) == HDDVD_OK);
    assert(hddvd_vm_run(vm, &commands[0][0], pgc.pre_command_count, 0, r) == HDDVD_OK);
}

static void menus(void)
{
    size_t size, vmg_size;
    unsigned char *bytes = load("HVDVD_TS/HV001I01.IFO", &size);
    unsigned char *vmg_bytes = load("HVDVD_TS/HV000I01.IFO", &vmg_size);
    hddvd_vts *vts = NULL;
    hddvd_vmg *vmg = NULL;
    hddvd_menu_unit unit;
    hddvd_menu_pgc category;
    hddvd_vts_pgc pgc;
    hddvd_vts_cell cell;
    hddvd_vm *vm = NULL;
    hddvd_vm_result r;
    uint8_t command[8];

    assert(open_vts(bytes, size, &vts) == HDDVD_OK);
    assert(hddvd_vts_menu_unit_count(vts) == 1 && hddvd_vts_menu_unit_count(NULL) == 0);
    assert(hddvd_vts_get_menu_unit(vts, 0, &unit) == HDDVD_OK);
    assert(!strcmp(unit.language, "en") && unit.existence == 0x80 && unit.pgc_count == 1);
    assert(hddvd_vts_get_menu_unit(vts, 1, &unit) == HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_unit(vts, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_pgc(vts, 0, 0, &category, &pgc) == HDDVD_OK);
    assert(category.entry == 1 && category.menu_id == 3 && category.category[1] == 0x30);
    assert(pgc.program_count == 0 && pgc.cell_count == 0 && pgc.pre_command_count == 1 &&
           pgc.post_command_count == 1 && pgc.cell_command_count == 0);
    assert(hddvd_vts_get_menu_pgc(vts, 0, 0, &category, NULL) == HDDVD_OK);
    assert(hddvd_vts_get_menu_pgc(vts, 0, 0, NULL, NULL) == HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_pgc(vts, 0, 1, &category, &pgc) == HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_command(vts, 0, 0, HDDVD_VTS_PRE_COMMAND, 0, command) == HDDVD_OK);
    assert(command[0] == 0x71 && command[3] == 2 && command[5] == 7);
    assert(hddvd_vts_get_menu_command(vts, 0, 0, HDDVD_VTS_POST_COMMAND, 0, command) == HDDVD_OK);
    assert(command[0] == 0x30 && command[1] == 0x03);
    assert(hddvd_vts_get_menu_command(vts, 0, 0, HDDVD_VTS_CELL_COMMAND, 0, command) ==
           HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_command(vts, 0, 0, HDDVD_VTS_PRE_COMMAND, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_cell(vts, 0, 0, 0, &cell) == HDDVD_EINVAL);
    assert(hddvd_vts_get_menu_unit(NULL, 0, &unit) == HDDVD_EINVAL);
    hddvd_vts_close(vts);

    /* A title set without menus; a pointer past the file, no units, a unit
       inside the pointers or past the table, a PGC past its unit. */
    assert(mutated32(bytes, size, 0xd0, 0) == HDDVD_OK);
    assert(mutated32(bytes, size, 0xd0, 99) == HDDVD_EPARSE);
    assert(mutated16(bytes, size, VTS_MENUS, 0) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, VTS_MENUS + 12, 8) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, VTS_MENUS + 12, 4000) == HDDVD_EPARSE);
    assert(mutated16(bytes, size, VTS_MENU_UNIT, 0) == HDDVD_EPARSE);
    assert(mutated32(bytes, size, VTS_MENU_UNIT + 16, 4000) == HDDVD_EPARSE);

    assert(open_vmg(vmg_bytes, vmg_size, &vmg) == HDDVD_OK);
    assert(hddvd_vmg_menu_unit_count(vmg) == 1);
    assert(hddvd_vmg_get_menu_unit(vmg, 0, &unit) == HDDVD_OK && unit.pgc_count == 2);
    assert(hddvd_vmg_get_menu_pgc(vmg, 0, 0, &category, &pgc) == HDDVD_OK);
    assert(category.menu_id == 2 && pgc.pre_command_count == 2);
    assert(hddvd_vmg_get_menu_pgc(vmg, 0, 1, &category, &pgc) == HDDVD_OK);
    assert(category.entry == 0 && category.menu_id == 0 && pgc.pre_command_count == 1);
    assert(hddvd_vmg_get_menu_command(vmg, 0, 1, HDDVD_VTS_PRE_COMMAND, 0, command) == HDDVD_OK);
    assert(command[1] == 0x08);
    assert(hddvd_vmg_get_menu_cell(vmg, 0, 0, 0, &cell) == HDDVD_EINVAL);
    assert(hddvd_vmg_get_first_play(vmg, &pgc) == HDDVD_OK && pgc.pre_command_count == 2 &&
           pgc.cell_count == 0);
    assert(hddvd_vmg_get_first_play(vmg, NULL) == HDDVD_EINVAL);
    assert(hddvd_vmg_get_first_play_command(vmg, HDDVD_VTS_POST_COMMAND, 0, command) ==
           HDDVD_EINVAL);

    /* First play sets GPRM0 and jumps to the title menu (id 2, PGC 1),
       which links to PGC 2, which calls the title set's root menu. */
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    run_pre(vm, vmg, (size_t)-1, 0, &r);
    assert(r.action == HDDVD_VM_JUMP_SYSTEM && r.domain == HDDVD_VM_VMG_MENU && r.target == 2);
    run_pre(vm, vmg, 0, 0, &r);
    assert(r.action == HDDVD_VM_LINK && r.link == HDDVD_VM_LINK_PGC && r.target == 2);
    run_pre(vm, vmg, 0, r.target - 1u, &r);
    assert(r.action == HDDVD_VM_CALL_SYSTEM && r.domain == HDDVD_VM_VTS_MENU && r.target == 3 &&
           r.resume_cell == 1);
    /* With GPRM0 cleared, the title menu plays title 1. */
    assert(hddvd_vm_set_register(vm, 0, 0) == HDDVD_OK);
    run_pre(vm, vmg, 0, 0, &r);
    assert(r.action == HDDVD_VM_JUMP_TITLE && r.target == 1 && r.line == 2);
    hddvd_vm_destroy(vm);
    hddvd_vmg_close(vmg);

    /* No first play; one starting inside the header fields, or a header
       end past the file. */
    {
        unsigned char *copy = malloc(vmg_size);
        assert(copy);
        memcpy(copy, vmg_bytes, vmg_size);
        put32(copy + 0x84, 0);
        assert(open_vmg(copy, vmg_size, &vmg) == HDDVD_OK);
        assert(hddvd_vmg_get_first_play(vmg, &pgc) == HDDVD_ENOTFOUND);
        assert(hddvd_vmg_get_first_play_command(vmg, HDDVD_VTS_PRE_COMMAND, 0, command) ==
               HDDVD_ENOTFOUND);
        hddvd_vmg_close(vmg);
        put32(copy + 0x84, 0x40);
        assert(open_vmg(copy, vmg_size, &vmg) == HDDVD_EPARSE && !vmg);
        put32(copy + 0x84, 0x400);
        put32(copy + 0x80, (uint32_t)vmg_size);
        assert(open_vmg(copy, vmg_size, &vmg) == HDDVD_EPARSE);
        /* A header too short for the PGC. */
        put32(copy + 0x80, 0x410);
        assert(open_vmg(copy, vmg_size, &vmg) == HDDVD_EPARSE);
        put32(copy + 0x80, 0x7ff);
        put32(copy + 0xc8, 0);
        assert(open_vmg(copy, vmg_size, &vmg) == HDDVD_OK);
        assert(hddvd_vmg_menu_unit_count(vmg) == 0);
        assert(hddvd_vmg_get_menu_unit(vmg, 0, &unit) == HDDVD_EINVAL);
        hddvd_vmg_close(vmg);
        put32(copy + 0xc8, 3);
        put32(copy + VMG_MENU_UNIT + 8 + 12 + 8, 2);
        assert(open_vmg(copy, vmg_size, &vmg) == HDDVD_EPARSE);
        free(copy);
    }
    free(vmg_bytes);
    free(bytes);
}

int main(void)
{
    title_set();
    title_set_errors();
    times();
    video_manager();
    menus();
    return 0;
}
