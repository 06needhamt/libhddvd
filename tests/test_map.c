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
    return 0;
}
