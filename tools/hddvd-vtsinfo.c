#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prints a Standard Content IFO. For a title set it also checks what the
   library does not enforce: PGC times against their cells and time-map
   discontinuity flags against cell boundaries. Given the title EVO, it
   checks the IFO against the EVO's navigation packs (docs/formats/ifo-map.md):
   the VOBU map, cell durations and elapsed times, and every time-map entry. */

#define SECTOR 2048u

typedef struct nav
{
    uint32_t sector;
    uint32_t start;
    uint32_t end;
    uint64_t elapsed;
} nav;

static int failures;

static void check(int ok, const char *what, size_t a, size_t b)
{
    if (ok)
        return;
    failures++;
    printf("MISMATCH %s (%zu, %zu)\n", what, a, b);
}

static uint32_t be32(const unsigned char *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static uint64_t ticks(const hddvd_vts_time *time)
{
    hddvd_time t = {0};
    return hddvd_vts_time_to_ticks(time, &t) ? 0 : t.ticks;
}

/* The navigation pack in a sector: a pack header, then the private-stream-2
   packet whose first byte is 0 (the PCI). Returns 0 when there is none. */
static int read_nav(const unsigned char *s, nav *out)
{
    size_t p;
    if (memcmp(s, "\0\0\1\xba", 4))
        return 0;
    p = 14u + (s[13] & 7u);
    while (p + 7 <= SECTOR && !memcmp(s + p, "\0\0\1", 3))
    {
        size_t length = (size_t)s[p + 4] << 8 | s[p + 5];
        if (s[p + 3] == 0xbf && s[p + 6] == 0 && p + 6 + 0x1d <= SECTOR)
        {
            hddvd_vts_time elapsed;
            const unsigned char *pci = s + p + 6;
            out->sector = be32(pci + 1);
            out->start = be32(pci + 0x0d);
            out->end = be32(pci + 0x11);
            memset(&elapsed, 0, sizeof(elapsed));
            elapsed.hours = (uint8_t)((pci[0x19] >> 4) * 10 + (pci[0x19] & 15));
            elapsed.minutes = (uint8_t)((pci[0x1a] >> 4) * 10 + (pci[0x1a] & 15));
            elapsed.seconds = (uint8_t)(((pci[0x1b] & 0x7f) >> 4) * 10 + (pci[0x1b] & 15));
            elapsed.frames = (uint8_t)(((pci[0x1c] & 0x3f) >> 4) * 10 + (pci[0x1c] & 15));
            elapsed.rate = (uint8_t)(pci[0x1c] >> 6);
            out->elapsed = ticks(&elapsed);
            return 1;
        }
        if (s[p + 3] == 0xba)
            break;
        p += 6 + length;
    }
    return 0;
}

static int load(const char *path, unsigned char **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long end;
    if (!f)
        return HDDVD_EIO;
    if (fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
    {
        fclose(f);
        return HDDVD_EIO;
    }
    *size = (size_t)end;
    *data = malloc(*size ? *size : 1);
    if (!*data || fread(*data, 1, *size, f) != *size)
    {
        fclose(f);
        free(*data);
        return HDDVD_EIO;
    }
    fclose(f);
    return HDDVD_OK;
}

static const nav *find_nav(const nav *navs, size_t count, uint32_t sector)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (navs[i].sector == sector)
            return &navs[i];
    return NULL;
}

/* Checks the title EVO against the IFO. */
static void check_evo(const hddvd_vts *vts, const char *path)
{
    hddvd_vts_info info;
    unsigned char *data = NULL;
    size_t size = 0, count = 0, i, p, c, k;
    nav *navs;
    if (load(path, &data, &size))
    {
        printf("MISMATCH cannot read %s\n", path);
        failures++;
        return;
    }
    hddvd_vts_get_info(vts, &info);
    check(size == (size_t)info.title_evo_sectors * SECTOR, "EVO size against the cell table",
          size / SECTOR, info.title_evo_sectors);
    navs = calloc(size / SECTOR + 1, sizeof(*navs));
    for (i = 0; navs && i < size / SECTOR; i++)
        if (read_nav(data + i * SECTOR, &navs[count]))
        {
            check(navs[count].sector == i, "navigation pack's own sector", i, navs[count].sector);
            count++;
        }
    check(count == hddvd_vts_vobu_count(vts), "navigation packs against the VOBU map", count,
          hddvd_vts_vobu_count(vts));
    for (i = 0; navs && i < hddvd_vts_vobu_count(vts) && i < count; i++)
    {
        uint32_t sector;
        hddvd_vts_get_vobu(vts, i, &sector);
        check(navs[i].sector == sector, "VOBU map entry is a navigation pack", i, sector);
    }
    for (p = 0; navs && p < hddvd_vts_pgc_count(vts); p++)
    {
        hddvd_vts_pgc pgc;
        uint64_t *cell_start, offset = 0;
        uint8_t unit;
        size_t entries;
        hddvd_vts_get_pgc(vts, p, &pgc);
        cell_start = calloc((size_t)pgc.cell_count + 1, sizeof(*cell_start));
        for (c = 0; cell_start && c < pgc.cell_count; c++)
        {
            hddvd_vts_cell cell;
            const nav *first, *last;
            uint64_t duration;
            hddvd_vts_get_cell(vts, p, c, &cell);
            first = find_nav(navs, count, cell.first_vobu_sector);
            last = find_nav(navs, count, cell.last_vobu_sector);
            if (!first || !last)
            {
                check(0, "cell VOBUs have navigation packs", p + 1, c + 1);
                continue;
            }
            duration = last->end - first->start;
            /* Cell times are whole frames; 3:2 pulldown leaves half frames. */
            check(duration + 3003 > ticks(&cell.playback_time) &&
                      ticks(&cell.playback_time) + 3003 > duration,
                  "cell time against its navigation packs", p + 1, c + 1);
            for (k = 0; k < count; k++)
                if (navs[k].sector >= cell.first_vobu_sector && navs[k].sector <= cell.last_sector)
                    check(navs[k].elapsed + 3003 > navs[k].start - first->start &&
                              navs[k].start - first->start + 3003 > navs[k].elapsed,
                          "elapsed time in cell", c + 1, navs[k].sector);
            cell_start[c + 1] = cell_start[c] + duration;
        }
        /* Entry k names the VOBU playing at (k + 1) units into the PGC. */
        if (cell_start && !hddvd_vts_get_time_map(vts, p, &unit, &entries))
            for (k = 0; k < entries; k++)
            {
                hddvd_vts_map_entry entry;
                uint32_t expected = UINT32_MAX;
                hddvd_vts_get_time_map_entry(vts, p, k, &entry);
                offset = (uint64_t)(k + 1) * unit * 90000u;
                for (c = 0; c < pgc.cell_count; c++)
                    if (offset >= cell_start[c] && offset < cell_start[c + 1])
                    {
                        hddvd_vts_cell cell;
                        const nav *first;
                        size_t n;
                        hddvd_vts_get_cell(vts, p, c, &cell);
                        first = find_nav(navs, count, cell.first_vobu_sector);
                        for (n = 0; first && n < count; n++)
                            if (navs[n].sector >= cell.first_vobu_sector &&
                                navs[n].sector <= cell.last_sector &&
                                offset - cell_start[c] >= navs[n].start - first->start &&
                                offset - cell_start[c] < navs[n].end - first->start)
                                expected = navs[n].sector;
                    }
                check(entry.sector == expected, "time-map entry against VOBU times", p + 1, k + 1);
            }
        free(cell_start);
    }
    printf("evo %s sectors=%zu navigation-packs=%zu\n", path, size / SECTOR, count);
    free(navs);
    free(data);
}

static void print_vts(const hddvd_vts *vts)
{
    hddvd_vts_info info;
    size_t t, c, p, k;
    hddvd_vts_get_info(vts, &info);
    printf("title-set last-sector=%lu ifo-sectors=%lu menu-evo=%lu title-evo=%lu+%lu "
           "vobus=%zu evob-cells=%zu\n",
           (unsigned long)info.last_sector, (unsigned long)info.ifo_sectors,
           (unsigned long)info.menu_evo_sector, (unsigned long)info.title_evo_sector,
           (unsigned long)info.title_evo_sectors, hddvd_vts_vobu_count(vts),
           hddvd_vts_evob_cell_count(vts));
    for (t = 0; t < hddvd_vts_title_count(vts); t++)
    {
        printf("title %zu chapters:", t + 1);
        for (c = 0; c < hddvd_vts_chapter_count(vts, t); c++)
        {
            hddvd_vts_chapter chapter;
            hddvd_vts_get_chapter(vts, t, c, &chapter);
            printf(" %u/%u", (unsigned)chapter.pgc, (unsigned)chapter.program);
        }
        printf("\n");
    }
    for (p = 0; p < hddvd_vts_pgc_count(vts); p++)
    {
        hddvd_vts_pgc pgc;
        uint64_t sum = 0;
        uint8_t unit;
        size_t entries;
        hddvd_vts_get_pgc(vts, p, &pgc);
        printf("pgc %zu entry=%u title=%u programs=%u cells=%u time=%.3fs commands=%u/%u/%u\n",
               p + 1, (unsigned)pgc.entry, (unsigned)pgc.title, (unsigned)pgc.program_count,
               (unsigned)pgc.cell_count, (double)ticks(&pgc.playback_time) / 90000.0,
               (unsigned)pgc.pre_command_count, (unsigned)pgc.post_command_count,
               (unsigned)pgc.cell_command_count);
        for (c = 0; c < pgc.cell_count; c++)
        {
            hddvd_vts_cell cell;
            hddvd_vts_get_cell(vts, p, c, &cell);
            sum += ticks(&cell.playback_time);
            printf("  cell %zu evob=%u cell=%u time=%.3fs sectors=%lu-%lu last-vobu=%lu "
                   "command=%u\n",
                   c + 1, (unsigned)cell.evob_id, (unsigned)cell.cell_id,
                   (double)ticks(&cell.playback_time) / 90000.0,
                   (unsigned long)cell.first_vobu_sector, (unsigned long)cell.last_sector,
                   (unsigned long)cell.last_vobu_sector, (unsigned)cell.command);
        }
        check(sum == ticks(&pgc.playback_time), "PGC time against its cells", p + 1, 0);
        if (!hddvd_vts_get_time_map(vts, p, &unit, &entries))
        {
            printf("  time-map unit=%us entries=%zu\n", (unsigned)unit, entries);
            for (k = 0; k < entries; k++)
            {
                hddvd_vts_map_entry entry, next;
                size_t here = SIZE_MAX, there = SIZE_MAX;
                hddvd_vts_get_time_map_entry(vts, p, k, &entry);
                for (c = 0; c < pgc.cell_count; c++)
                {
                    hddvd_vts_cell cell;
                    hddvd_vts_get_cell(vts, p, c, &cell);
                    if (entry.sector >= cell.first_vobu_sector && entry.sector <= cell.last_sector)
                        here = c;
                    if (k + 1 < entries && !hddvd_vts_get_time_map_entry(vts, p, k + 1, &next) &&
                        next.sector >= cell.first_vobu_sector && next.sector <= cell.last_sector)
                        there = c;
                }
                check(entry.discontinuity == (here != there), "discontinuity flag", p + 1, k + 1);
            }
        }
    }
}

int main(int argc, char **argv)
{
    hddvd_stream *stream = NULL;
    hddvd_vts *vts = NULL;
    hddvd_vmg *vmg = NULL;
    size_t i;
    int rc;
    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "usage: %s HVnnnI01.IFO [HVnnnT01.EVO]\n", argv[0]);
        return 2;
    }
    rc = hddvd_stream_open_file(argv[1], &stream);
    if (!rc)
    {
        rc = hddvd_vts_open(stream, &vts);
        if (rc == HDDVD_ENOTSUP)
            rc = hddvd_vmg_open(stream, &vmg);
    }
    hddvd_stream_close(stream);
    if (rc)
    {
        fprintf(stderr, "%s: %s\n", argv[1], hddvd_strerror(rc));
        return 1;
    }
    if (vmg)
    {
        printf("vmg last-sector=%lu title-sets=%zu titles=%zu\n",
               (unsigned long)hddvd_vmg_last_sector(vmg), hddvd_vmg_title_set_count(vmg),
               hddvd_vmg_title_count(vmg));
        for (i = 0; i < hddvd_vmg_title_count(vmg); i++)
        {
            hddvd_vmg_title title;
            hddvd_vmg_get_title(vmg, i, &title);
            printf("title %zu title-set=%u/%u chapters=%u angles=%u sector=%lu type=0x%02x\n",
                   i + 1, (unsigned)title.title_set, (unsigned)title.title_set_title,
                   (unsigned)title.chapter_count, (unsigned)title.angle_count,
                   (unsigned long)title.title_set_sector, (unsigned)title.playback_type);
        }
        hddvd_vmg_close(vmg);
        return 0;
    }
    print_vts(vts);
    if (argc == 3)
        check_evo(vts, argv[2]);
    hddvd_vts_close(vts);
    printf("summary %s\n", failures ? "MISMATCH" : "ok");
    return failures ? 1 : 0;
}
