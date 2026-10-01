#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *f;
    long end;
    unsigned char *bytes;
    hddvd_script *script;
    size_t i;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s SCRIPT\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f || fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: cannot read input\n", argv[0]);
        if (f)
            fclose(f);
        return 1;
    }
    bytes = malloc((size_t)end ? (size_t)end : 1);
    if (!bytes || fread(bytes, 1, (size_t)end, f) != (size_t)end)
    {
        fprintf(stderr, "%s: cannot read input\n", argv[0]);
        free(bytes);
        fclose(f);
        return 1;
    }
    fclose(f);
    if (hddvd_script_open(bytes, (size_t)end, &script) != HDDVD_OK)
    {
        fprintf(stderr, "%s: invalid or unsupported script\n", argv[0]);
        free(bytes);
        return 1;
    }
    free(bytes);
    printf("version %u\n", (unsigned)hddvd_script_version(script));
    for (i = 0; i < hddvd_script_string_count(script); ++i)
        printf("string[%zu] %s\n", i, hddvd_script_string(script, i));
    for (i = 0; i < hddvd_script_function_count(script); ++i)
    {
        hddvd_script_function fn;
        hddvd_script_get_function(script, i, &fn);
        printf("function[%zu] name=%u instructions=%u..%u\n", i, fn.name_index,
               fn.first_instruction, fn.first_instruction + fn.instruction_count);
    }
    for (i = 0; i < hddvd_script_instruction_count(script); ++i)
    {
        hddvd_script_instruction instruction;
        hddvd_script_get_instruction(script, i, &instruction);
        printf("%06zu  %02x %02x %d\n", i, instruction.opcode, instruction.flags,
               instruction.operand);
    }
    hddvd_script_close(script);
    return 0;
}
