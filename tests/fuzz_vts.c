/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>

/* Walks a menu PGC's commands and cells. */
static void menu_pgc(const hddvd_vts *vts, const hddvd_vmg *vmg, size_t unit, size_t p)
{
    hddvd_menu_pgc category;
    hddvd_vts_pgc pgc;
    hddvd_vts_cell cell;
    uint8_t command[8];
    unsigned kind, counts[3];
    size_t i;
    if ((vts ? hddvd_vts_get_menu_pgc(vts, unit, p, &category, &pgc)
             : hddvd_vmg_get_menu_pgc(vmg, unit, p, &category, &pgc)) != HDDVD_OK)
        return;
    counts[0] = pgc.pre_command_count;
    counts[1] = pgc.post_command_count;
    counts[2] = pgc.cell_command_count;
    for (kind = 0; kind < 3; kind++)
        for (i = 0; i < counts[kind]; i++)
            (void)(vts ? hddvd_vts_get_menu_command(vts, unit, p, (hddvd_vts_command_kind)kind, i,
                                                    command)
                       : hddvd_vmg_get_menu_command(vmg, unit, p, (hddvd_vts_command_kind)kind, i,
                                                    command));
    for (i = 0; i < pgc.cell_count; i++)
        (void)(vts ? hddvd_vts_get_menu_cell(vts, unit, p, i, &cell)
                   : hddvd_vmg_get_menu_cell(vmg, unit, p, i, &cell));
}

/* Reads the input as a Standard Content title set and as a VMG, and walks
   everything each one exposes; then runs it as navigation commands, from
   the start and from the line its first byte names. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_menu_unit unit;
    hddvd_vm *vm = NULL;
    hddvd_vm_result result;
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
        hddvd_video_attributes video;
        hddvd_stream_attributes attributes;
        (void)hddvd_vts_get_info(vts, &info);
        (void)hddvd_vts_get_video_attributes(vts, &video);
        for (i = 0; i < hddvd_vts_stream_count(vts, HDDVD_STREAM_AUDIO); i++)
            (void)hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_AUDIO, i, &attributes);
        for (i = 0; i < hddvd_vts_stream_count(vts, HDDVD_STREAM_SUBPICTURE); i++)
            (void)hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_SUBPICTURE, i, &attributes);
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
        for (i = 0; i < hddvd_vts_menu_unit_count(vts); i++)
            if (hddvd_vts_get_menu_unit(vts, i, &unit) == HDDVD_OK)
                for (j = 0; j < unit.pgc_count; j++)
                    menu_pgc(vts, NULL, i, j);
        hddvd_vts_close(vts);
    }
    if (hddvd_vmg_open(stream, &vmg) == HDDVD_OK)
    {
        hddvd_vmg_title title;
        hddvd_vts_pgc first_play;
        uint8_t command[8];
        for (i = 0; i < hddvd_vmg_title_count(vmg); i++)
            (void)hddvd_vmg_get_title(vmg, i, &title);
        for (i = 0; i < hddvd_vmg_menu_unit_count(vmg); i++)
            if (hddvd_vmg_get_menu_unit(vmg, i, &unit) == HDDVD_OK)
                for (j = 0; j < unit.pgc_count; j++)
                    menu_pgc(NULL, vmg, i, j);
        if (hddvd_vmg_get_first_play(vmg, &first_play) == HDDVD_OK)
            for (i = 0; i < first_play.pre_command_count; i++)
                (void)hddvd_vmg_get_first_play_command(vmg, HDDVD_VTS_PRE_COMMAND, i, command);
        hddvd_vmg_close(vmg);
    }
    hddvd_stream_close(stream);
    if (hddvd_vm_create(size ? data[0] : 0, &vm) == HDDVD_OK)
    {
        size_t count = size / 8;
        (void)hddvd_vm_run(vm, data, count, 0, &result);
        if (count)
            (void)hddvd_vm_run(vm, data, count, data[0] % count, &result);
        hddvd_vm_destroy(vm);
    }
    return 0;
}
