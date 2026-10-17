#include "hddvd.h"
#include <stdint.h>

/* Feeds the input to both the VTI and the time-map readers, then walks and
   cross-checks whatever they accept. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_stream *stream;
    hddvd_vti *vti = NULL;
    hddvd_tmap *tmap = NULL;
    size_t i, index;
    if (hddvd_stream_open_memory(data, size, &stream) != HDDVD_OK)
        return 0;
    (void)hddvd_vti_open(stream, &vti);
    (void)hddvd_tmap_open(stream, &tmap);
    hddvd_stream_close(stream);
    if (tmap)
    {
        hddvd_tmap_entry entry;
        hddvd_time t = hddvd_tmap_duration(tmap);
        for (i = 0; i < hddvd_tmap_entry_count(tmap); i++)
            (void)hddvd_tmap_get_entry(tmap, i, &entry);
        (void)hddvd_tmap_find(tmap, t, &index);
        t.ticks /= 2;
        (void)hddvd_tmap_find(tmap, t, &index);
    }
    if (vti)
    {
        hddvd_vti_evob evob;
        for (i = 0; i < hddvd_vti_evob_count(vti); i++)
            if (hddvd_vti_get_evob(vti, i, &evob) == HDDVD_OK && tmap)
                (void)hddvd_tmap_check_evob(tmap, &evob);
        (void)hddvd_vti_find_evob(vti, hddvd_tmap_evob_number(tmap), &index);
    }
    hddvd_tmap_close(tmap);
    hddvd_vti_close(vti);
    return 0;
}
