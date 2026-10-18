#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs one HDi application from an archive in the script runtime: evaluates
   its manifest's scripts in order, loads its first markup, calls the markup
   loaded handler, then performs the steps given and prints the focus after
   each, plus every issue and failure. --before FILE first evaluates a local
   script, for example stand-ins for host objects the runtime lacks. */

static int read_member(const hddvd_aca *aca, const char *name, void **out, size_t *size)
{
    hddvd_stream *stream = NULL;
    uint64_t length;
    size_t got = 0;
    void *bytes;
    int rc = hddvd_aca_open_member(aca, name, &stream);
    if (rc)
        return rc;
    length = hddvd_stream_size(stream);
    bytes = length <= SIZE_MAX - 1 ? malloc((size_t)length + 1) : NULL;
    if (!bytes)
        rc = HDDVD_ENOMEM;
    else
        rc = hddvd_stream_read_at(stream, 0, bytes, (size_t)length, &got);
    hddvd_stream_close(stream);
    if (!rc && got != length)
        rc = HDDVD_EIO;
    if (rc)
    {
        free(bytes);
        return rc;
    }
    *out = bytes;
    *size = (size_t)length;
    return HDDVD_OK;
}

/* A script's source: the payload of its envelope, or the member as is. */
static int read_script(const hddvd_aca *aca, const char *name, void **out, size_t *size)
{
    hddvd_object *object = NULL;
    hddvd_stream *payload = NULL;
    void *raw;
    size_t raw_size, got = 0;
    uint64_t length;
    int rc = read_member(aca, name, &raw, &raw_size);
    if (rc || !hddvd_object_is_envelope(raw, raw_size))
    {
        if (!rc)
        {
            *out = raw;
            *size = raw_size;
        }
        return rc;
    }
    rc = hddvd_object_open(raw, raw_size, &object);
    if (!rc)
        rc = hddvd_object_open_payload(object, &payload);
    if (!rc)
    {
        length = hddvd_stream_size(payload);
        *out = length <= SIZE_MAX - 1 ? malloc((size_t)length + 1) : NULL;
        rc = *out ? hddvd_stream_read_at(payload, 0, *out, (size_t)length, &got) : HDDVD_ENOMEM;
        if (!rc && got != length)
            rc = HDDVD_EIO;
        if (rc)
            free(*out);
        else
            *size = (size_t)length;
    }
    hddvd_stream_close(payload);
    hddvd_object_close(object);
    free(raw);
    return rc;
}

/* Manifest references may be URIs; members are found by base name. */
static const char *base_name(const char *src)
{
    const char *slash = strrchr(src, '/');
    return slash ? slash + 1 : src;
}

static void report(const hddvd_runtime *runtime, const char *what, int rc)
{
    hddvd_runtime_diagnostic d;
    size_t i;
    hddvd_runtime_get_diagnostic(runtime, &d);
    if (rc)
        printf("  %s: %s%s%s\n", what, hddvd_strerror(rc), d.message ? ": " : "",
               d.message ? d.message : "");
    if (rc && d.trace)
        printf("  trace:\n%s", d.trace);
    for (i = 0; i < hddvd_runtime_issue_count(runtime); i++)
    {
        hddvd_runtime_issue issue;
        hddvd_runtime_get_issue(runtime, i, &issue);
        printf("  issue: %s\n", issue.message);
    }
    if (hddvd_runtime_issues_dropped(runtime))
        printf("  issues dropped: %lu\n", (unsigned long)hddvd_runtime_issues_dropped(runtime));
    hddvd_runtime_clear_issues((hddvd_runtime *)runtime);
}

static int read_file(const char *path, void **out, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (!file)
        return HDDVD_EIO;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) ||
        !(*out = malloc((size_t)length + 1)) ||
        fread(*out, 1, (size_t)length, file) != (size_t)length)
    {
        fclose(file);
        return HDDVD_EIO;
    }
    fclose(file);
    *size = (size_t)length;
    return HDDVD_OK;
}

static void print_focus(const hddvd_runtime *runtime, const hddvd_xmu *xmu)
{
    size_t focus = hddvd_runtime_focus(runtime);
    hddvd_xmu_node node;
    if (focus == SIZE_MAX || !xmu || hddvd_xmu_get_node(xmu, focus, &node))
        printf("  focus: none");
    else
        printf("  focus: %s", node.id ? node.id : node.name);
    printf(" (generation %llu)\n", (unsigned long long)hddvd_runtime_generation(runtime));
}

static int key_code(const char *step, uint32_t *code)
{
    static const struct
    {
        const char *name;
        uint32_t code;
    } names[] = {{"left", HDDVD_KEY_LEFT},
                 {"right", HDDVD_KEY_RIGHT},
                 {"up", HDDVD_KEY_UP},
                 {"down", HDDVD_KEY_DOWN},
                 {"enter", HDDVD_KEY_ENTER}};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(step, names[i].name))
        {
            *code = names[i].code;
            return 1;
        }
    if (!strncmp(step, "key:", 4))
    {
        char *end;
        unsigned long value = strtoul(step + 4, &end, 0);
        if (*end || value > 0xffffffffu)
            return 0;
        *code = (uint32_t)value;
        return 1;
    }
    return 0;
}

static int step(hddvd_runtime *runtime, const char *text, hddvd_time *now)
{
    uint32_t code;
    int rc, prevented = 0;
    if (key_code(text, &code))
    {
        rc = hddvd_runtime_key(runtime, code, 1, &prevented);
        report(runtime, "key down", rc);
        rc = hddvd_runtime_key(runtime, code, 0, NULL);
        report(runtime, "key up", rc);
        if (prevented)
            printf("  prevented by a listener\n");
        return 0;
    }
    if (!strncmp(text, "wait:", 5))
    {
        char *end;
        double seconds = strtod(text + 5, &end);
        if (*end || !(seconds >= 0) || seconds > 86400)
            return -1;
        now->ticks += (uint64_t)(seconds * 90000.0 + 0.5);
        report(runtime, "advance", hddvd_runtime_advance(runtime, *now));
        return 0;
    }
    if (!strncmp(text, "event:", 6) && text[6])
    {
        report(runtime, "dispatch", hddvd_runtime_dispatch(runtime, text + 6));
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    hddvd_aca *aca = NULL;
    hddvd_xmf *xmf = NULL;
    hddvd_xmu *xmu = NULL;
    hddvd_runtime *runtime = NULL;
    hddvd_time now = {0};
    void *bytes = NULL, *markup = NULL;
    size_t size = 0, markup_size = 0, i;
    const char *id, *src, *before = NULL, *program = argv[0];
    int rc, failed = 0;
    if (argc >= 3 && !strcmp(argv[1], "--before"))
    {
        before = argv[2];
        argv += 2;
        argc -= 2;
    }
    if (argc < 3)
    {
        fprintf(stderr,
                "Usage: %s [--before FILE] archive.aca MANIFEST.xmf [STEP...]\n"
                "Steps: left, right, up, down, enter, key:CODE, wait:SECONDS, event:TYPE\n",
                program);
        return 2;
    }
    if (!hddvd_runtime_engine())
    {
        fprintf(stderr, "this build has no script engine\n");
        return 1;
    }
    rc = hddvd_aca_open(argv[1], &aca);
    if (!rc)
        rc = read_member(aca, argv[2], &bytes, &size);
    if (!rc)
        rc = hddvd_xmf_open(bytes, size, &xmf);
    if (!rc)
        rc = hddvd_runtime_create(&runtime);
    if (rc)
    {
        fprintf(stderr, "%s: %s\n", argv[2], hddvd_strerror(rc));
        hddvd_xmf_close(xmf);
        free(bytes);
        hddvd_aca_close(aca);
        return 1;
    }
    if (before)
    {
        void *source = NULL;
        size_t source_size = 0;
        printf("script %s\n", before);
        rc = read_file(before, &source, &source_size);
        if (rc)
            printf("  not run: %s\n", hddvd_strerror(rc));
        else
            report(runtime, "failed", hddvd_runtime_evaluate(runtime, before, source, source_size));
        failed |= rc != 0;
        free(source);
    }
    for (i = 0; i < hddvd_xmf_script_count(xmf); i++)
    {
        void *source = NULL;
        size_t source_size = 0;
        hddvd_xmf_get_script(xmf, i, &id, &src);
        printf("script %s\n", base_name(src));
        rc = read_script(aca, base_name(src), &source, &source_size);
        if (rc)
            printf("  not run: %s\n", hddvd_strerror(rc));
        else
            report(runtime, "failed",
                   hddvd_runtime_evaluate(runtime, base_name(src), source, source_size));
        failed |= rc != 0;
        free(source);
    }
    if (hddvd_xmf_markup_count(xmf))
    {
        hddvd_xmf_get_markup(xmf, 0, &id, &src);
        printf("markup %s\n", base_name(src));
        rc = read_member(aca, base_name(src), &markup, &markup_size);
        if (!rc)
            rc = hddvd_xmu_open(markup, markup_size, &xmu);
        if (!rc)
            rc = hddvd_runtime_load_markup(runtime, xmu);
        report(runtime, "not loaded", rc);
        failed |= rc != 0;
    }
    printf("markup loaded handler\n");
    report(runtime, "failed", hddvd_runtime_markup_loaded(runtime));
    print_focus(runtime, xmu);
    for (i = 3; i < (size_t)argc; i++)
    {
        printf("step %s\n", argv[i]);
        if (step(runtime, argv[i], &now))
        {
            fprintf(stderr, "unknown step %s\n", argv[i]);
            failed = 1;
            break;
        }
        print_focus(runtime, xmu);
    }
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
    free(markup);
    hddvd_xmf_close(xmf);
    free(bytes);
    hddvd_aca_close(aca);
    return failed;
}
