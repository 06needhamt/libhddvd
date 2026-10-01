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
    if (!f) {
        return 1;
    }
    fwrite(pes, 1, sizeof(pes), f);
    fclose(f);
    if (hddvd_evo_probe(p, s, 8, &n) != HDDVD_OK) {
        return 2;
    }
    remove(p);
    for (i = 0; i < n; i++)
    {
        if (s[i].codec == HDDVD_CODEC_MPEG2_VIDEO) {
            video = 1;
        }
        if (s[i].codec == HDDVD_CODEC_AC3_EAC3) {
            audio = 1;
        }
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
    return 0;
}
