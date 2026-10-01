#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct file_stream
{
    hddvd_stream base;
    FILE *file;
} file_stream;

typedef struct memory_stream
{
    hddvd_stream base;
    unsigned char *data;
} memory_stream;

typedef struct slice_stream
{
    hddvd_stream base;
    hddvd_stream *parent;
    uint64_t offset;
} slice_stream;

static int seek_file(FILE *file, uint64_t offset)
{
#ifdef _WIN32
    return offset > INT64_MAX || _fseeki64(file, (__int64)offset, SEEK_SET) != 0;
#else
    return offset > (uint64_t)LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0;
#endif
}

static int file_read(hddvd_stream *stream, uint64_t offset, void *buffer, size_t size,
                     size_t *read_size)
{
    file_stream *self = (file_stream *)stream;
    size_t available;

    if (offset >= stream->size)
    {
        *read_size = 0;
        return HDDVD_OK;
    }
    available = stream->size - offset > SIZE_MAX ? SIZE_MAX : (size_t)(stream->size - offset);
    if (size > available)
        size = available;
    if (seek_file(self->file, offset))
        return HDDVD_EIO;
    *read_size = fread(buffer, 1, size, self->file);
    return *read_size == size || feof(self->file) ? HDDVD_OK : HDDVD_EIO;
}

static void file_destroy(hddvd_stream *stream)
{
    file_stream *self = (file_stream *)stream;
    fclose(self->file);
    free(self);
}

int hddvd_stream_open_file(const char *path, hddvd_stream **out)
{
    file_stream *stream;
#ifdef _WIN32
    __int64 end;
#else
    long end;
#endif

    if (!path || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream)
        return HDDVD_ENOMEM;
    stream->file = fopen(path, "rb");
    if (!stream->file)
    {
        free(stream);
        return HDDVD_EIO;
    }
#ifdef _WIN32
    if (_fseeki64(stream->file, 0, SEEK_END) || (end = _ftelli64(stream->file)) < 0)
    {
#else
    if (fseek(stream->file, 0, SEEK_END) || (end = ftell(stream->file)) < 0)
    {
#endif
        file_destroy(&stream->base);
        return HDDVD_EIO;
    }
    stream->base.size = (uint64_t)end;
    stream->base.refs = 1;
    stream->base.read_at = file_read;
    stream->base.destroy = file_destroy;
    *out = &stream->base;
    return HDDVD_OK;
}

static int memory_read(hddvd_stream *stream, uint64_t offset, void *buffer, size_t size,
                       size_t *read_size)
{
    memory_stream *self = (memory_stream *)stream;
    size_t available;

    if (offset >= stream->size)
    {
        *read_size = 0;
        return HDDVD_OK;
    }
    available = (size_t)(stream->size - offset);
    if (size > available)
        size = available;
    memcpy(buffer, self->data + (size_t)offset, size);
    *read_size = size;
    return HDDVD_OK;
}

static void memory_destroy(hddvd_stream *stream)
{
    memory_stream *self = (memory_stream *)stream;
    free(self->data);
    free(self);
}

int hddvd_stream_open_memory(const void *data, size_t size, hddvd_stream **out)
{
    memory_stream *stream;

    if ((!data && size) || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream)
        return HDDVD_ENOMEM;
    stream->data = malloc(size ? size : 1);
    if (!stream->data)
    {
        free(stream);
        return HDDVD_ENOMEM;
    }
    if (size)
        memcpy(stream->data, data, size);
    stream->base.size = size;
    stream->base.refs = 1;
    stream->base.read_at = memory_read;
    stream->base.destroy = memory_destroy;
    *out = &stream->base;
    return HDDVD_OK;
}

static int slice_read(hddvd_stream *stream, uint64_t offset, void *buffer, size_t size,
                      size_t *read_size)
{
    slice_stream *self = (slice_stream *)stream;
    size_t available;

    if (offset >= stream->size)
    {
        *read_size = 0;
        return HDDVD_OK;
    }
    available = stream->size - offset > SIZE_MAX ? SIZE_MAX : (size_t)(stream->size - offset);
    if (size > available)
        size = available;
    return hddvd_stream_read_at(self->parent, self->offset + offset, buffer, size, read_size);
}

static void slice_destroy(hddvd_stream *stream)
{
    slice_stream *self = (slice_stream *)stream;
    hddvd_stream_close(self->parent);
    free(self);
}

int hddvd_stream_slice(hddvd_stream *parent, uint64_t offset, uint64_t size, hddvd_stream **out)
{
    slice_stream *stream;

    if (!parent || !out || offset > parent->size || size > parent->size - offset)
        return HDDVD_EINVAL;
    *out = NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream)
        return HDDVD_ENOMEM;
    stream->base.size = size;
    stream->base.refs = 1;
    stream->base.read_at = slice_read;
    stream->base.destroy = slice_destroy;
    stream->parent = parent;
    hddvd_stream_retain(parent);
    stream->offset = offset;
    *out = &stream->base;
    return HDDVD_OK;
}

uint64_t hddvd_stream_size(const hddvd_stream *stream)
{
    return stream ? stream->size : 0;
}

int hddvd_stream_read_at(hddvd_stream *stream, uint64_t offset, void *buffer, size_t size,
                         size_t *read_size)
{
    if (!stream || (!buffer && size) || !read_size)
        return HDDVD_EINVAL;
    *read_size = 0;
    if (!size)
        return HDDVD_OK;
    return stream->read_at(stream, offset, buffer, size, read_size);
}

void hddvd_stream_close(hddvd_stream *stream)
{
    if (stream && --stream->refs == 0)
        stream->destroy(stream);
}

void hddvd_stream_retain(hddvd_stream *stream)
{
    if (stream)
        stream->refs++;
}
