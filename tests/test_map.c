#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An original MAP-shaped buffer: identifier, end sector at 0x0c, VTI name at
   0x72. */
static unsigned char *make_map(size_t sectors, uint32_t last_sector, const char *vti)
{
    unsigned char *b = calloc(sectors, 2048);
    assert(b);
    memcpy(b, "HDDVD_TMAP00", 12);
    b[0x0c] = (unsigned char)(last_sector >> 24);
    b[0x0d] = (unsigned char)(last_sector >> 16);
    b[0x0e] = (unsigned char)(last_sector >> 8);
    b[0x0f] = (unsigned char)last_sector;
    memcpy(b + 0x72, vti, strlen(vti));
    return b;
}

static int read_header(const unsigned char *b, size_t size, hddvd_map_header *header)
{
    hddvd_stream *stream = NULL;
    int rc;
    assert(hddvd_stream_open_memory(b, size, &stream) == HDDVD_OK);
    rc = hddvd_map_read_header(stream, header);
    hddvd_stream_close(stream);
    return rc;
}

static void put32(unsigned char *b, uint32_t v)
{
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
}

static uint32_t entry(uint32_t reference, uint32_t units, uint32_t sectors)
{
    return reference << 21 | units << 11 | sectors;
}

/* An original time map: EVOB 7, three entries at 0x1a0. */
static unsigned char *make_tmap(void)
{
    unsigned char *b = make_map(1, 0, "ORIGINAL.VTI");
    put32(b + 0x180, 0x1a0);
    b[0x185] = 7;
    b[0x187] = 3;
    put32(b + 0x1a0, entry(2, 100, 10));
    put32(b + 0x1a4, entry(5, 80, 20));
    put32(b + 0x1a8, entry(7, 3, 7));
    return b;
}

static int open_tmap(const unsigned char *b, hddvd_tmap **tmap)
{
    hddvd_stream *stream = NULL;
    int rc;
    assert(hddvd_stream_open_memory(b, 2048, &stream) == HDDVD_OK);
    rc = hddvd_tmap_open(stream, tmap);
    hddvd_stream_close(stream);
    return rc;
}

static void test_tmap(void)
{
    unsigned char *b = make_tmap();
    hddvd_tmap *tmap;
    hddvd_tmap_entry e;
    hddvd_map_header header;
    hddvd_vti_evob evob;
    hddvd_time t;
    size_t index;

    assert(open_tmap(b, &tmap) == HDDVD_OK);
    assert(hddvd_tmap_get_header(tmap, &header) == HDDVD_OK &&
           !strcmp(header.vti_name, "ORIGINAL.VTI"));
    assert(hddvd_tmap_evob_number(tmap) == 7 && hddvd_tmap_entry_count(tmap) == 3);
    assert(hddvd_tmap_sector_count(tmap) == 37);
    /* 183 units of 3003/8 ticks; cumulative times round down once. */
    assert(hddvd_tmap_duration(tmap).ticks == 68693);
    assert(hddvd_tmap_get_entry(tmap, 0, &e) == HDDVD_OK);
    assert(e.first_sector == 0 && e.sector_count == 10 && e.first_reference_sectors == 2 &&
           e.duration_units == 100 && e.start.ticks == 0 && e.duration.ticks == 37537);
    assert(hddvd_tmap_get_entry(tmap, 1, &e) == HDDVD_OK);
    assert(e.first_sector == 10 && e.start.ticks == 37537 && e.duration.ticks == 30030);
    assert(hddvd_tmap_get_entry(tmap, 2, &e) == HDDVD_OK);
    assert(e.first_sector == 30 && e.sector_count == 7 && e.first_reference_sectors == 7 &&
           e.start.ticks == 67567 && e.duration.ticks == 1126);
    assert(hddvd_tmap_get_entry(tmap, 3, &e) == HDDVD_EINVAL);

    t.ticks = 0;
    assert(hddvd_tmap_find(tmap, t, &index) == HDDVD_OK && index == 0);
    t.ticks = 37536;
    assert(hddvd_tmap_find(tmap, t, &index) == HDDVD_OK && index == 0);
    t.ticks = 37537;
    assert(hddvd_tmap_find(tmap, t, &index) == HDDVD_OK && index == 1);
    t.ticks = 68692;
    assert(hddvd_tmap_find(tmap, t, &index) == HDDVD_OK && index == 2);
    t.ticks = 68693;
    assert(hddvd_tmap_find(tmap, t, &index) == HDDVD_ENOTFOUND);

    memset(&evob, 0, sizeof(evob));
    evob.number = 7;
    evob.sector_count = 37;
    evob.start.ticks = 1000;
    evob.end.ticks = 69693;
    assert(hddvd_tmap_check_evob(tmap, &evob) == HDDVD_OK);
    evob.end.ticks++;
    assert(hddvd_tmap_check_evob(tmap, &evob) == HDDVD_EPARSE);
    evob.end.ticks--;
    evob.sector_count++;
    assert(hddvd_tmap_check_evob(tmap, &evob) == HDDVD_EPARSE);
    evob.sector_count--;
    evob.number = 8;
    assert(hddvd_tmap_check_evob(tmap, &evob) == HDDVD_EPARSE);
    hddvd_tmap_close(tmap);
    free(b);

#define REJECT(edit, status)                                                                       \
    do                                                                                             \
    {                                                                                              \
        b = make_tmap();                                                                           \
        edit;                                                                                      \
        tmap = (hddvd_tmap *)b;                                                                    \
        assert(open_tmap(b, &tmap) == (status) && tmap == NULL);                                   \
        free(b);                                                                                   \
    } while (0)
    REJECT(b[0] = 'X', HDDVD_ENOTSUP);                         /* not a time map */
    REJECT(b[0x187] = 0, HDDVD_EPARSE);                        /* no entries */
    REJECT(put32(b + 0x180, 0x184), HDDVD_EPARSE);             /* over the counts */
    REJECT(put32(b + 0x180, 0x1a2), HDDVD_EPARSE);             /* misaligned */
    REJECT(put32(b + 0x180, 2048 - 8), HDDVD_EPARSE);          /* runs off the file */
    REJECT(b[0x186] = 2, HDDVD_EPARSE);                        /* 515 entries run off the file */
    REJECT(put32(b + 0x1a4, entry(5, 80, 0)), HDDVD_EPARSE);   /* empty EVOBU */
    REJECT(put32(b + 0x1a4, entry(5, 0, 20)), HDDVD_EPARSE);   /* no playback time */
    REJECT(put32(b + 0x1a4, entry(0, 80, 20)), HDDVD_EPARSE);  /* no reference */
    REJECT(put32(b + 0x1a4, entry(21, 80, 20)), HDDVD_EPARSE); /* reference too big */
#undef REJECT
    assert(hddvd_tmap_open(NULL, &tmap) == HDDVD_EINVAL);
    assert(hddvd_tmap_open(NULL, NULL) == HDDVD_EINVAL);
    assert(hddvd_tmap_entry_count(NULL) == 0 && hddvd_tmap_duration(NULL).ticks == 0);
    assert(hddvd_tmap_find(NULL, t, &index) == HDDVD_EINVAL);
    assert(hddvd_tmap_check_evob(NULL, &evob) == HDDVD_EINVAL);
    hddvd_tmap_close(NULL);
}

int main(void)
{
    hddvd_map_header header;
    hddvd_map_info info;
    unsigned char *b;
    char long_name[80];
    FILE *f;

    b = make_map(2, 1, "ORIGINAL.VTI");
    assert(read_header(b, 4096, &header) == HDDVD_OK);
    assert(header.last_sector == 1 && header.size == 4096 &&
           !strcmp(header.vti_name, "ORIGINAL.VTI"));
    assert(read_header(b, 2048, &header) == HDDVD_EPARSE);
    assert(read_header(b, 4095, &header) == HDDVD_EPARSE);
    assert(read_header(b, 0x80, &header) == HDDVD_EPARSE);
    f = fopen("test.map", "wb");
    assert(f && fwrite(b, 1, 4096, f) == 4096);
    fclose(f);
    assert(hddvd_map_probe("test.map", &info) == HDDVD_OK && info.structure_validated &&
           info.sector_count == 2);
    b[0] = 'X';
    f = fopen("test.map", "wb");
    assert(f && fwrite(b, 1, 4096, f) == 4096);
    fclose(f);
    assert(hddvd_map_probe("test.map", &info) == HDDVD_OK && !info.structure_validated);
    remove("test.map");
    assert(read_header(b, 4096, &header) == HDDVD_ENOTSUP);
    free(b);

    /* The VTI name must be present, printable, and fit the record. */
    b = make_map(1, 0, "");
    assert(read_header(b, 2048, &header) == HDDVD_EPARSE);
    free(b);
    b = make_map(1, 0, "BAD\tNAME.VTI");
    assert(read_header(b, 2048, &header) == HDDVD_EPARSE);
    free(b);
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    b = make_map(1, 0, long_name);
    assert(read_header(b, 2048, &header) == HDDVD_EPARSE);
    free(b);
    assert(hddvd_map_read_header(NULL, &header) == HDDVD_EINVAL);
    test_tmap();
    return 0;
}
