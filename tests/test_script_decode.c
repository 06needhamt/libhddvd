#include "hddvd.h"
#include <stdio.h>
#include <string.h>

static const unsigned char valid[] = {
    'H','D','I','S', 0,1, 0,32,
    0,0,0,1, 0,0,0,1, 0,0,0,2,
    0,0,0,32, 0,0,0,40, 0,0,0,52,
    0,0,0,4, 'm','a','i','n',
    0,0,0,0, 0,0,0,0, 0,0,0,2,
    0x20,0,0,0, 0,0,0,0,
    0x10,0,0,0, 0,0,0,0
};

int main(void)
{
    hddvd_script *s = NULL;
    hddvd_script_function fn;
    hddvd_script_instruction ins;
    unsigned char bad[sizeof(valid)];
    hddvd_script_limits limits;
    int ok = hddvd_script_open(valid, sizeof(valid), &s) == HDDVD_OK;
    ok = ok && hddvd_script_version(s) == 1 &&
         hddvd_script_string_count(s) == 1 &&
         !strcmp(hddvd_script_string(s, 0), "main") &&
         hddvd_script_function_count(s) == 1 &&
         hddvd_script_instruction_count(s) == 2;
    ok = ok && hddvd_script_get_function(s, 0, &fn) == HDDVD_OK &&
         fn.name_index == 0 && fn.instruction_count == 2;
    ok = ok && hddvd_script_get_instruction(s, 1, &ins) == HDDVD_OK &&
         ins.opcode == 0x10 && ins.operand == 0;
    hddvd_script_close(s);
    memcpy(bad, valid, sizeof(bad)); bad[67] = 2;
    ok = ok && hddvd_script_open(bad, sizeof(bad), &s) == HDDVD_EPARSE;
    hddvd_script_limits_default(&limits); limits.max_instructions = 1;
    ok = ok && hddvd_script_open_with_limits(valid, sizeof(valid), &limits, &s) == HDDVD_ELIMIT;
    ok = ok && hddvd_script_open(valid, 31, &s) == HDDVD_EPARSE;
    if (!ok) fprintf(stderr, "script decoder test failed\n");
    return ok ? 0 : 1;
}
