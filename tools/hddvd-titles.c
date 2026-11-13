#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lists a disc's playable titles: the Standard Content titles (the VMG's
   title table) and the Advanced Content titles (the playlist's), each with
   its chapters and the EVO extents it plays. --read also reads every title
   through and says whether its data is clear program stream, encrypted or
   damaged. --navigate follows the Standard Content disc's own commands from
   first play for up to COUNT cells. --guess turns on the navigator's
   guesses, which read DVD-Video's layout where HD DVD's is unknown (menu
   cells and their buttons, angle blocks, PGC links): a menu cell is listed
   with its buttons, and its selected button is pressed, as a test of the
   guesses on a real disc. */

static double seconds(hddvd_time time)
{
    return (double)time.ticks / 90000.0;
}

static void report(hddvd_ctx *ctx, const char *what, int rc)
{
    hddvd_error_detail detail;
    printf("%s: %s", what, hddvd_strerror(rc));
    if (hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == rc && detail.message[0])
        printf(" (%s: %s)", detail.resource, detail.message);
    printf("\n");
}

/* Returns the number of titles whose data could not be read through. */
static int list(hddvd_ctx *ctx, hddvd_titles *titles, int read)
{
    static unsigned char buffer[64 * 2048];
    size_t title, i;
    int unread = 0;
    for (title = 0; title < hddvd_titles_count(titles); title++)
    {
        hddvd_title_info info;
        hddvd_titles_get_info(titles, title, &info);
        printf("title %zu duration=%.3fs sectors=%llu chapters=%zu extents=%zu", title + 1,
               seconds(info.duration), (unsigned long long)(info.size / 2048u), info.chapter_count,
               info.extent_count);
        if (info.title_set)
            printf(" title-set=%u/%u angles=%u", info.title_set, info.title_set_title,
                   info.angle_count);
        printf("\n");
        for (i = 0; i < info.chapter_count; i++)
        {
            hddvd_title_chapter chapter;
            hddvd_titles_get_chapter(titles, title, i, &chapter);
            printf("  chapter %zu start=%.3fs sector=%llu\n", i + 1, seconds(chapter.start),
                   (unsigned long long)(chapter.offset / 2048u));
        }
        for (i = 0; i < info.extent_count; i++)
        {
            hddvd_title_extent extent;
            hddvd_titles_get_extent(titles, title, i, &extent);
            printf("  extent %zu %s sectors=%lu+%lu start=%.3fs duration=%.3fs\n", i + 1,
                   extent.evo_uri, (unsigned long)extent.first_sector,
                   (unsigned long)extent.sector_count, seconds(extent.start),
                   seconds(extent.duration));
        }
        if (read)
        {
            uint64_t offset = 0;
            size_t got = 0;
            int rc = HDDVD_OK;
            while (offset < info.size &&
                   (rc = hddvd_titles_read(titles, title, offset, buffer, sizeof(buffer), &got)) ==
                       HDDVD_OK &&
                   got)
                offset += got;
            if (rc)
            {
                char what[64];
                offset += got;
                snprintf(what, sizeof(what), "  read stopped at sector %llu",
                         (unsigned long long)(offset / 2048u));
                report(ctx, what, rc);
                unread++;
            }
            else
                printf("  read ok\n");
        }
    }
    return unread;
}

/* A menu step under the guesses: the cell, its buttons, then the selected
   button pressed (or, without buttons, the next cell). */
static int menu(hddvd_std_nav *nav, hddvd_std_nav_step *step)
{
    hddvd_std_nav_menu_cell cell;
    hddvd_std_nav_button b;
    size_t i, count = hddvd_std_nav_button_count(nav);
    hddvd_std_nav_get_menu_cell(nav, &cell);
    printf("navigate menu title-set=%u pgc=%u cell=%u evo=%s sectors=%lu+%lu still=%u "
           "buttons=%zu\n",
           step->title_set, step->pgc, step->cell, cell.evo_uri, (unsigned long)cell.first_sector,
           (unsigned long)cell.sector_count, cell.still, count);
    for (i = 0; i < count && hddvd_std_nav_get_button(nav, i, &b) == HDDVD_OK; i++)
        printf("  button %zu at %lu,%lu size %lux%lu up=%u down=%u left=%u right=%u%s\n", i + 1,
               (unsigned long)b.x, (unsigned long)b.y, (unsigned long)b.width,
               (unsigned long)b.height, b.up, b.down, b.left, b.right,
               b.auto_action ? " auto" : "");
    if (!count)
        return hddvd_std_nav_next(nav, step);
    printf("  press button %u\n", hddvd_std_nav_selected_button(nav));
    return hddvd_std_nav_activate_button(nav, step);
}

static int navigate(hddvd_ctx *ctx, unsigned long cells, int guess)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step;
    unsigned long i;
    int rc = hddvd_std_nav_open(ctx, &nav);
    if (!rc && guess)
        rc = hddvd_std_nav_set_options(nav, HDDVD_STD_NAV_GUESS_PGC_LINKS |
                                                HDDVD_STD_NAV_GUESS_ANGLES |
                                                HDDVD_STD_NAV_GUESS_MENU_CELLS);
    if (!rc)
        rc = hddvd_std_nav_start(nav, &step);
    for (i = 0; !rc && i < cells; i++)
    {
        if (step.kind == HDDVD_STD_NAV_MENU)
        {
            rc = menu(nav, &step);
            continue;
        }
        if (step.kind != HDDVD_STD_NAV_PLAY)
        {
            printf("navigate %s: %s\n", step.kind == HDDVD_STD_NAV_STOP ? "stop" : "unsupported",
                   step.reason);
            break;
        }
        printf("navigate play title=%zu chapter=%zu sectors=%llu+%llu pgc=%u cell=%u\n",
               step.title + 1, step.chapter + 1, (unsigned long long)(step.offset / 2048u),
               (unsigned long long)(step.size / 2048u), step.pgc, step.cell);
        rc = hddvd_std_nav_next(nav, &step);
    }
    if (rc)
        report(ctx, "navigate", rc);
    hddvd_std_nav_close(nav);
    return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
    hddvd_ctx *ctx;
    hddvd_titles *titles = NULL;
    hddvd_xpl *xpl = NULL;
    hddvd_error_detail detail;
    const char *root = NULL;
    unsigned long cells = 0;
    int read = 0, guess = 0, a, rc, found = 0, failed = 0;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--read"))
            read = 1;
        else if (!strcmp(argv[a], "--guess"))
            guess = 1;
        else if (!strcmp(argv[a], "--navigate") && a + 1 < argc)
            cells = strtoul(argv[++a], NULL, 10);
        else if (!root && argv[a][0] != '-')
            root = argv[a];
        else
            root = NULL, a = argc;
    }
    if (!root)
    {
        fprintf(stderr, "usage: %s [--read] [--navigate COUNT [--guess]] DISC_ROOT\n", argv[0]);
        return 2;
    }
    ctx = hddvd_open(root);
    if (!ctx)
    {
        fprintf(stderr, "%s: cannot open %s\n", argv[0], root);
        return 1;
    }

    rc = hddvd_titles_open(ctx, &titles);
    if (rc == HDDVD_OK)
    {
        printf("standard-content titles=%zu\n", hddvd_titles_count(titles));
        failed += list(ctx, titles, read);
        hddvd_titles_close(titles);
        found = 1;
        if (cells)
            failed += navigate(ctx, cells, guess);
    }
    /* Not finding the VMG itself only means no Standard Content. */
    else if (rc != HDDVD_ENOTFOUND || hddvd_get_last_error(ctx, &detail) != HDDVD_OK ||
             !strstr(detail.resource, "HV000I01.IFO"))
    {
        report(ctx, "standard-content", rc);
        failed++;
    }

    titles = NULL;
    rc = hddvd_open_playlist(ctx, &xpl);
    if (rc == HDDVD_OK)
        rc = hddvd_titles_open_playlist(ctx, xpl, &titles);
    if (rc == HDDVD_OK)
    {
        printf("advanced-content titles=%zu\n", hddvd_titles_count(titles));
        failed += list(ctx, titles, read);
        hddvd_titles_close(titles);
        found = 1;
    }
    else if (xpl || hddvd_has_advanced_content(ctx))
    {
        report(ctx, "advanced-content", rc);
        failed++;
    }
    hddvd_xpl_close(xpl);
    hddvd_close(ctx);
    if (!found && !failed)
    {
        printf("no titles\n");
        return 1;
    }
    printf("summary %s\n", failed ? "FAILED" : "ok");
    return failed ? 1 : 0;
}
