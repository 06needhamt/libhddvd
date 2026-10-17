#include "hddvd.h"
#include <stdio.h>

/* Lists the EVOBs a VTI file describes, then checks each time map given
   after it against the VTI record of the EVOB it names. */
int main(int argc, char **argv)
{
    hddvd_stream *stream = NULL;
    hddvd_vti *vti = NULL;
    size_t i;
    int a, rc, failed = 0;
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s HVA00001.VTI [FILE.MAP ...]\n", argv[0]);
        return 2;
    }
    rc = hddvd_stream_open_file(argv[1], &stream);
    if (!rc)
        rc = hddvd_vti_open(stream, &vti);
    hddvd_stream_close(stream);
    if (rc)
    {
        fprintf(stderr, "%s: %s\n", argv[1], hddvd_strerror(rc));
        return 1;
    }
    for (i = 0; i < hddvd_vti_evob_count(vti); i++)
    {
        hddvd_vti_evob evob;
        hddvd_vti_get_evob(vti, i, &evob);
        printf("evob %u %s start=%llu end=%llu duration=%.3fs sectors=%lu attributes=%u\n",
               (unsigned)evob.number, evob.file_name, (unsigned long long)evob.start.ticks,
               (unsigned long long)evob.end.ticks,
               (double)(evob.end.ticks - evob.start.ticks) / 90000.0,
               (unsigned long)evob.sector_count, (unsigned)evob.attribute_number);
    }
    for (a = 2; a < argc; a++)
    {
        hddvd_tmap *tmap = NULL;
        hddvd_vti_evob evob;
        size_t index;
        stream = NULL;
        rc = hddvd_stream_open_file(argv[a], &stream);
        if (!rc)
            rc = hddvd_tmap_open(stream, &tmap);
        hddvd_stream_close(stream);
        if (!rc)
        {
            printf("map %s evob=%u entries=%zu sectors=%llu duration=%.3fs", argv[a],
                   (unsigned)hddvd_tmap_evob_number(tmap), hddvd_tmap_entry_count(tmap),
                   (unsigned long long)hddvd_tmap_sector_count(tmap),
                   (double)hddvd_tmap_duration(tmap).ticks / 90000.0);
            rc = hddvd_vti_find_evob(vti, hddvd_tmap_evob_number(tmap), &index);
            if (!rc)
            {
                hddvd_vti_get_evob(vti, index, &evob);
                rc = hddvd_tmap_check_evob(tmap, &evob);
            }
            printf(" %s\n", rc ? "MISMATCH" : "ok");
            hddvd_tmap_close(tmap);
        }
        else
            printf("map %s error: %s\n", argv[a], hddvd_strerror(rc));
        failed |= rc != HDDVD_OK;
    }
    hddvd_vti_close(vti);
    return failed;
}
