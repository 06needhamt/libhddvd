#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void exercise(hddvd_stream *stream)
{
    char buffer[8] = {0};
    size_t got = 99;
    assert(hddvd_stream_size(stream) == 6);
    assert(hddvd_stream_read_at(stream, 2, buffer, 3, &got) == HDDVD_OK);
    assert(got == 3 && !memcmp(buffer, "cde", 3));
    assert(hddvd_stream_read_at(stream, 5, buffer, sizeof(buffer), &got) == HDDVD_OK);
    assert(got == 1 && buffer[0] == 'f');
    assert(hddvd_stream_read_at(stream, 99, buffer, 1, &got) == HDDVD_OK && got == 0);
    assert(hddvd_stream_read_at(stream, 0, NULL, 0, &got) == HDDVD_OK && got == 0);
}

int main(void)
{
    const char bytes[] = "abcdef";
    hddvd_stream *memory, *file_stream;
    FILE *file;
    assert(hddvd_stream_open_memory(bytes, 6, &memory) == HDDVD_OK);
    exercise(memory);
    hddvd_stream_close(memory);
    file = fopen("stream-test.bin", "wb");
    assert(file && fwrite(bytes, 1, 6, file) == 6);
    fclose(file);
    assert(hddvd_stream_open_file("stream-test.bin", &file_stream) == HDDVD_OK);
    exercise(file_stream);
    hddvd_stream_close(file_stream);
    remove("stream-test.bin");
    assert(hddvd_stream_open_memory(NULL, 1, &memory) == HDDVD_EINVAL);
    assert(hddvd_stream_read_at(NULL, 0, NULL, 0, &(size_t){0}) == HDDVD_EINVAL);
    return 0;
}
