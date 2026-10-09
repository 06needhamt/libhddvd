#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>

/* Prints a JSON summary of how the typed XPL model represents a playlist,
   including every construct it parsed but does not model. Element and
   attribute names are XML names and need no JSON escaping. */
int main(int argc, char **argv)
{
    FILE *f;
    long end;
    unsigned char *bytes;
    hddvd_xpl *xpl = NULL;
    size_t i, clips = 0, chapters = 0, tracks = 0, events = 0, applications;
    int rc;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s PLAYLIST.xpl\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f || fseek(f, 0, SEEK_END) || (end = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: cannot read input\n", argv[0]);
        if (f)
            fclose(f);
        return 1;
    }
    bytes = malloc((size_t)end);
    if (!bytes || fread(bytes, 1, (size_t)end, f) != (size_t)end)
    {
        fprintf(stderr, "%s: cannot read input\n", argv[0]);
        free(bytes);
        fclose(f);
        return 1;
    }
    fclose(f);
    rc = hddvd_xpl_open(bytes, (size_t)end, &xpl);
    free(bytes);
    if (rc != HDDVD_OK)
    {
        printf("{\"status\": \"error\", \"code\": %d, \"message\": \"%s\"}\n", rc,
               hddvd_strerror(rc));
        return 1;
    }
    applications = hddvd_xpl_application_count(xpl, SIZE_MAX);
    for (i = 0; i < hddvd_xpl_title_count(xpl); i++)
    {
        clips += hddvd_xpl_clip_count(xpl, i);
        chapters += hddvd_xpl_chapter_count(xpl, i);
        tracks += hddvd_xpl_track_count(xpl, i);
        events += hddvd_xpl_event_count(xpl, i);
        applications += hddvd_xpl_application_count(xpl, i);
    }
    printf("{\"status\": \"ok\", \"frame_rate\": %u, \"titles\": %zu, \"clips\": %zu, "
           "\"chapters\": %zu, \"tracks\": %zu, \"events\": %zu, \"applications\": %zu, "
           "\"unmodeled\": [",
           (unsigned)hddvd_xpl_frame_rate(xpl), hddvd_xpl_title_count(xpl), clips, chapters, tracks,
           events, applications);
    for (i = 0; i < hddvd_xpl_unmodeled_count(xpl); i++)
    {
        hddvd_xpl_unmodeled record;
        hddvd_xpl_get_unmodeled(xpl, i, &record);
        printf("%s{\"element\": \"%s\", \"attribute\": ", i ? ", " : "", record.element);
        if (record.attribute)
            printf("\"%s\"", record.attribute);
        else
            printf("null");
        printf(", \"occurrences\": %zu}", record.occurrences);
    }
    printf("]}\n");
    hddvd_xpl_close(xpl);
    return 0;
}
