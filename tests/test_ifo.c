#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An original IFO-shaped buffer: identifier, then the end sector at 0x1c. */
static unsigned char *make_ifo(const char *identifier, size_t sectors, uint32_t last_sector)
{
    unsigned char *b = calloc(sectors ? sectors : 1, 2048);
    assert(b);
    memcpy(b, identifier, strlen(identifier));
    b[0x1c] = (unsigned char)(last_sector >> 24);
    b[0x1d] = (unsigned char)(last_sector >> 16);
    b[0x1e] = (unsigned char)(last_sector >> 8);
    b[0x1f] = (unsigned char)last_sector;
    return b;
}

static void put32(unsigned char *b, uint32_t v)
{
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
}

/* An original VTI-shaped buffer: the EVOB table in sector 1, with a 0x140
   byte record per EVOB after the offset list. */
static unsigned char *make_vti(size_t count)
{
    unsigned char *b = make_ifo("ADVANCED-VTS", 2, 1), *t = b + 2048;
    size_t i;
    put32(b + 0xbc, 1);
    t[3] = (unsigned char)count;
    put32(t + 4, (uint32_t)(8 + 4 * count + 0x140 * count - 1));
    for (i = 0; i < count; i++)
    {
        unsigned char *r = t + 8 + 4 * count + 0x140 * i;
        put32(t + 8 + 4 * i, (uint32_t)(r - t));
        snprintf((char *)r + 2, 256, "CLIP%u.EVO", (unsigned)i);
        r[0x109] = (unsigned char)(i + 1);
        put32(r + 0x10a, 1000u * (uint32_t)(i + 1));
        put32(r + 0x10e, 0x80000000u + (uint32_t)i);
        put32(r + 0x112, 0x10000u + (uint32_t)i);
        r[0x117] = (unsigned char)(i + 1);
    }
    return b;
}

static int open_vti(const unsigned char *b, size_t size, hddvd_vti **vti)
{
    hddvd_stream *stream = NULL;
    int rc;
    assert(hddvd_stream_open_memory(b, size, &stream) == HDDVD_OK);
    rc = hddvd_vti_open(stream, vti);
    hddvd_stream_close(stream);
    return rc;
}

static void test_vti(void)
{
    unsigned char *b = make_vti(2), *t, *r;
    hddvd_vti *vti;
    hddvd_vti_evob evob;
    hddvd_ifo_header header;
    size_t index;
    hddvd_stream *stream;

    assert(hddvd_stream_open_memory(b, 4096, &stream) == HDDVD_OK);
    assert(hddvd_ifo_read_header(stream, &header) == HDDVD_OK &&
           header.kind == HDDVD_IFO_ADVANCED_VTS);
    hddvd_stream_close(stream);
    assert(open_vti(b, 4096, &vti) == HDDVD_OK && hddvd_vti_evob_count(vti) == 2);
    assert(hddvd_vti_get_evob(vti, 1, &evob) == HDDVD_OK);
    assert(evob.number == 2 && evob.attribute_number == 2 && !strcmp(evob.file_name, "CLIP1.EVO"));
    assert(evob.start.ticks == 2000 && evob.end.ticks == 0x80000001u &&
           evob.sector_count == 0x10001u);
    assert(hddvd_vti_find_evob(vti, 2, &index) == HDDVD_OK && index == 1);
    assert(hddvd_vti_find_evob(vti, 3, &index) == HDDVD_ENOTFOUND);
    assert(hddvd_vti_get_evob(vti, 2, &evob) == HDDVD_EINVAL);
    hddvd_vti_close(vti);
    free(b);

    /* An empty table is valid. */
    b = make_vti(0);
    assert(open_vti(b, 4096, &vti) == HDDVD_OK && hddvd_vti_evob_count(vti) == 0);
    hddvd_vti_close(vti);
    free(b);

    /* Other identifiers are not VTI files. */
    b = make_ifo("STANDARD-VTS", 1, 0);
    vti = (hddvd_vti *)b;
    assert(open_vti(b, 2048, &vti) == HDDVD_ENOTSUP && vti == NULL);
    free(b);

#define REJECT(edit)                                                                               \
    do                                                                                             \
    {                                                                                              \
        b = make_vti(2);                                                                           \
        t = b + 2048;                                                                              \
        r = t + 16;                                                                                \
        (void)r;                                                                                   \
        edit;                                                                                      \
        assert(open_vti(b, 4096, &vti) == HDDVD_EPARSE && vti == NULL);                            \
        free(b);                                                                                   \
    } while (0)
    REJECT(put32(b + 0xbc, 0));              /* table in the header sector */
    REJECT(put32(b + 0xbc, 2));              /* table past the end sector */
    REJECT(put32(t + 4, 2048));              /* table runs off the file */
    REJECT(put32(t + 4, 12));                /* offset list overruns the table */
    REJECT(put32(t + 8, 4));                 /* record over the offset list */
    REJECT(put32(t + 12, 2048 - 0x140 + 1)); /* record past the table */
    REJECT(memset(r + 2, 0, 256));           /* empty name */
    REJECT(memset(r + 2, 'A', 256));         /* unterminated name */
    REJECT(r[3] = '/');                      /* path separator */
    REJECT(r[3] = ' ');                      /* unprintable */
    REJECT(r[0x109] = 2);                    /* duplicate number */
    REJECT(put32(r + 0x10e, 999));           /* end before start */
    REJECT(put32(r + 0x112, 0));             /* no sectors */
#undef REJECT
    assert(hddvd_vti_open(NULL, &vti) == HDDVD_EINVAL);
    assert(hddvd_vti_open(NULL, NULL) == HDDVD_EINVAL);
    assert(hddvd_vti_evob_count(NULL) == 0);
    assert(hddvd_vti_find_evob(NULL, 1, &index) == HDDVD_EINVAL);
    hddvd_vti_close(NULL);
}

/* make_vti(2) plus an attribute table in sectors 2 and 3: record 1 has VC-1
   video and two audio streams (English, and one without a language), record
   2 MPEG-2 video and no audio. */
static unsigned char *make_vti_attributes(void)
{
    static const unsigned char english[8] = {0x1c, 0x04, 0xc4, 0, 'e', 'n', 0, 0x1f};
    static const unsigned char unnamed[8] = {0x1c, 0, 0xc4, 0, 0, 0, 0, 0};
    unsigned char *two = make_vti(2), *b = calloc(4, 2048), *t;
    assert(b);
    memcpy(b, two, 4096);
    free(two);
    put32(b + 0x1c, 3);
    put32(b + 0xb8, 2);
    t = b + 2 * 2048;
    t[1] = 2;
    put32(t + 4, 0x80f);
    put32(t + 8, 0x10);
    put32(t + 12, 0x410);
    put32(t + 0x10 + 2, 0x6b10c000u);
    t[0x10 + 0x0f] = 2;
    memcpy(t + 0x10 + 0x10, english, 8);
    memcpy(t + 0x10 + 0x18, unnamed, 8);
    put32(t + 0x410 + 2, 0x2b08c000u);
    return b;
}

static void test_vti_attributes(void)
{
    unsigned char *b = make_vti_attributes(), *t;
    hddvd_vti *vti;
    hddvd_video_attributes video;
    hddvd_stream_attributes audio;
    assert(open_vti(b, 4 * 2048, &vti) == HDDVD_OK);
    assert(hddvd_vti_attribute_count(vti) == 2);
    assert(hddvd_vti_get_video_attributes(vti, 0, &video) == HDDVD_OK);
    assert(video.coding == 1 && video.raw[0] == 0x6b && video.raw[1] == 0x10);
    assert(hddvd_vti_get_video_attributes(vti, 1, &video) == HDDVD_OK && video.coding == 0);
    assert(hddvd_vti_audio_count(vti, 0) == 2 && hddvd_vti_audio_count(vti, 1) == 0);
    assert(hddvd_vti_get_audio_attributes(vti, 0, 0, &audio) == HDDVD_OK);
    assert(!strcmp(audio.language, "en") && audio.raw[0] == 0x1c && audio.raw[7] == 0x1f);
    assert(hddvd_vti_get_audio_attributes(vti, 0, 1, &audio) == HDDVD_OK);
    assert(audio.language[0] == '\0' && audio.raw[2] == 0xc4);
    assert(hddvd_vti_get_audio_attributes(vti, 0, 2, &audio) == HDDVD_EINVAL);
    assert(hddvd_vti_get_audio_attributes(vti, 1, 0, &audio) == HDDVD_EINVAL);
    assert(hddvd_vti_get_video_attributes(vti, 2, &video) == HDDVD_EINVAL);
    assert(hddvd_vti_audio_count(vti, 2) == 0 && hddvd_vti_audio_count(NULL, 0) == 0);
    assert(hddvd_vti_attribute_count(NULL) == 0);
    assert(hddvd_vti_get_video_attributes(NULL, 0, &video) == HDDVD_EINVAL);
    assert(hddvd_vti_get_video_attributes(vti, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_vti_get_audio_attributes(vti, 0, 0, NULL) == HDDVD_EINVAL);
    hddvd_vti_close(vti);
    free(b);

#define REJECT(edit)                                                                               \
    do                                                                                             \
    {                                                                                              \
        b = make_vti_attributes();                                                                 \
        t = b + 2 * 2048;                                                                          \
        edit;                                                                                      \
        assert(open_vti(b, 4 * 2048, &vti) == HDDVD_EPARSE && vti == NULL);                        \
        free(b);                                                                                   \
    } while (0)
    REJECT(put32(b + 0xb8, 4));       /* table past the end sector */
    REJECT(put32(t + 4, 4096));       /* table runs off the file */
    REJECT(put32(t + 12, 0x411));     /* record past the table */
    REJECT(put32(t + 8, 4));          /* record over the offset list */
    REJECT(t[0x10 + 0x0e] = 1);       /* audio records past the record */
    REJECT(t[1] = 1);                 /* EVOB 2 names a missing record */
    REJECT(b[2048 + 16 + 0x117] = 0); /* EVOB 1 names no record */
#undef REJECT
}

static int read_header(const unsigned char *b, size_t size, hddvd_ifo_header *header)
{
    hddvd_stream *stream = NULL;
    int rc;
    assert(hddvd_stream_open_memory(b, size, &stream) == HDDVD_OK);
    rc = hddvd_ifo_read_header(stream, header);
    hddvd_stream_close(stream);
    return rc;
}

int main(void)
{
    hddvd_ifo_header header;
    hddvd_ifo_info info;
    unsigned char *b;
    FILE *f;

    b = make_ifo("HVDVD-VMG100", 3, 2);
    assert(read_header(b, 3 * 2048, &header) == HDDVD_OK);
    assert(header.kind == HDDVD_IFO_VMG && !strcmp(header.identifier, "HVDVD-VMG100") &&
           header.last_sector == 2 && header.size == 3 * 2048);
    /* The end sector must agree with the size, which must be whole sectors. */
    assert(read_header(b, 2 * 2048, &header) == HDDVD_EPARSE);
    assert(read_header(b, 3 * 2048 - 1, &header) == HDDVD_EPARSE);
    free(b);

    b = make_ifo("STANDARD-VTS", 1, 0);
    assert(read_header(b, 2048, &header) == HDDVD_OK && header.kind == HDDVD_IFO_STANDARD_VTS);
    assert(read_header(b, 16, &header) == HDDVD_EPARSE);
    f = fopen("test.ifo", "wb");
    assert(f && fwrite(b, 1, 2048, f) == 2048);
    fclose(f);
    assert(hddvd_ifo_probe("test.ifo", &info) == HDDVD_OK && info.recognized_identifier);
    remove("test.ifo");
    free(b);

    b = make_ifo("STANDARD-VTX", 1, 0);
    assert(read_header(b, 2048, &header) == HDDVD_ENOTSUP && header.kind == HDDVD_IFO_UNKNOWN);
    free(b);
    assert(hddvd_ifo_read_header(NULL, &header) == HDDVD_EINVAL);
    test_vti();
    test_vti_attributes();
    return 0;
}
