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
    return 0;
}
