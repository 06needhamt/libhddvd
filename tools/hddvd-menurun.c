#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs one HDi application from an archive in the script runtime: evaluates
   its manifest's scripts in order, loads its first markup, calls the markup
   loaded handler, then performs the steps given and prints the focus after
   each, plus every issue, failure and player command (each acknowledged as
   accepted). --before FILE first evaluates a local script, for example
   stand-ins for host objects the runtime lacks; --playlist FILE.xpl gives
   scripts the disc's playlist, and --title ID reports that title playing.
   --state also prints, after each step, the ids of the elements shown: those
   that are visible in the scene built from the runtime's styles, as are all
   their ancestors. tests/replay_reference_trace.py compares these lines with
   a trace recorded on a player. */

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
        {
            free(*out);
            *out = NULL;
        }
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

static const hddvd_xpl *playlist;
static const char *base_name(const char *src);

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b))
            return 0;
    return !*a && !*b;
}

/* The first title whose applications include this manifest; its
   applications in z-order (bottom first), and which one is this. */
static size_t find_title(const hddvd_xpl *xpl, const char *manifest, const char *self_id,
                         const char **ids, size_t *count, size_t *self)
{
    size_t t, a, k;
    for (t = 0; t < hddvd_xpl_title_count(xpl); t++)
    {
        size_t n = hddvd_xpl_application_count(xpl, t), order[63];
        int32_t z[63];
        *self = SIZE_MAX;
        *count = 0;
        for (a = 0; a < n && *count < 63; a++)
        {
            hddvd_xpl_application app;
            if (hddvd_xpl_get_application(xpl, t, a, &app) || !app.src)
                continue;
            /* Insertion by zOrder keeps playlist order among equals. */
            for (k = *count; k > 0 && z[k - 1] > (app.has_z_order ? app.z_order : 0); k--)
            {
                z[k] = z[k - 1];
                order[k] = order[k - 1];
            }
            z[k] = app.has_z_order ? app.z_order : 0;
            order[k] = a;
            (*count)++;
        }
        for (k = 0; k < *count; k++)
        {
            hddvd_xpl_application app;
            hddvd_xpl_get_application(xpl, t, order[k], &app);
            ids[k] = "";
            if (same_name(base_name(app.src), manifest))
            {
                *self = k;
                ids[k] = self_id ? self_id : "";
            }
        }
        if (*self != SIZE_MAX)
            return t;
    }
    *count = 0;
    return SIZE_MAX;
}

/* Prints and accepts every command the scripts made. */
static void commands(hddvd_runtime *runtime)
{
    static const char *const names[] = {"play",
                                        "pause",
                                        "jump",
                                        "select-audio",
                                        "select-subtitle",
                                        "select-audio-language",
                                        "select-subtitle-language",
                                        "subtitle-visible",
                                        "activate-application",
                                        "inactivate-application",
                                        "application-to-top",
                                        "application-to-bottom"};
    hddvd_runtime_command command;
    while (hddvd_runtime_take_command(runtime, &command) == HDDVD_OK)
    {
        printf("  command: %s", names[command.type]);
        if (command.type == HDDVD_RUNTIME_COMMAND_JUMP)
        {
            hddvd_xpl_title_info info;
            uint64_t frames = command.time.ticks * 2u / 3003u;
            if (playlist && !hddvd_xpl_get_title(playlist, command.title, &info) && info.id)
                printf(" title=%s", info.id);
            else
                printf(" title=%lu", (unsigned long)command.title + 1);
            printf(" time=%02u:%02u:%02u:%02u", (unsigned)(frames / 216000u),
                   (unsigned)(frames / 3600u % 60u), (unsigned)(frames / 60u % 60u),
                   (unsigned)(frames % 60u));
        }
        else if (command.text[0])
            printf(" %s", command.text);
        else if (command.type != HDDVD_RUNTIME_COMMAND_PLAY &&
                 command.type != HDDVD_RUNTIME_COMMAND_PAUSE)
            printf(" %lu", (unsigned long)command.number);
        printf("\n");
        hddvd_runtime_acknowledge(runtime, command.id, HDDVD_COMMAND_ACCEPTED);
    }
}

static void report(hddvd_runtime *runtime, const char *what, int rc)
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
    hddvd_runtime_clear_issues(runtime);
    commands(runtime);
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

static int no_image(void *opaque, const char *url, const void **data, size_t *size)
{
    (void)opaque;
    (void)url;
    (void)data;
    (void)size;
    return HDDVD_ENOTFOUND;
}

static int show_state;

static void print_visible(const hddvd_runtime *runtime, const hddvd_xmu *xmu)
{
    hddvd_scene *scene = NULL;
    const hddvd_scene_node *nodes;
    size_t i, count;
    if (!xmu ||
        hddvd_scene_build(xmu, hddvd_runtime_styles(runtime), SIZE_MAX, no_image, NULL, &scene))
    {
        printf("  visible: unknown\n");
        return;
    }
    nodes = hddvd_scene_nodes(scene);
    count = hddvd_scene_node_count(scene);
    printf("  visible:");
    for (i = 0; i < count; i++)
    {
        hddvd_xmu_node node;
        size_t at = i;
        /* Parents precede children, so the walk up ends. */
        while (at < count && nodes[at].visible && nodes[at].parent < at)
            at = nodes[at].parent;
        if (at < count && nodes[at].visible &&
            hddvd_xmu_get_node(xmu, (size_t)nodes[i].id, &node) == HDDVD_OK && node.id)
            printf(" %s", node.id);
    }
    printf("\n");
    hddvd_scene_close(scene);
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
    if (show_state)
        print_visible(runtime, xmu);
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
    hddvd_xpl *xpl = NULL;
    hddvd_time now = {0};
    void *bytes = NULL, *markup = NULL, *xpl_bytes = NULL;
    size_t size = 0, markup_size = 0, xpl_size = 0, i;
    const char *id, *src, *before = NULL, *playlist_path = NULL, *title = NULL;
    const char *program = argv[0];
    int rc, failed = 0;
    while (argc >= 2 && !strcmp(argv[1], "--state"))
    {
        show_state = 1;
        argv++;
        argc--;
    }
    while (argc >= 3 && (!strcmp(argv[1], "--before") || !strcmp(argv[1], "--playlist") ||
                         !strcmp(argv[1], "--title")))
    {
        if (!strcmp(argv[1], "--before"))
            before = argv[2];
        else if (!strcmp(argv[1], "--playlist"))
            playlist_path = argv[2];
        else
            title = argv[2];
        argv += 2;
        argc -= 2;
    }
    if (argc < 3 || (title && !playlist_path))
    {
        fprintf(
            stderr,
            "Usage: %s [--state] [--before FILE] [--playlist FILE.xpl [--title ID]] archive.aca "
            "MANIFEST.xmf [STEP...]\n"
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
    if (playlist_path)
    {
        hddvd_runtime_player player;
        size_t index = SIZE_MAX;
        printf("playlist %s\n", playlist_path);
        rc = read_file(playlist_path, &xpl_bytes, &xpl_size);
        if (!rc)
            rc = hddvd_xpl_open(xpl_bytes, xpl_size, &xpl);
        if (!rc)
            rc = hddvd_runtime_load_playlist(runtime, xpl);
        if (!rc && title && hddvd_xpl_find_title(xpl, title, &index))
            rc = HDDVD_ENOTFOUND;
        if (!rc)
        {
            const char *ids[63];
            size_t count = 0, self = SIZE_MAX;
            size_t found =
                find_title(xpl, argv[2], hddvd_xmf_application_id(xmf), ids, &count, &self);
            if (!title)
                index = found;
            if (found != SIZE_MAX)
                rc = hddvd_runtime_set_applications(runtime, ids, count, self);
            printf("  title %lu, application %lu of %lu\n",
                   index == SIZE_MAX ? 0ul : (unsigned long)index + 1,
                   self == SIZE_MAX ? 0ul : (unsigned long)self + 1, (unsigned long)count);
        }
        if (!rc && index != SIZE_MAX)
        {
            memset(&player, 0, sizeof(player));
            player.title = index;
            player.state = HDDVD_PLAYBACK_PLAYING;
            rc = hddvd_runtime_set_player(runtime, &player);
        }
        playlist = xpl;
        report(runtime, "not loaded", rc);
        failed |= rc != 0;
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
    hddvd_xpl_close(xpl);
    free(xpl_bytes);
    hddvd_xmu_close(xmu);
    free(markup);
    hddvd_xmf_close(xmf);
    free(bytes);
    hddvd_aca_close(aca);
    return failed;
}
