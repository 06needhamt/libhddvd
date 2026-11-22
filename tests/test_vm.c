/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <string.h>

/* Commands are written out as bytes, in the forms docs/formats/ifo-map.md
   describes. */
#define CMD(a, b, c, d, e, f, g, h) {a, b, c, d, e, f, g, h}

static uint16_t get(const hddvd_vm *vm, unsigned r)
{
    uint16_t value = 0xdead;
    assert(hddvd_vm_get_register(vm, r, &value) == HDDVD_OK);
    return value;
}

static void run(hddvd_vm *vm, const uint8_t (*c)[8], size_t count, int status, hddvd_vm_result *out)
{
    assert(hddvd_vm_run(vm, &c[0][0], count, 0, out) == status);
}

static void test_registers(void)
{
    hddvd_vm *vm;
    uint16_t value;
    assert(hddvd_vm_create(0, NULL) == HDDVD_EINVAL);
    assert(hddvd_vm_create(7, &vm) == HDDVD_OK);
    assert(get(vm, 0) == 0 && get(vm, 63) == 0 && get(vm, 0x80) == 0 && get(vm, 0x9f) == 0);
    assert(hddvd_vm_set_register(vm, 63, 5) == HDDVD_OK && get(vm, 63) == 5);
    assert(hddvd_vm_set_register(vm, 0x88, 2048) == HDDVD_OK && get(vm, 0x88) == 2048);
    assert(hddvd_vm_get_register(vm, 64, &value) == HDDVD_EINVAL);
    assert(hddvd_vm_get_register(vm, 0xa0, &value) == HDDVD_EINVAL);
    assert(hddvd_vm_set_register(vm, 0x40, 1) == HDDVD_EINVAL);
    assert(hddvd_vm_get_register(NULL, 0, &value) == HDDVD_EINVAL);
    assert(hddvd_vm_get_register(vm, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_vm_set_register(NULL, 0, 0) == HDDVD_EINVAL);
    hddvd_vm_destroy(vm);
    hddvd_vm_destroy(NULL);
}

static void test_arithmetic(void)
{
    static const uint8_t c[][8] = {
        CMD(0x71, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00), /* GPRM0 = 5 */
        CMD(0x73, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00), /* GPRM0 += 3 */
        CMD(0x71, 0x00, 0x00, 0x01, 0xff, 0xf0, 0x00, 0x00), /* GPRM1 = 0xfff0 */
        CMD(0x73, 0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00), /* GPRM1 += 32: saturates */
        CMD(0x71, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00), /* GPRM2 = 2 */
        CMD(0x74, 0x00, 0x00, 0x02, 0x00, 0x09, 0x00, 0x00), /* GPRM2 -= 9: floors */
        CMD(0x71, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00), /* GPRM3 = 256 */
        CMD(0x75, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00), /* GPRM3 *= 256: saturates */
        CMD(0x71, 0x00, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00), /* GPRM4 = 17 */
        CMD(0x76, 0x00, 0x00, 0x04, 0x00, 0x05, 0x00, 0x00), /* GPRM4 /= 5 */
        CMD(0x71, 0x00, 0x00, 0x05, 0x00, 0x11, 0x00, 0x00), /* GPRM5 = 17 */
        CMD(0x77, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00), /* GPRM5 %= 5 */
        CMD(0x76, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00), /* GPRM6 /= 0 */
        CMD(0x77, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00), /* GPRM7 %= 0 */
        CMD(0x71, 0x00, 0x00, 0x08, 0x00, 0xf0, 0x00, 0x00), /* GPRM8 = 0xf0 */
        CMD(0x79, 0x00, 0x00, 0x08, 0x00, 0x3c, 0x00, 0x00), /* GPRM8 &= 0x3c */
        CMD(0x7a, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00), /* GPRM8 |= 0x100 */
        CMD(0x7b, 0x00, 0x00, 0x08, 0x00, 0x30, 0x00, 0x00), /* GPRM8 ^= 0x30 */
        CMD(0x61, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00), /* GPRM9 = GPRM0 */
        CMD(0x62, 0x00, 0x00, 0x09, 0x00, 0x04, 0x00, 0x00), /* GPRM9 <-> GPRM4 */
        CMD(0x61, 0x00, 0x00, 0x0a, 0x00, 0x88, 0x00, 0x00), /* GPRM10 = SPRM8 */
    };
    hddvd_vm *vm;
    hddvd_vm_result r;
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    assert(hddvd_vm_set_register(vm, 0x88, 3072) == HDDVD_OK);
    run(vm, c, sizeof(c) / sizeof(c[0]), HDDVD_OK, &r);
    assert(r.action == HDDVD_VM_END && r.line == 0 && r.steps == sizeof(c) / sizeof(c[0]));
    assert(get(vm, 0) == 8 && get(vm, 1) == 0xffff && get(vm, 2) == 0 && get(vm, 3) == 0xffff);
    assert(get(vm, 4) == 8 && get(vm, 5) == 2 && get(vm, 6) == 0xffff && get(vm, 7) == 0xffff);
    assert(get(vm, 8) == 0x100 && get(vm, 9) == 3 && get(vm, 10) == 3072);
    hddvd_vm_destroy(vm);
}

static void test_random(void)
{
    static const uint8_t c[][8] = {
        CMD(0x78, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00), /* GPRM0 rnd 6 */
        CMD(0x78, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00), /* GPRM1 rnd 0 */
    };
    hddvd_vm *a, *b;
    hddvd_vm_result r;
    int i;
    assert(hddvd_vm_create(42, &a) == HDDVD_OK && hddvd_vm_create(42, &b) == HDDVD_OK);
    for (i = 0; i < 100; i++)
    {
        run(a, c, 2, HDDVD_OK, &r);
        run(b, c, 2, HDDVD_OK, &r);
        assert(get(a, 0) >= 1 && get(a, 0) <= 6 && get(a, 0) == get(b, 0));
        assert(get(a, 1) == 1);
    }
    hddvd_vm_destroy(a);
    hddvd_vm_destroy(b);
}

static void test_control(void)
{
    /* GPRM0 counts to 3 in a loop, then links to PGC 2. */
    static const uint8_t loop[][8] = {
        CMD(0x73, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00), /* GPRM0 += 1 */
        CMD(0x00, 0xf1, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01), /* if (GPRM0 < 3) Goto 1 */
        CMD(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* Nop */
        CMD(0x20, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02), /* LinkPGCN 2 */
        CMD(0x20, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05), /* not reached */
    };
    static const uint8_t brk[][8] = {
        CMD(0x00, 0xa2, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00), /* if (GPRM0 == 1) Break */
        CMD(0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* Break */
    };
    static const uint8_t bad_goto[][8] = {
        CMD(0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03), /* Goto 3 of 1 */
    };
    static const uint8_t forever[][8] = {
        CMD(0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01), /* Goto 1 */
    };
    hddvd_vm *vm;
    hddvd_vm_result r;
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    run(vm, loop, 5, HDDVD_OK, &r);
    assert(r.action == HDDVD_VM_LINK && r.link == HDDVD_VM_LINK_PGC && r.target == 2);
    assert(r.line == 4 && r.steps == 8 && r.button == 0 && get(vm, 0) == 3);
    assert(!memcmp(r.command, loop[3], 8));
    /* Starting part-way. */
    assert(hddvd_vm_run(vm, &loop[0][0], 5, 3, &r) == HDDVD_OK && r.line == 4 && r.steps == 1);
    assert(hddvd_vm_run(vm, &loop[0][0], 5, 5, &r) == HDDVD_OK && r.line == 0 && r.steps == 0);
    assert(hddvd_vm_run(vm, &loop[0][0], 5, 6, &r) == HDDVD_EINVAL);
    assert(hddvd_vm_run(vm, NULL, 0, 0, &r) == HDDVD_OK && r.action == HDDVD_VM_END);
    assert(hddvd_vm_run(vm, NULL, 1, 0, &r) == HDDVD_EINVAL);
    assert(hddvd_vm_run(NULL, &loop[0][0], 5, 0, &r) == HDDVD_EINVAL);
    assert(hddvd_vm_run(vm, &loop[0][0], 5, 0, NULL) == HDDVD_EINVAL);
    /* A false condition passes over Break; a true one stops. */
    run(vm, brk, 2, HDDVD_OK, &r);
    assert(r.action == HDDVD_VM_END && r.line == 2 && r.steps == 2);
    assert(hddvd_vm_set_register(vm, 0, 1) == HDDVD_OK);
    run(vm, brk, 2, HDDVD_OK, &r);
    assert(r.action == HDDVD_VM_END && r.line == 1);
    run(vm, bad_goto, 1, HDDVD_EPARSE, &r);
    assert(r.line == 1);
    run(vm, forever, 1, HDDVD_ELIMIT, &r);
    assert(r.steps == 65536);
    hddvd_vm_destroy(vm);
}

static void test_links(void)
{
    static const uint8_t c[][8] = {
        CMD(0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x01), /* LinkSIns 1, button 3 */
        CMD(0x20, 0x05, 0x00, 0x00, 0x00, 0x00, 0x08, 0x02), /* LinkPTTN 2, button 2 */
        CMD(0x20, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83), /* LinkPGN 3 */
        CMD(0x20, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04), /* LinkCN 4 */
        CMD(0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* subset 2: not run */
        /* if (GPRM1 >= 2) LinkPGCN 9 */
        CMD(0x20, 0xc4, 0x00, 0x01, 0x00, 0x02, 0x00, 0x09),
    };
    hddvd_vm *vm;
    hddvd_vm_result r;
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 0, &r) == HDDVD_OK);
    assert(r.link == HDDVD_VM_LINK_INSTRUCTION && r.target == 1 && r.button == 3);
    assert(get(vm, 0x88) == 3072);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 1, &r) == HDDVD_OK);
    assert(r.link == HDDVD_VM_LINK_CHAPTER && r.target == 2 && r.button == 2);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 2, &r) == HDDVD_OK);
    assert(r.link == HDDVD_VM_LINK_PROGRAM && r.target == 3 && r.button == 0);
    assert(get(vm, 0x88) == 2048);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 3, &r) == HDDVD_OK);
    assert(r.link == HDDVD_VM_LINK_CELL && r.target == 4);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 4, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_UNSUPPORTED && r.line == 5 && r.steps == 1);
    /* The condition is false at first, so the run ends; then true. */
    assert(hddvd_vm_run(vm, &c[0][0], 6, 5, &r) == HDDVD_OK && r.action == HDDVD_VM_END);
    assert(hddvd_vm_set_register(vm, 1, 2) == HDDVD_OK);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 5, &r) == HDDVD_OK && r.target == 9);
    hddvd_vm_destroy(vm);
}

static void test_jumps(void)
{
    static const uint8_t c[][8] = {
        CMD(0x30, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* Exit */
        CMD(0x30, 0x02, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00), /* JumpTT 25 */
        CMD(0x30, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00), /* JumpVTS_TT 2 */
        CMD(0x30, 0x05, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00), /* JumpVTS_PTT 1:7 */
        CMD(0x30, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* JumpSS first play */
        CMD(0x30, 0x06, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00), /* JumpSS VMG menu 2 */
        CMD(0x30, 0x06, 0x00, 0x00, 0x00, 0xa3, 0x00, 0x00), /* JumpSS VTS menu 3 */
        CMD(0x30, 0x06, 0x00, 0x09, 0x00, 0xb0, 0x00, 0x00), /* JumpSS VMG PGC 9 */
        CMD(0x30, 0x08, 0x00, 0x04, 0x02, 0xb0, 0x00, 0x00), /* CallSS VMG PGC 4, cell 2 */
        CMD(0x30, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* subset 7: not run */
        /* if (GPRM2 == GPRM3) JumpTT 1 */
        CMD(0x30, 0x22, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03),
    };
    hddvd_vm *vm;
    hddvd_vm_result r;
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 0, &r) == HDDVD_OK && r.action == HDDVD_VM_EXIT);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 1, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_JUMP_TITLE && r.target == 25);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 2, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_JUMP_VTS_TITLE && r.target == 2);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 3, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_JUMP_VTS_CHAPTER && r.target == 1 && r.chapter == 7);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 4, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_JUMP_SYSTEM && r.domain == HDDVD_VM_FIRST_PLAY && r.target == 0);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 5, &r) == HDDVD_OK);
    assert(r.domain == HDDVD_VM_VMG_MENU && r.target == 2);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 6, &r) == HDDVD_OK);
    assert(r.domain == HDDVD_VM_VTS_MENU && r.target == 3);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 7, &r) == HDDVD_OK);
    assert(r.domain == HDDVD_VM_VMG_PGC && r.target == 9);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 8, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_CALL_SYSTEM && r.domain == HDDVD_VM_VMG_PGC && r.target == 4);
    assert(r.resume_cell == 2);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 9, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_UNSUPPORTED);
    /* GPRM2 == GPRM3 == 0. */
    assert(hddvd_vm_run(vm, &c[0][0], 11, 10, &r) == HDDVD_OK && r.target == 1);
    assert(hddvd_vm_set_register(vm, 3, 1) == HDDVD_OK);
    assert(hddvd_vm_run(vm, &c[0][0], 11, 10, &r) == HDDVD_OK && r.action == HDDVD_VM_END);
    hddvd_vm_destroy(vm);
}

static void test_system_and_conditional_sets(void)
{
    static const uint8_t c[][8] = {
        CMD(0x56, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00), /* SetHL_BTNN 2 */
        CMD(0x51, 0x00, 0x00, 0x82, 0xc1, 0x03, 0x00, 0x00), /* SetSTN audio 2, sp 0x41 */
        CMD(0x41, 0x00, 0x00, 0x85, 0x00, 0x00, 0x00, 0x00), /* SetSTN audio GPRM5 */
        /* if (GPRM21 >= 39) GPRM21 -= 39 */
        CMD(0x74, 0xc0, 0x15, 0x15, 0x00, 0x27, 0x00, 0x27),
        /* SetHL_BTNN 4; LinkPGCN 3 */
        CMD(0x56, 0x04, 0x00, 0x00, 0x10, 0x00, 0x00, 0x03),
        CMD(0x59, 0x00, 0x00, 0x00, 0xc1, 0x00, 0x00, 0x00), /* sub-op 9: not run */
    };
    hddvd_vm *vm;
    hddvd_vm_result r;
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    assert(hddvd_vm_set_register(vm, 5, 6) == HDDVD_OK);
    assert(hddvd_vm_set_register(vm, 21, 45) == HDDVD_OK);
    assert(hddvd_vm_run(vm, &c[0][0], 4, 0, &r) == HDDVD_OK && r.action == HDDVD_VM_END);
    assert(get(vm, 0x88) == 2048 && get(vm, 0x81) == 6 && get(vm, 0x82) == 0x41);
    assert(get(vm, 0x83) == 0 && get(vm, 21) == 6);
    /* 6 < 39: no change. */
    assert(hddvd_vm_run(vm, &c[0][0], 4, 3, &r) == HDDVD_OK && get(vm, 21) == 6);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 4, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_LINK && r.target == 3 && r.button == 4 && get(vm, 0x88) == 4096);
    assert(hddvd_vm_run(vm, &c[0][0], 6, 5, &r) == HDDVD_OK);
    assert(r.action == HDDVD_VM_UNSUPPORTED && r.line == 6);
    hddvd_vm_destroy(vm);
}

static void test_bad_registers(void)
{
    static const uint8_t c[][8] = {
        CMD(0x71, 0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00), /* GPRM64 = 1 */
        CMD(0x61, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00), /* GPRM0 = GPRM80 */
        CMD(0x00, 0xa1, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x01), /* if (0xc0 == 0) Goto 1 */
        CMD(0x71, 0x00, 0x00, 0x80, 0x00, 0x01, 0x00, 0x00), /* SPRM0 = 1 */
        CMD(0x72, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00), /* swap with data */
    };
    hddvd_vm *vm;
    hddvd_vm_result r;
    size_t i;
    assert(hddvd_vm_create(1, &vm) == HDDVD_OK);
    for (i = 0; i < 4; i++)
    {
        assert(hddvd_vm_run(vm, &c[0][0], 5, i, &r) == HDDVD_EPARSE);
        assert(r.line == i + 1 && r.steps == 1);
    }
    assert(hddvd_vm_run(vm, &c[0][0], 5, 4, &r) == HDDVD_OK && r.action == HDDVD_VM_UNSUPPORTED);
    assert(get(vm, 0) == 0);
    hddvd_vm_destroy(vm);
}

int main(void)
{
    test_registers();
    test_arithmetic();
    test_random();
    test_control();
    test_links();
    test_jumps();
    test_system_and_conditional_sets();
    test_bad_registers();
    return 0;
}
