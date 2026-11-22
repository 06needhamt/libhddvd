/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Navigation command VM. Encoding notes and evidence: docs/formats/ifo-map.md.
   Arithmetic and register forms follow libdvdnav's reading of DVD-Video. */
#define GPRM_COUNT 64u
#define SPRM_COUNT 32u
#define SPRM_AUDIO 1u
#define SPRM_HIGHLIGHT 8u
#define MAX_STEPS 65536u

struct hddvd_vm
{
    uint16_t gprm[GPRM_COUNT];
    uint16_t sprm[SPRM_COUNT];
    uint32_t seed;
};

int hddvd_vm_create(uint32_t seed, hddvd_vm **out)
{
    hddvd_vm *vm;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    vm = calloc(1, sizeof(*vm));
    if (!vm)
        return HDDVD_ENOMEM;
    vm->seed = seed ? seed : 0x2545f491u;
    *out = vm;
    return HDDVD_OK;
}

void hddvd_vm_destroy(hddvd_vm *vm)
{
    free(vm);
}

/* Register byte: bit 7 names an SPRM (low 5 bits), else a GPRM. */
static uint16_t *reg_ptr(hddvd_vm *vm, unsigned r)
{
    if (r & 0x80u)
        return (r & 0x60u) ? NULL : &vm->sprm[r & 0x1fu];
    return r < GPRM_COUNT ? &vm->gprm[r] : NULL;
}

int hddvd_vm_get_register(const hddvd_vm *vm, unsigned r, uint16_t *value)
{
    const uint16_t *p;
    if (!vm || !value)
        return HDDVD_EINVAL;
    p = reg_ptr((hddvd_vm *)vm, r);
    if (!p)
        return HDDVD_EINVAL;
    *value = *p;
    return HDDVD_OK;
}

int hddvd_vm_set_register(hddvd_vm *vm, unsigned r, uint16_t value)
{
    uint16_t *p;
    if (!vm)
        return HDDVD_EINVAL;
    p = reg_ptr(vm, r);
    if (!p)
        return HDDVD_EINVAL;
    *p = value;
    return HDDVD_OK;
}

static uint16_t be16(const uint8_t *b)
{
    return (uint16_t)(b[0] << 8 | b[1]);
}

static uint32_t next_random(hddvd_vm *vm)
{
    uint32_t x = vm->seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    vm->seed = x;
    return x;
}

/* Reads a register, or 16 bits of data at `at` when immediate; a register
   operand is the byte after `at`. */
static int operand(hddvd_vm *vm, const uint8_t *c, int immediate, size_t at, uint16_t *value)
{
    const uint16_t *p;
    if (immediate)
    {
        *value = be16(c + at);
        return HDDVD_OK;
    }
    p = reg_ptr(vm, c[at + 1]);
    if (!p)
        return HDDVD_EPARSE;
    *value = *p;
    return HDDVD_OK;
}

/* Compare op (1 to 7) on the register at reg_at and the operand at
   operand_at; op 0 is always true. */
static int condition(hddvd_vm *vm, const uint8_t *c, unsigned op, int immediate, size_t reg_at,
                     size_t operand_at, int *result)
{
    const uint16_t *p;
    uint16_t a, b;
    int status;
    *result = 1;
    if (!op)
        return HDDVD_OK;
    p = reg_ptr(vm, c[reg_at]);
    if (!p)
        return HDDVD_EPARSE;
    a = *p;
    status = operand(vm, c, immediate, operand_at, &b);
    if (status)
        return status;
    switch (op)
    {
        case 1:
            *result = (a & b) != 0;
            break;
        case 2:
            *result = a == b;
            break;
        case 3:
            *result = a != b;
            break;
        case 4:
            *result = a >= b;
            break;
        case 5:
            *result = a > b;
            break;
        case 6:
            *result = a <= b;
            break;
        default:
            *result = a < b;
            break;
    }
    return HDDVD_OK;
}

/* The link part of type 1 to 3 commands: subset in byte 1's low nibble,
   target in bytes 6 and 7. Returns 0 for a subset the VM does not run. */
static int link_to(hddvd_vm *vm, const uint8_t *c, hddvd_vm_result *out)
{
    unsigned subset = c[1] & 0x0fu;
    uint16_t button = 0;
    switch (subset)
    {
        case 1:
            out->target = c[7] & 0x1fu;
            button = (uint16_t)(c[6] >> 2);
            break;
        case 4:
            out->target = be16(c + 6) & 0x7fffu;
            break;
        case 5:
            out->target = be16(c + 6) & 0x3ffu;
            button = (uint16_t)(c[6] >> 2);
            break;
        case 6:
            out->target = c[7] & 0x7fu;
            button = (uint16_t)(c[6] >> 2);
            break;
        case 7:
            out->target = c[7];
            button = (uint16_t)(c[6] >> 2);
            break;
        default:
            return 0;
    }
    out->action = HDDVD_VM_LINK;
    out->link = (hddvd_vm_link)subset;
    if (button)
    {
        out->button = button;
        vm->sprm[SPRM_HIGHLIGHT] = (uint16_t)(button << 10);
    }
    return 1;
}

static int jump(const uint8_t *c, hddvd_vm_result *out)
{
    switch (c[1] & 0x0fu)
    {
        case 1:
            out->action = HDDVD_VM_EXIT;
            return 1;
        case 2:
            out->action = HDDVD_VM_JUMP_TITLE;
            out->target = c[5] & 0x7fu;
            return 1;
        case 3:
            out->action = HDDVD_VM_JUMP_VTS_TITLE;
            out->target = c[5] & 0x7fu;
            return 1;
        case 5:
            out->action = HDDVD_VM_JUMP_VTS_CHAPTER;
            out->target = c[5] & 0x7fu;
            out->chapter = be16(c + 2) & 0x3ffu;
            return 1;
        case 6:
        case 8:
            out->action = (c[1] & 0x0fu) == 6 ? HDDVD_VM_JUMP_SYSTEM : HDDVD_VM_CALL_SYSTEM;
            out->domain = (hddvd_vm_domain)((c[5] >> 4) & 3u);
            if (out->domain == HDDVD_VM_VMG_PGC)
                out->target = be16(c + 2) & 0x7fffu;
            else if (out->domain != HDDVD_VM_FIRST_PLAY)
                out->target = c[5] & 0x0fu;
            if (out->action == HDDVD_VM_CALL_SYSTEM)
                out->resume_cell = c[4];
            return 1;
        default:
            return 0;
    }
}

/* Type 3: GPRM[byte 3] op= data (bytes 4 and 5) or register (byte 5). */
static int set_gprm(hddvd_vm *vm, const uint8_t *c)
{
    unsigned op = c[0] & 0x0fu;
    int immediate = (c[0] & 0x10u) != 0;
    uint16_t *dest = c[3] < GPRM_COUNT ? &vm->gprm[c[3]] : NULL;
    uint16_t value;
    uint32_t a, b;
    int status;
    if (!dest)
        return HDDVD_EPARSE;
    status = operand(vm, c, immediate, 4, &value);
    if (status)
        return status;
    a = *dest;
    b = value;
    switch (op)
    {
        case 1:
            *dest = value;
            break;
        case 2:
        {
            uint16_t *src = reg_ptr(vm, c[5]);
            *dest = *src;
            *src = (uint16_t)a;
            break;
        }
        case 3:
            *dest = (uint16_t)(a + b > 0xffffu ? 0xffffu : a + b);
            break;
        case 4:
            *dest = (uint16_t)(a > b ? a - b : 0);
            break;
        case 5:
            *dest = (uint16_t)(a * b > 0xffffu ? 0xffffu : a * b);
            break;
        case 6:
            *dest = (uint16_t)(b ? a / b : 0xffffu);
            break;
        case 7:
            *dest = (uint16_t)(b ? a % b : 0xffffu);
            break;
        case 8:
            *dest = (uint16_t)(b ? 1u + next_random(vm) % b : 1u);
            break;
        case 9:
            *dest = (uint16_t)(a & b);
            break;
        case 10:
            *dest = (uint16_t)(a | b);
            break;
        default:
            *dest = (uint16_t)(a ^ b);
            break;
    }
    return HDDVD_OK;
}

/* SetSTN value: 7 bits of data, or the GPRM in the low nibble. */
static uint16_t stream_number(const hddvd_vm *vm, uint8_t b, int immediate)
{
    return immediate ? (uint16_t)(b & 0x7fu) : vm->gprm[b & 0x0fu];
}

/* Type 2: SetSTN (1) and SetHL_BTNN (6). */
static int set_system(hddvd_vm *vm, const uint8_t *c, uint16_t *button)
{
    int immediate = (c[0] & 0x10u) != 0;
    unsigned i;
    uint16_t value;
    int status;
    if ((c[0] & 0x0fu) == 1)
    {
        for (i = 0; i < 3; i++)
            if (c[3 + i] & 0x80u)
                vm->sprm[SPRM_AUDIO + i] = stream_number(vm, c[3 + i], immediate);
        return HDDVD_OK;
    }
    status = operand(vm, c, immediate, 4, &value);
    if (status)
        return status;
    vm->sprm[SPRM_HIGHLIGHT] = value;
    *button = (uint16_t)(value >> 10);
    return HDDVD_OK;
}

/* Whether the VM runs this form; checked before anything is done. */
static int supported(const uint8_t *c)
{
    unsigned type = c[0] >> 5, sub = c[0] & 0x0fu, subset = c[1] & 0x0fu;
    switch (type)
    {
        case 0:
            return !(c[0] & 0x1fu) && subset <= 2;
        case 1:
            if (c[0] & 0x10u)
                return subset == 1 || subset == 2 || subset == 3 || subset == 5 || subset == 6 ||
                       subset == 8;
            return subset == 1 || (subset >= 4 && subset <= 7);
        case 2:
            return (sub == 1 || sub == 6) &&
                   (subset == 0 || subset == 1 || (subset >= 4 && subset <= 7));
        case 3:
            return sub >= 1 && sub <= 11 && !(sub == 2 && (c[0] & 0x10u)) &&
                   (subset == 0 || subset == 1 || (subset >= 4 && subset <= 7));
        default:
            return 0;
    }
}

/* Runs one command: 1 when it ended the run, 0 to go on (*next set). */
static int step(hddvd_vm *vm, const uint8_t *c, size_t count, size_t *next, hddvd_vm_result *out,
                int *status)
{
    unsigned type = c[0] >> 5, op = (c[1] >> 4) & 7u;
    int immediate = c[1] >> 7, taken;
    uint16_t button = 0;
    if (!supported(c))
    {
        out->action = HDDVD_VM_UNSUPPORTED;
        return 1;
    }
    switch (type)
    {
        case 0:
            *status = condition(vm, c, op, immediate, 3, 4, &taken);
            if (*status || !taken)
                return *status != 0;
            if ((c[1] & 0x0fu) == 2)
                return 1;
            if ((c[1] & 0x0fu) == 1)
            {
                if (c[7] < 1 || c[7] > count)
                {
                    *status = HDDVD_EPARSE;
                    return 1;
                }
                *next = (size_t)c[7] - 1;
            }
            return 0;
        case 1:
            if (c[0] & 0x10u)
            {
                *status = condition(vm, c, op, 0, 6, 6, &taken);
                return *status || (taken && jump(c, out));
            }
            *status = condition(vm, c, op, immediate, 3, 4, &taken);
            return *status || (taken && link_to(vm, c, out));
        case 2:
            *status = condition(vm, c, op, 0, 6, 6, &taken);
            if (*status || !taken)
                return *status != 0;
            *status = set_system(vm, c, &button);
            if (*status)
                return 1;
            if (!(c[1] & 0x0fu))
                return 0;
            link_to(vm, c, out);
            if (button && !out->button)
                out->button = button;
            return 1;
        default:
            *status = condition(vm, c, op, immediate, 2, 6, &taken);
            if (*status || !taken)
                return *status != 0;
            *status = set_gprm(vm, c);
            return *status || ((c[1] & 0x0fu) && link_to(vm, c, out));
    }
}

int hddvd_vm_run(hddvd_vm *vm, const uint8_t *commands, size_t count, size_t first,
                 hddvd_vm_result *out)
{
    size_t i = first;
    if (!vm || !out || (count && !commands))
        return HDDVD_EINVAL;
    memset(out, 0, sizeof(*out));
    if (first > count)
        return HDDVD_EINVAL;
    while (i < count)
    {
        const uint8_t *c = commands + i * 8u;
        size_t next = i + 1;
        int status = HDDVD_OK;
        if (out->steps == MAX_STEPS)
            return HDDVD_ELIMIT;
        out->steps++;
        if (step(vm, c, count, &next, out, &status))
        {
            out->line = i + 1;
            memcpy(out->command, c, 8);
            return status;
        }
        i = next;
    }
    return HDDVD_OK;
}
