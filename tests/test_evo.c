#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const char *p = "test.evo";
    FILE *f = fopen(p, "wb");
    unsigned char pes[] = {0, 0, 1, 0xe0, 0, 3, 0x80, 0, 0, 0, 0, 1, 0xbd, 0, 4, 0x80, 0, 0, 0xc0};
    hddvd_stream_info s[8];
    size_t n = 0, i, split;
    int video = 0, audio = 0;
    if (!f)
        return 1;
    fwrite(pes, 1, sizeof(pes), f);
    fclose(f);
    if (hddvd_evo_probe(p, s, 8, &n) != HDDVD_OK)
        return 2;
    remove(p);
    for (i = 0; i < n; i++)
    {
        if (s[i].codec == HDDVD_CODEC_MPEG2_VIDEO)
            video = 1;
        if (s[i].codec == HDDVD_CODEC_AC3_EAC3)
            audio = 1;
    }
    assert(video && audio);
    for (split = 0; split <= sizeof(pes); split++)
    {
        hddvd_evo_parser *q;
        assert(hddvd_evo_parser_create(&q) == 0);
        assert(hddvd_evo_parser_push(q, pes, split) == 0);
        assert(hddvd_evo_parser_push(q, pes + split, sizeof(pes) - split) == 0);
        assert(hddvd_evo_parser_finish(q) == 0);
        assert(hddvd_evo_parser_stream_count(q) == 2);
        hddvd_evo_parser_destroy(q);
    }
    {
        /* Extended stream id (0xfd) packets name their stream in the PES
           header extension, not the payload: VC-1 0x55 after a PTS, the
           same id in a packet whose header length is too short to hold it,
           and one without an extension. */
        static const unsigned char vc1[] = {
            0,    0,    1,    0xfd, 0, 13, 0x81, 0x81, 8,  0x21, 0,    1, 0,    1,   1, 0x81,
            0x55, 0x0f, 0x12, 0,    0, 1,  0xfd, 0,    11, 0x81, 0x81, 7, 0x21, 0,   1, 0,
            1,    1,    0x81, 0x55, 0, 0,  1,    0xfd, 0,  4,    0x81, 0, 0,    0x55};
        hddvd_stream_info info;
        hddvd_evo_parser *q;
        assert(hddvd_evo_parser_create(&q) == 0);
        assert(hddvd_evo_parser_push(q, vc1, sizeof(vc1)) == 0);
        assert(hddvd_evo_parser_finish(q) == 0);
        assert(hddvd_evo_parser_stream_count(q) == 2);
        assert(hddvd_evo_parser_get_stream(q, 0, &info) == 0);
        assert(info.stream_id == 0xfd && info.substream_id == 0x55 &&
               info.codec == HDDVD_CODEC_VC1 && info.packet_count == 1);
        assert(hddvd_evo_parser_get_stream(q, 1, &info) == 0);
        assert(info.substream_id == 0 && info.codec == HDDVD_CODEC_UNKNOWN &&
               info.packet_count == 2);
        hddvd_evo_parser_destroy(q);
    }
    return 0;
}
