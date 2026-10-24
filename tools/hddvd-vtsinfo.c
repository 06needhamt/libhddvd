#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prints a Standard Content IFO. For a title set it also checks what the
   library does not enforce: PGC times against their cells, time-map
   discontinuity flags against cell boundaries, and the targets of the
   navigation commands, which it prints in DVD-Video's command syntax.
   Given the title EVO, it checks the IFO against the EVO's navigation packs
   (docs/formats/ifo-map.md): the VOBU map, cell durations and elapsed times,
   and every time-map entry; and the stream attributes against the EVO's
   streams. For the VMG it prints the title table, the first-play PGC and
   where its commands lead (hddvd_vm_run), and the menus' commands. */

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

/* Navigation commands, in DVD-Video's 8-byte encoding (docs/formats/ifo-map.md).
   Registers below 0x80 are general (HD DVD uses up to 63), the rest system. */
typedef struct command_context
{
    /* The title set, or NULL for the VMG's PGCs. */
    const hddvd_vts *vts;
    const hddvd_vts_pgc *pgc;
    /* PGCs LinkPGCN can name: the title set's, or the menu unit's. */
    size_t pgc_count;
    /* VMG titles JumpTT can name, when known. */
    size_t title_count;
    int menu;
    size_t group_count;
    int unconfirmed;
} command_context;

static const char *const comparisons[] = {"", "&", "==", "!=", ">=", ">", "<=", "<"};
static const char *const set_ops[] = {
    "", "=", "<->", "+=", "-=", "*=", "/=", "%=", "rnd", "&=", "|=", "^=", "?", "?", "?", "?"};
static const char *const domains[] = {"first-play", "vmg-menu", "vts-menu", "vmg-pgc"};
static const char *const actions[] = {
    "end",         "link",        "jump-title", "jump-vts-title", "jump-vts-chapter",
    "jump-system", "call-system", "exit",       "unsupported"};

static size_t append(char *out, size_t size, size_t used, const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
#include <stdarg.h>
static size_t append(char *out, size_t size, size_t used, const char *format, ...)
{
    va_list args;
    int n;
    if (used >= size)
        return used;
    va_start(args, format);
    n = vsnprintf(out + used, size - used, format, args);
    va_end(args);
    return n < 0 ? used : used + (size_t)n;
}

static size_t reg(char *out, size_t size, size_t used, unsigned r)
{
    if (r & 0x80)
        return append(out, size, used, "SPRM%u", r & 0x1fu);
    check(r <= 63, "general register number", r, 63);
    return append(out, size, used, "GPRM%u", r);
}

/* "if (r op operand) ": the register at byte reg_at, then 16 bits of data or
   a register at operand_at, chosen by the immediate bit. */
static size_t condition(char *out, size_t size, size_t used, const uint8_t *c, unsigned op,
                        int immediate, size_t reg_at, size_t operand_at)
{
    if (!op)
        return used;
    used = append(out, size, used, "if (");
    used = reg(out, size, used, c[reg_at]);
    used = append(out, size, used, " %s ", comparisons[op]);
    if (immediate)
        used = append(out, size, used, "%u", (unsigned)(c[operand_at] << 8 | c[operand_at + 1]));
    else
        used = reg(out, size, used, c[operand_at + 1]);
    return append(out, size, used, ") ");
}

static size_t chapters_of(const hddvd_vts *vts, size_t title)
{
    return vts && title && title <= hddvd_vts_title_count(vts)
               ? hddvd_vts_chapter_count(vts, title - 1)
               : 0;
}

/* The link part of type 1 to 3 commands: subset in byte 1, target in bytes
   6 and 7. */
static size_t link_part(char *out, size_t size, size_t used, const uint8_t *c, command_context *ctx,
                        int optional)
{
    unsigned subset = c[1] & 0x0fu, target = (unsigned)(c[6] << 8 | c[7]);
    switch (subset)
    {
        case 0:
            if (!optional)
                ctx->unconfirmed++;
            return optional ? used : append(out, size, used, "link?");
        case 1:
            return append(out, size, used, "LinkSIns %u (button %u)", c[7] & 0x1fu, c[6] >> 2);
        case 4:
            check((target & 0x7fffu) >= 1 && (target & 0x7fffu) <= ctx->pgc_count,
                  "LinkPGCN target", target & 0x7fffu, ctx->pgc_count);
            return append(out, size, used, "LinkPGCN %u", target & 0x7fffu);
        case 5:
            if (!ctx->menu)
                check((target & 0x3ffu) >= 1 &&
                          (target & 0x3ffu) <= chapters_of(ctx->vts, ctx->pgc->title),
                      "LinkPTTN target", target & 0x3ffu, chapters_of(ctx->vts, ctx->pgc->title));
            return append(out, size, used, "LinkPTTN %u (button %u)", target & 0x3ffu, c[6] >> 2);
        case 6:
            check((c[7] & 0x7fu) >= 1 && (c[7] & 0x7fu) <= ctx->pgc->program_count,
                  "LinkPGN target", c[7] & 0x7fu, ctx->pgc->program_count);
            return append(out, size, used, "LinkPGN %u (button %u)", c[7] & 0x7fu, c[6] >> 2);
        case 7:
            check(c[7] >= 1 && c[7] <= ctx->pgc->cell_count, "LinkCN target", c[7],
                  ctx->pgc->cell_count);
            return append(out, size, used, "LinkCN %u (button %u)", c[7], c[6] >> 2);
        default:
            ctx->unconfirmed++;
            return append(out, size, used, "link%u?", subset);
    }
}

/* JumpSS and CallSS: the domain in byte 5's bits 5 and 4; a menu id in its
   low nibble or a VMG PGC in bytes 2 and 3. Bit 7 of byte 5 is unexplained. */
static size_t system_jump(char *out, size_t size, size_t used, const uint8_t *c)
{
    unsigned domain = (c[5] >> 4) & 3u;
    used = append(out, size, used, "%s %s", (c[1] & 0x0fu) == 6 ? "JumpSS" : "CallSS",
                  domains[domain]);
    if (domain == 3)
        used = append(out, size, used, " %u", (unsigned)(c[2] << 8 | c[3]) & 0x7fffu);
    else if (domain)
        used = append(out, size, used, " %u", c[5] & 0x0fu);
    if ((c[1] & 0x0fu) == 8)
        used = append(out, size, used, " (resume cell %u)", c[4]);
    return used;
}

static void disassemble(const uint8_t *c, command_context *ctx, char *out, size_t size)
{
    unsigned type = c[0] >> 5, op = (c[1] >> 4) & 7u, sub = c[0] & 0x0fu;
    int immediate = c[1] >> 7;
    size_t used = 0;
    out[0] = '\0';
    switch (type)
    {
        case 0:
            used = condition(out, size, used, c, op, immediate, 3, 4);
            switch (c[1] & 0x0fu)
            {
                case 0:
                    append(out, size, used, "Nop");
                    return;
                case 1:
                    check(c[7] >= 1 && c[7] <= ctx->group_count, "Goto line", c[7],
                          ctx->group_count);
                    append(out, size, used, "Goto %u", c[7]);
                    return;
                case 2:
                    append(out, size, used, "Break");
                    return;
                default:
                    break;
            }
            break;
        case 1:
            if (c[0] & 0x10)
            {
                unsigned title = c[5] & 0x7fu;
                used = condition(out, size, used, c, op, 0, 6, 6);
                switch (c[1] & 0x0fu)
                {
                    case 1:
                        append(out, size, used, "Exit");
                        return;
                    case 2:
                        check(title >= 1 && (!ctx->title_count || title <= ctx->title_count),
                              "JumpTT target", title, ctx->title_count);
                        append(out, size, used, "JumpTT %u", title);
                        return;
                    case 3:
                        if (ctx->vts)
                            check(title >= 1 && title <= hddvd_vts_title_count(ctx->vts),
                                  "JumpVTS_TT target", title, hddvd_vts_title_count(ctx->vts));
                        append(out, size, used, "JumpVTS_TT %u", title);
                        return;
                    case 5:
                    {
                        unsigned chapter = (unsigned)(c[2] << 8 | c[3]) & 0x3ffu;
                        if (ctx->vts)
                            check(chapter >= 1 && chapter <= chapters_of(ctx->vts, title),
                                  "JumpVTS_PTT target", chapter, chapters_of(ctx->vts, title));
                        append(out, size, used, "JumpVTS_PTT %u:%u", title, chapter);
                        return;
                    }
                    case 6:
                    case 8:
                        system_jump(out, size, used, c);
                        return;
                    default:
                        break;
                }
                break;
            }
            used = condition(out, size, used, c, op, immediate, 3, 4);
            link_part(out, size, used, c, ctx, 0);
            return;
        case 2:
            /* SetSTN and SetHL_BTNN; buttons count in units of 1024. */
            used = condition(out, size, used, c, op, 0, 6, 6);
            if (sub == 6)
            {
                if (c[0] & 0x10)
                    used = append(out, size, used, "SetHL_BTNN %u",
                                  (unsigned)(c[4] << 8 | c[5]) >> 10);
                else
                    used = reg(out, size, append(out, size, used, "SetHL_BTNN "), c[5]);
            }
            else if (sub == 1)
            {
                static const char *const names[] = {"audio", "subpicture", "angle"};
                unsigned i;
                used = append(out, size, used, "SetSTN");
                for (i = 0; i < 3; i++)
                {
                    if (!(c[3 + i] & 0x80))
                        continue;
                    used = append(out, size, used, " %s=", names[i]);
                    if (c[0] & 0x10)
                        used = append(out, size, used, "%u", c[3 + i] & 0x7fu);
                    else
                        used = reg(out, size, used, c[3 + i] & 0x0fu);
                }
            }
            else
                break;
            if (c[1] & 0x0fu)
                link_part(out, size, append(out, size, used, "; "), c, ctx, 1);
            return;
        case 3:
            /* An optional condition on the register in byte 2. */
            if (!sub || sub > 11 || (sub == 2 && c[0] & 0x10))
                break;
            used = condition(out, size, used, c, op, immediate, 2, 6);
            used = reg(out, size, used, c[3]);
            used = append(out, size, used, " %s ", set_ops[sub]);
            if (c[0] & 0x10)
                used = append(out, size, used, "%u", (unsigned)(c[4] << 8 | c[5]));
            else
                used = reg(out, size, used, c[5]);
            if (c[1] & 0x0fu)
                link_part(out, size, append(out, size, used, "; "), c, ctx, 1);
            return;
        default:
            break;
    }
    /* Not a form these commands were checked in. */
    ctx->unconfirmed++;
    snprintf(out, size, "unconfirmed %02x%02x %02x%02x %02x%02x %02x%02x", c[0], c[1], c[2], c[3],
             c[4], c[5], c[6], c[7]);
}

/* Fetches a command of the PGC being printed. */
typedef int (*command_fn)(const void *owner, size_t unit, size_t pgc, hddvd_vts_command_kind kind,
                          size_t index, uint8_t command[8]);

static int title_command(const void *owner, size_t unit, size_t pgc, hddvd_vts_command_kind kind,
                         size_t index, uint8_t command[8])
{
    (void)unit;
    return hddvd_vts_get_command(owner, pgc, kind, index, command);
}

static int vts_menu_command(const void *owner, size_t unit, size_t pgc, hddvd_vts_command_kind kind,
                            size_t index, uint8_t command[8])
{
    return hddvd_vts_get_menu_command(owner, unit, pgc, kind, index, command);
}

static int vmg_menu_command(const void *owner, size_t unit, size_t pgc, hddvd_vts_command_kind kind,
                            size_t index, uint8_t command[8])
{
    return hddvd_vmg_get_menu_command(owner, unit, pgc, kind, index, command);
}

static int first_play_command(const void *owner, size_t unit, size_t pgc,
                              hddvd_vts_command_kind kind, size_t index, uint8_t command[8])
{
    (void)unit;
    (void)pgc;
    return hddvd_vmg_get_first_play_command(owner, kind, index, command);
}

static int print_commands(command_context *ctx, command_fn fetch, const void *owner, size_t unit,
                          size_t p)
{
    static const char *const names[] = {"pre", "post", "cell"};
    const unsigned counts[] = {ctx->pgc->pre_command_count, ctx->pgc->post_command_count,
                               ctx->pgc->cell_command_count};
    unsigned kind;
    size_t i;
    for (kind = 0; kind < 3; kind++)
    {
        ctx->group_count = counts[kind];
        for (i = 0; i < counts[kind]; i++)
        {
            uint8_t command[8];
            char text[160];
            if (fetch(owner, unit, p, (hddvd_vts_command_kind)kind, i, command))
                continue;
            disassemble(command, ctx, text, sizeof(text));
            printf("  %s %zu: %s\n", names[kind], i + 1, text);
        }
    }
    return ctx->unconfirmed;
}

static void print_menu_pgc(size_t u, size_t p, const hddvd_menu_pgc *category,
                           const hddvd_vts_pgc *pgc)
{
    printf("menu %zu pgc %zu entry=%u menu-id=%u category=%02x%02x%02x%02x programs=%u cells=%u "
           "commands=%u/%u/%u\n",
           u + 1, p + 1, (unsigned)category->entry, (unsigned)category->menu_id,
           category->category[0], category->category[1], category->category[2],
           category->category[3], (unsigned)pgc->program_count, (unsigned)pgc->cell_count,
           (unsigned)pgc->pre_command_count, (unsigned)pgc->post_command_count,
           (unsigned)pgc->cell_command_count);
}

static void print_menu_unit(size_t u, const hddvd_menu_unit *unit)
{
    printf("menu %zu language=%s extension=%u existence=0x%02x pgcs=%u\n", u + 1,
           unit->language[0] ? unit->language : "-", (unsigned)unit->language_extension,
           (unsigned)unit->existence, (unsigned)unit->pgc_count);
}

static size_t print_vts_menus(const hddvd_vts *vts)
{
    size_t u, p, unconfirmed = 0;
    for (u = 0; u < hddvd_vts_menu_unit_count(vts); u++)
    {
        hddvd_menu_unit unit;
        hddvd_vts_get_menu_unit(vts, u, &unit);
        print_menu_unit(u, &unit);
        for (p = 0; p < unit.pgc_count; p++)
        {
            hddvd_menu_pgc category;
            hddvd_vts_pgc pgc;
            command_context ctx;
            hddvd_vts_get_menu_pgc(vts, u, p, &category, &pgc);
            print_menu_pgc(u, p, &category, &pgc);
            memset(&ctx, 0, sizeof(ctx));
            ctx.vts = vts;
            ctx.pgc = &pgc;
            ctx.pgc_count = unit.pgc_count;
            ctx.menu = 1;
            unconfirmed += (size_t)print_commands(&ctx, vts_menu_command, vts, u, p);
        }
    }
    return unconfirmed;
}

/* Runs the first-play PGC's pre commands from cleared registers and prints
   where they lead. */
static void trace_first_play(const hddvd_vmg *vmg, const hddvd_vts_pgc *pgc)
{
    uint8_t *commands = malloc((size_t)pgc->pre_command_count * 8u + 8u);
    hddvd_vm *vm = NULL;
    hddvd_vm_result r;
    size_t i;
    int rc;
    if (!commands || hddvd_vm_create(1, &vm))
    {
        free(commands);
        return;
    }
    for (i = 0; i < pgc->pre_command_count; i++)
        hddvd_vmg_get_first_play_command(vmg, HDDVD_VTS_PRE_COMMAND, i, commands + i * 8u);
    rc = hddvd_vm_run(vm, commands, pgc->pre_command_count, 0, &r);
    printf("first-play run: status=%s action=%s domain=%s target=%u line=%zu steps=%zu\n",
           hddvd_strerror(rc), actions[r.action], domains[r.domain], (unsigned)r.target, r.line,
           r.steps);
    check(rc == HDDVD_OK && r.action != HDDVD_VM_UNSUPPORTED, "first-play run", r.line, 0);
    hddvd_vm_destroy(vm);
    free(commands);
}

static void print_vmg(const hddvd_vmg *vmg)
{
    size_t i, u, p, unconfirmed = 0;
    hddvd_vts_pgc pgc;
    command_context ctx;
    printf("vmg last-sector=%lu title-sets=%zu titles=%zu\n",
           (unsigned long)hddvd_vmg_last_sector(vmg), hddvd_vmg_title_set_count(vmg),
           hddvd_vmg_title_count(vmg));
    for (i = 0; i < hddvd_vmg_title_count(vmg); i++)
    {
        hddvd_vmg_title title;
        hddvd_vmg_get_title(vmg, i, &title);
        printf("title %zu title-set=%u/%u chapters=%u angles=%u sector=%lu type=0x%02x\n", i + 1,
               (unsigned)title.title_set, (unsigned)title.title_set_title,
               (unsigned)title.chapter_count, (unsigned)title.angle_count,
               (unsigned long)title.title_set_sector, (unsigned)title.playback_type);
    }
    if (!hddvd_vmg_get_first_play(vmg, &pgc))
    {
        printf("first-play programs=%u cells=%u commands=%u/%u/%u\n", (unsigned)pgc.program_count,
               (unsigned)pgc.cell_count, (unsigned)pgc.pre_command_count,
               (unsigned)pgc.post_command_count, (unsigned)pgc.cell_command_count);
        memset(&ctx, 0, sizeof(ctx));
        ctx.pgc = &pgc;
        ctx.menu = 1;
        ctx.title_count = hddvd_vmg_title_count(vmg);
        ctx.pgc_count = hddvd_vmg_menu_unit_count(vmg) ? SIZE_MAX : 0;
        unconfirmed += (size_t)print_commands(&ctx, first_play_command, vmg, 0, 0);
        trace_first_play(vmg, &pgc);
    }
    for (u = 0; u < hddvd_vmg_menu_unit_count(vmg); u++)
    {
        hddvd_menu_unit unit;
        hddvd_vmg_get_menu_unit(vmg, u, &unit);
        print_menu_unit(u, &unit);
        for (p = 0; p < unit.pgc_count; p++)
        {
            hddvd_menu_pgc category;
            hddvd_vmg_get_menu_pgc(vmg, u, p, &category, &pgc);
            print_menu_pgc(u, p, &category, &pgc);
            memset(&ctx, 0, sizeof(ctx));
            ctx.pgc = &pgc;
            ctx.pgc_count = unit.pgc_count;
            ctx.title_count = hddvd_vmg_title_count(vmg);
            ctx.menu = 1;
            unconfirmed += (size_t)print_commands(&ctx, vmg_menu_command, vmg, u, p);
        }
    }
    printf("commands unconfirmed=%zu\n", unconfirmed);
}

static const char *const video_codings[] = {"MPEG-2", "VC-1", "unknown", "unknown"};

static void print_attributes(const hddvd_vts *vts)
{
    static const char *const kinds[] = {"audio", "subpicture"};
    hddvd_video_attributes video;
    unsigned kind;
    size_t i;
    hddvd_vts_get_video_attributes(vts, &video);
    printf("video %s (%02x%02x%02x%02x)\n", video_codings[video.coding & 3u], video.raw[0],
           video.raw[1], video.raw[2], video.raw[3]);
    for (kind = 0; kind < 2; kind++)
        for (i = 0; i < hddvd_vts_stream_count(vts, (hddvd_stream_kind)kind); i++)
        {
            hddvd_stream_attributes stream;
            hddvd_vts_get_stream_attributes(vts, (hddvd_stream_kind)kind, i, &stream);
            printf("%s %zu language=%s (%02x%02x%02x%02x%02x%02x%02x%02x)\n", kinds[kind], i + 1,
                   stream.language[0] ? stream.language : "-", stream.raw[0], stream.raw[1],
                   stream.raw[2], stream.raw[3], stream.raw[4], stream.raw[5], stream.raw[6],
                   stream.raw[7]);
        }
}

/* The EVO's elementary streams against the IFO's attributes: the video
   coding, and the number of audio and subpicture streams. */
static void check_streams(const hddvd_vts *vts, const char *path)
{
    hddvd_stream_info streams[64];
    hddvd_video_attributes video;
    size_t count = 0, i, audio = 0, subpictures = 0;
    int coding = -1;
    if (hddvd_evo_probe(path, streams, 64, &count) || count > 64)
    {
        printf("MISMATCH cannot list the streams of %s\n", path);
        failures++;
        return;
    }
    for (i = 0; i < count; i++)
        switch (streams[i].codec)
        {
            case HDDVD_CODEC_MPEG2_VIDEO:
                coding = 0;
                break;
            case HDDVD_CODEC_VC1:
                coding = 1;
                break;
            case HDDVD_CODEC_SUBPICTURE:
                subpictures++;
                break;
            case HDDVD_CODEC_MPEG_AUDIO:
            case HDDVD_CODEC_AC3_EAC3:
            case HDDVD_CODEC_DTS_HD:
            case HDDVD_CODEC_LPCM:
            case HDDVD_CODEC_TRUEHD_MLP:
                audio++;
                break;
            default:
                break;
        }
    hddvd_vts_get_video_attributes(vts, &video);
    printf("evo streams video=%s audio=%zu subpicture=%zu\n",
           coding < 0 ? "none" : video_codings[coding], audio, subpictures);
    check(coding == (int)video.coding, "video coding", video.coding, (size_t)coding);
    check(audio == hddvd_vts_stream_count(vts, HDDVD_STREAM_AUDIO), "audio streams", audio,
          hddvd_vts_stream_count(vts, HDDVD_STREAM_AUDIO));
    check(subpictures == hddvd_vts_stream_count(vts, HDDVD_STREAM_SUBPICTURE), "subpicture streams",
          subpictures, hddvd_vts_stream_count(vts, HDDVD_STREAM_SUBPICTURE));
}

static void print_vts(const hddvd_vts *vts)
{
    hddvd_vts_info info;
    size_t t, c, p, k, unconfirmed = 0;
    command_context ctx;
    hddvd_vts_get_info(vts, &info);
    printf("title-set last-sector=%lu ifo-sectors=%lu menu-evo=%lu title-evo=%lu+%lu "
           "vobus=%zu evob-cells=%zu\n",
           (unsigned long)info.last_sector, (unsigned long)info.ifo_sectors,
           (unsigned long)info.menu_evo_sector, (unsigned long)info.title_evo_sector,
           (unsigned long)info.title_evo_sectors, hddvd_vts_vobu_count(vts),
           hddvd_vts_evob_cell_count(vts));
    print_attributes(vts);
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
        memset(&ctx, 0, sizeof(ctx));
        ctx.vts = vts;
        ctx.pgc = &pgc;
        ctx.pgc_count = hddvd_vts_pgc_count(vts);
        unconfirmed += (size_t)print_commands(&ctx, title_command, vts, 0, p);
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
    unconfirmed += print_vts_menus(vts);
    printf("commands unconfirmed=%zu\n", unconfirmed);
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
        print_vmg(vmg);
        hddvd_vmg_close(vmg);
        printf("summary %s\n", failures ? "MISMATCH" : "ok");
        return failures ? 1 : 0;
    }
    print_vts(vts);
    if (argc == 3)
    {
        check_evo(vts, argv[2]);
        check_streams(vts, argv[2]);
    }
    hddvd_vts_close(vts);
    printf("summary %s\n", failures ? "MISMATCH" : "ok");
    return failures ? 1 : 0;
}
