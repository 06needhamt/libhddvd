#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Runs the input as a script (any encoding the runtime accepts) with a small
   document, playlist, player state and application list, calls its function
   f and the markup loaded handler, then sends keys, events and time and
   acknowledges the commands made, directly and through the script bridge.
   The input is also run as the contents of a <timing> element. All under
   small budgets so every input
   finishes quickly. */
static const char markup[] =
    "<root xmlns='http://www.dvdforum.org/2005/ihd'"
    " xmlns:s='http://www.dvdforum.org/2005/ihd#style'"
    " xmlns:st='http://www.dvdforum.org/2005/ihd#state'><head><styling>"
    "<style id='look' s:x='10px' s:opacity='0.5'/></styling></head><body><div id='page'>"
    "<button id='a' style='look' st:focused='true' s:navRight='b' s:navDown='b'/>"
    "<button id='b' s:navLeft='a' s:navUpLeft='a'/><input id='field' st:value='v'/>"
    "</div></body></root>";
static const char playlist[] =
    "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'><TitleSet timeBase='60fps'>"
    "<Title id='Menu' titleDuration='00:00:10:00'/><Title id='Feature' titleDuration='00:01:00:00'>"
    "<ChapterList><Chapter titleTimeBegin='00:00:00:00'/><Chapter titleTimeBegin='00:00:30:00'/>"
    "</ChapterList><TrackNavigationList><AudioTrack track='1' langcode='en:00'/>"
    "</TrackNavigationList></Title></TitleSet></Playlist>";

static const char timing_open[] =
    "<root xmlns='http://www.dvdforum.org/2005/ihd'"
    " xmlns:style='http://www.dvdforum.org/2005/ihd#style'"
    " xmlns:state='http://www.dvdforum.org/2005/ihd#state'><head><timing clock='page'>";
static const char timing_close[] =
    "</timing></head><body><div id='page'><button id='a' class='b' state:focused='true'"
    " style:navRight='b'/><button id='b' class='b' style:navLeft='a'/>"
    "<input id='field' state:value='v'/></div></body></root>";

/* The input as markup timing, against a small document. */
static void timing(hddvd_runtime *runtime, const uint8_t *data, size_t size)
{
    static char xml[sizeof(timing_open) + sizeof(timing_close) + 8192];
    hddvd_xmu *xmu = NULL;
    hddvd_time now;
    size_t open = sizeof(timing_open) - 1, close = sizeof(timing_close) - 1, i;
    if (size > 8192)
        return;
    memcpy(xml, timing_open, open);
    memcpy(xml + open, data, size);
    memcpy(xml + open + size, timing_close, close);
    if (hddvd_xmu_open(xml, open + size + close, &xmu) != HDDVD_OK)
        return;
    (void)hddvd_runtime_evaluate(runtime, "vars.js", "document.setXPathVariable('V', 'a');", 36);
    if (hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK)
    {
        for (i = 0; i < 4; i++)
        {
            (void)hddvd_runtime_key(runtime, i % 2 ? HDDVD_KEY_ENTER : HDDVD_KEY_RIGHT, 1, NULL);
            (void)hddvd_runtime_key(runtime, HDDVD_KEY_ENTER, 0, NULL);
            now.ticks = 1000000u + (uint64_t)i * 60000u;
            (void)hddvd_runtime_advance(runtime, now);
        }
        (void)hddvd_runtime_dispatch(runtime, "x");
    }
    (void)hddvd_runtime_load_markup(runtime, NULL);
    hddvd_xmu_close(xmu);
}

/* Takes every command the scripts made, accepting and rejecting in turn. */
static void drain(hddvd_runtime *runtime)
{
    hddvd_runtime_command command;
    unsigned n = 0;
    while (hddvd_runtime_take_command(runtime, &command) == HDDVD_OK)
        (void)hddvd_runtime_acknowledge(runtime, command.id,
                                        n++ % 2 ? HDDVD_COMMAND_REJECTED : HDDVD_COMMAND_ACCEPTED);
}

/* Ids of commands the bridge handed over, acknowledged by later events. */
struct handed
{
    uint64_t ids[128];
    size_t count;
};

static void on_command(void *opaque, const hddvd_runtime_command *command)
{
    struct handed *handed = (struct handed *)opaque;
    if (handed->count < sizeof(handed->ids) / sizeof(handed->ids[0]))
        handed->ids[handed->count++] = command->id;
}

/* Every event type the bridge handles, then acknowledgements for what it
   handed over. */
static void bridge(hddvd_runtime *runtime)
{
    static const hddvd_event_type types[] = {
        HDDVD_EVENT_TITLE_BEGIN,   HDDVD_EVENT_PLAYBACK_STATE,
        HDDVD_EVENT_CHAPTER,       HDDVD_EVENT_STREAM_SELECTION,
        HDDVD_EVENT_INPUT,         HDDVD_EVENT_CLOCK,
        HDDVD_EVENT_DISCONTINUITY, HDDVD_EVENT_STREAM_SELECTION,
        HDDVD_EVENT_TITLE_END};
    struct handed handed;
    hddvd_runtime_bridge *b = NULL;
    hddvd_event e;
    size_t i;
    handed.count = 0;
    if (hddvd_runtime_bridge_create(runtime, on_command, &handed, &b) != HDDVD_OK)
        return;
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    {
        memset(&e, 0, sizeof(e));
        e.type = types[i];
        e.when.ticks = 300000u + (uint64_t)(i % 6) * 30000u;
        e.data.position.title = (uint32_t)(i % 3);
        e.data.stream.stream_type = (uint32_t)(i % 3);
        e.data.stream.stream_id = 1;
        e.data.input.key = HDDVD_KEY_ENTER;
        e.data.input.pressed = 1;
        (void)hddvd_runtime_bridge_event(b, &e);
    }
    for (i = 0; i < handed.count; i++)
    {
        memset(&e, 0, sizeof(e));
        e.type = HDDVD_EVENT_SCRIPT_ACK;
        e.when.ticks = 600000;
        e.data.acknowledgement.command_id = handed.ids[i];
        e.data.acknowledgement.result = i % 2 ? HDDVD_COMMAND_FAILED : HDDVD_COMMAND_ACCEPTED;
        (void)hddvd_runtime_bridge_event(b, &e);
    }
    hddvd_runtime_bridge_destroy(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint32_t keys[] = {HDDVD_KEY_RIGHT, HDDVD_KEY_ENTER, HDDVD_KEY_UP_LEFT, 0xfa};
    hddvd_runtime_limits limits;
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_diagnostic diagnostic;
    static const char *const applications[] = {"menu", "game"};
    static const char *const names[] = {"oldValue", "newValue"};
    static const char *const values[] = {"1", "2"};
    hddvd_runtime_issue issue;
    hddvd_runtime_player player;
    hddvd_xmu *xmu = NULL;
    hddvd_xpl *xpl = NULL;
    hddvd_time now;
    size_t i;
    hddvd_runtime_limits_default(&limits);
    limits.max_heap_bytes = 4u * 1024u * 1024u;
    limits.max_recursion = 16;
    limits.max_operations = 200000;
    limits.max_milliseconds = 200;
    if (hddvd_runtime_create_with_limits(&limits, &runtime) != HDDVD_OK)
        return 0;
    if (hddvd_xmu_open(markup, strlen(markup), &xmu) == HDDVD_OK)
        (void)hddvd_runtime_load_markup(runtime, xmu);
    if (hddvd_xpl_open(playlist, strlen(playlist), &xpl) == HDDVD_OK)
        (void)hddvd_runtime_load_playlist(runtime, xpl);
    memset(&player, 0, sizeof(player));
    player.title = 1;
    player.elapsed.ticks = 90090;
    player.chapter = 1;
    player.state = HDDVD_PLAYBACK_PLAYING;
    player.audio_track = 1;
    (void)hddvd_runtime_set_player(runtime, &player);
    (void)hddvd_runtime_set_applications(runtime, applications, 2, 0);
    (void)hddvd_runtime_evaluate(runtime, "fuzz.js", data, size);
    (void)hddvd_runtime_get_diagnostic(runtime, &diagnostic);
    (void)hddvd_runtime_call(runtime, "f");
    (void)hddvd_runtime_markup_loaded(runtime);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        (void)hddvd_runtime_key(runtime, keys[i], 1, NULL);
        (void)hddvd_runtime_key(runtime, keys[i], 0, NULL);
        /* The pointer on each node in turn, pressed on every other one,
           then on none and on one the document lacks. */
        (void)hddvd_runtime_pointer(runtime, i, (int)(i & 1u));
        (void)hddvd_runtime_pointer(runtime, i % 3 ? SIZE_MAX : 100000u, 0);
        now.ticks = (uint64_t)(i + 1) * 45000u;
        (void)hddvd_runtime_advance(runtime, now);
    }
    (void)hddvd_runtime_dispatch(runtime, "fuzz");
    (void)hddvd_runtime_dispatch_fields(runtime, "play_state", names, values, 2);
    drain(runtime);
    bridge(runtime);
    (void)hddvd_runtime_get_diagnostic(runtime, &diagnostic);
    for (i = 0; i < hddvd_runtime_issue_count(runtime); i++)
        (void)hddvd_runtime_get_issue(runtime, i, &issue);
    for (i = 0; xmu && i < hddvd_xmu_node_count(xmu); i++)
    {
        (void)hddvd_runtime_get_state(runtime, i, "value");
        (void)hddvd_runtime_get_style(runtime, i, "x");
    }
    (void)hddvd_runtime_focus(runtime);
    /* Element objects outlive their document and must fail cleanly. */
    (void)hddvd_runtime_load_markup(runtime, NULL);
    (void)hddvd_runtime_load_playlist(runtime, NULL);
    (void)hddvd_runtime_call(runtime, "f");
    timing(runtime, data, size);
    /* Commands left unacknowledged are released with the runtime. */
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
    hddvd_xpl_close(xpl);
    return 0;
}
