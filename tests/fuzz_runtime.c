#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Runs the input as a script (any encoding the runtime accepts) with a small
   document, playlist, player state and application list, calls its function
   f and the markup loaded handler, then sends keys, events and time and
   acknowledges the commands made, under small budgets so every input
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

/* Takes every command the scripts made, accepting and rejecting in turn. */
static void drain(hddvd_runtime *runtime)
{
    hddvd_runtime_command command;
    unsigned n = 0;
    while (hddvd_runtime_take_command(runtime, &command) == HDDVD_OK)
        (void)hddvd_runtime_acknowledge(runtime, command.id,
                                        n++ % 2 ? HDDVD_COMMAND_REJECTED : HDDVD_COMMAND_ACCEPTED);
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
        now.ticks = (uint64_t)(i + 1) * 45000u;
        (void)hddvd_runtime_advance(runtime, now);
    }
    (void)hddvd_runtime_dispatch(runtime, "fuzz");
    (void)hddvd_runtime_dispatch_fields(runtime, "play_state", names, values, 2);
    drain(runtime);
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
    /* Commands left unacknowledged are released with the runtime. */
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
    hddvd_xpl_close(xpl);
    return 0;
}
