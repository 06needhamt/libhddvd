#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>

/* Reads the input as a Standard Content title set and as a VMG, and walks
   everything each one exposes. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_stream *stream = NULL;
    hddvd_vts *vts = NULL;
    hddvd_vmg *vmg = NULL;
    size_t i, j;
    if (hddvd_stream_open_memory(data, size, &stream) != HDDVD_OK)
        return 0;
    if (hddvd_vts_open(stream, &vts) == HDDVD_OK)
    {
        hddvd_vts_info info;
        hddvd_vts_evob_cell extent;
        uint32_t sector;
        (void)hddvd_vts_get_info(vts, &info);
        for (i = 0; i < hddvd_vts_title_count(vts); i++)
            for (j = 0; j < hddvd_vts_chapter_count(vts, i); j++)
            {
                hddvd_vts_chapter chapter;
                (void)hddvd_vts_get_chapter(vts, i, j, &chapter);
            }
        for (i = 0; i < hddvd_vts_pgc_count(vts); i++)
        {
            hddvd_vts_pgc pgc;
            hddvd_time t;
            uint8_t command[8], unit;
            uint16_t cell_number;
            size_t entries;
            (void)hddvd_vts_get_pgc(vts, i, &pgc);
            (void)hddvd_vts_time_to_ticks(&pgc.playback_time, &t);
            for (j = 0; j < pgc.program_count; j++)
                (void)hddvd_vts_get_program(vts, i, j, &cell_number);
            for (j = 0; j < pgc.cell_count; j++)
            {
                hddvd_vts_cell cell;
                (void)hddvd_vts_get_cell(vts, i, j, &cell);
            }
            for (j = 0; j < pgc.pre_command_count; j++)
                (void)hddvd_vts_get_command(vts, i, HDDVD_VTS_PRE_COMMAND, j, command);
            for (j = 0; j < pgc.cell_command_count; j++)
                (void)hddvd_vts_get_command(vts, i, HDDVD_VTS_CELL_COMMAND, j, command);
            if (hddvd_vts_get_time_map(vts, i, &unit, &entries) == HDDVD_OK)
                for (j = 0; j < entries; j++)
                {
                    hddvd_vts_map_entry entry;
                    (void)hddvd_vts_get_time_map_entry(vts, i, j, &entry);
                }
        }
        for (i = 0; i < hddvd_vts_evob_cell_count(vts); i++)
            (void)hddvd_vts_get_evob_cell(vts, i, &extent);
        for (i = 0; i < hddvd_vts_vobu_count(vts); i++)
            (void)hddvd_vts_get_vobu(vts, i, &sector);
        hddvd_vts_close(vts);
    }
    if (hddvd_vmg_open(stream, &vmg) == HDDVD_OK)
    {
        hddvd_vmg_title title;
        for (i = 0; i < hddvd_vmg_title_count(vmg); i++)
            (void)hddvd_vmg_get_title(vmg, i, &title);
        hddvd_vmg_close(vmg);
    }
    hddvd_stream_close(stream);
    return 0;
}
