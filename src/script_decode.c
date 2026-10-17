#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SCRIPT_HEADER_SIZE 32u

struct hddvd_script
{
    uint16_t version;
    char **strings;
    size_t string_count;
    hddvd_script_function *functions;
    size_t function_count;
    hddvd_script_instruction *instructions;
    size_t instruction_count;
};

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void hddvd_script_limits_default(hddvd_script_limits *limits)
{
    if (!limits)
        return;
    limits->max_strings = 4096;
    limits->max_functions = 4096;
    limits->max_instructions = 1000000;
    limits->max_bytes = 64u * 1024u * 1024u;
}

void hddvd_script_close(hddvd_script *script)
{
    size_t i;
    if (!script)
        return;
    /* string_count is set before the array is allocated, so a failed
       allocation leaves strings NULL with a nonzero count. */
    for (i = 0; script->strings && i < script->string_count; ++i)
        free(script->strings[i]);
    free(script->strings);
    free(script->functions);
    free(script->instructions);
    free(script);
}

static int range_ok(size_t size, uint32_t offset, uint32_t count, size_t width)
{
    uint64_t end = (uint64_t)offset + (uint64_t)count * width;
    return offset >= SCRIPT_HEADER_SIZE && end <= size;
}

int hddvd_script_open_with_limits(const void *data, size_t size, const hddvd_script_limits *limits,
                                  hddvd_script **out)
{
    const unsigned char *p = data;
    hddvd_script *s = NULL;
    uint32_t sc, fc, ic, so, fo, io;
    size_t i, cursor;
    int rc = HDDVD_EPARSE;
    if (!data || !limits || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    if ((uint64_t)size > limits->max_bytes)
        return HDDVD_ELIMIT;
    if (size < SCRIPT_HEADER_SIZE || memcmp(p, "HDIS", 4) != 0)
        return HDDVD_EPARSE;
    if (be16(p + 6) != SCRIPT_HEADER_SIZE)
        return HDDVD_EPARSE;
    sc = be32(p + 8);
    fc = be32(p + 12);
    ic = be32(p + 16);
    so = be32(p + 20);
    fo = be32(p + 24);
    io = be32(p + 28);
    if (sc > limits->max_strings || fc > limits->max_functions || ic > limits->max_instructions)
        return HDDVD_ELIMIT;
    if (!range_ok(size, fo, fc, 12) || !range_ok(size, io, ic, 8) || so < SCRIPT_HEADER_SIZE ||
        so > size)
        return HDDVD_EPARSE;
    s = calloc(1, sizeof(*s));
    if (!s)
        return HDDVD_ENOMEM;
    s->version = be16(p + 4);
    s->string_count = sc;
    s->function_count = fc;
    s->instruction_count = ic;
    if (sc && !(s->strings = calloc(sc, sizeof(*s->strings))))
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    if (fc && !(s->functions = calloc(fc, sizeof(*s->functions))))
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    if (ic && !(s->instructions = calloc(ic, sizeof(*s->instructions))))
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    cursor = so;
    for (i = 0; i < sc; ++i)
    {
        uint32_t n;
        if (cursor > size || size - cursor < 4)
            goto fail;
        n = be32(p + cursor);
        cursor += 4;
        if ((uint64_t)n + 1 > SIZE_MAX || n > size - cursor || memchr(p + cursor, 0, n))
            goto fail;
        s->strings[i] = malloc((size_t)n + 1);
        if (!s->strings[i])
        {
            rc = HDDVD_ENOMEM;
            goto fail;
        }
        memcpy(s->strings[i], p + cursor, n);
        s->strings[i][n] = '\0';
        cursor += n;
    }
    for (i = 0; i < fc; ++i)
    {
        const unsigned char *q = p + fo + i * 12;
        uint32_t first, count;
        s->functions[i].name_index = be32(q);
        first = be32(q + 4);
        count = be32(q + 8);
        s->functions[i].first_instruction = first;
        s->functions[i].instruction_count = count;
        if (s->functions[i].name_index >= sc || first > ic || count > ic - first)
            goto fail;
    }
    for (i = 0; i < ic; ++i)
    {
        const unsigned char *q = p + io + i * 8;
        uint32_t operand = be32(q + 4);
        if (q[2] || q[3])
            goto fail;
        int64_t signed_operand = operand;
        if (operand > INT32_MAX)
            signed_operand -= INT64_C(4294967296);
        s->instructions[i].opcode = q[0];
        s->instructions[i].flags = q[1];
        s->instructions[i].operand = (int32_t)signed_operand;
        if ((q[0] == 0x10 || q[0] == 0x11) && operand >= ic)
            goto fail;
        if (q[0] == 0x20 && operand >= sc)
            goto fail;
    }
    *out = s;
    return HDDVD_OK;
fail:
    hddvd_script_close(s);
    return rc;
}

int hddvd_script_open_with_config(const void *data, size_t size, const hddvd_config_limits *config,
                                  hddvd_script **out)
{
    hddvd_script_limits limits;
    if (!hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    limits.max_strings = config->max_script_strings;
    limits.max_functions = config->max_script_functions;
    limits.max_instructions = config->max_script_instructions;
    limits.max_bytes = config->max_script_bytes;
    return hddvd_script_open_with_limits(data, size, &limits, out);
}

int hddvd_script_open(const void *data, size_t size, hddvd_script **out)
{
    hddvd_script_limits limits;
    hddvd_script_limits_default(&limits);
    return hddvd_script_open_with_limits(data, size, &limits, out);
}

uint16_t hddvd_script_version(const hddvd_script *s)
{
    return s ? s->version : 0;
}
size_t hddvd_script_string_count(const hddvd_script *s)
{
    return s ? s->string_count : 0;
}
const char *hddvd_script_string(const hddvd_script *s, size_t i)
{
    return s && i < s->string_count ? s->strings[i] : NULL;
}
size_t hddvd_script_function_count(const hddvd_script *s)
{
    return s ? s->function_count : 0;
}
int hddvd_script_get_function(const hddvd_script *s, size_t i, hddvd_script_function *out)
{
    if (!s || !out || i >= s->function_count)
        return HDDVD_EINVAL;
    *out = s->functions[i];
    return HDDVD_OK;
}
size_t hddvd_script_instruction_count(const hddvd_script *s)
{
    return s ? s->instruction_count : 0;
}
int hddvd_script_get_instruction(const hddvd_script *s, size_t i, hddvd_script_instruction *out)
{
    if (!s || !out || i >= s->instruction_count)
        return HDDVD_EINVAL;
    *out = s->instructions[i];
    return HDDVD_OK;
}
