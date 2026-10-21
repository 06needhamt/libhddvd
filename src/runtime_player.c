#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

/* The state behind the HDi Player and application lifecycle objects
   (hdi_prelude.js): the borrowed playlist, the player state and application
   list the host reports, and the queue of commands scripts make. Everything
   here is engine-neutral; the runtime never calls the host. */

enum player_native
{
    PLAYER_TITLE_COUNT,
    PLAYER_TITLE,
    PLAYER_CHAPTER,
    PLAYER_TRACK_LANGUAGE,
    PLAYER_STATE,
    PLAYER_COMMAND,
    PLAYER_APPLICATION
};

/* Times scripts give must stay exact as ECMAScript numbers. */
#define MAX_TICKS 9007199254740992.0

static int fail(const char **message, const char *text, int rc)
{
    *message = text;
    return rc;
}

static int arg_number(const hddvd_engine_value *args, size_t count, size_t i, double *out)
{
    if (i >= count || (args[i].type != HDDVD_ENGINE_NUMBER && args[i].type != HDDVD_ENGINE_BOOLEAN))
        return 0;
    *out = args[i].number;
    return 1;
}

/* A whole number in [0, limit). */
static int arg_index(const hddvd_engine_value *args, size_t count, size_t i, double limit,
                     size_t *out)
{
    double value;
    if (!arg_number(args, count, i, &value) || !(value >= 0 && value < limit) ||
        value != (double)(size_t)value)
        return 0;
    *out = (size_t)value;
    return 1;
}

static void set_number(hddvd_engine_value *result, double value)
{
    result->type = HDDVD_ENGINE_NUMBER;
    result->number = value;
}

static void set_string(hddvd_engine_value *result, const char *value)
{
    if (!value)
    {
        result->type = HDDVD_ENGINE_NULL;
        return;
    }
    result->type = HDDVD_ENGINE_STRING;
    result->string = value;
    result->length = strlen(value);
}

static size_t title_count(const hddvd_runtime *runtime)
{
    return runtime->xpl ? hddvd_xpl_title_count(runtime->xpl) : 0;
}

/* A playlist time in ticks, or -1 when the playlist's time base is not
   modeled. */
static double ticks_of(hddvd_xpl_time time)
{
    hddvd_time ticks;
    return hddvd_xpl_time_to_ticks(time, &ticks) ? -1.0 : (double)ticks.ticks;
}

/* Arguments 0 and 1: the playlist the title object came from, and its
   index. */
static int arg_title(const hddvd_runtime *runtime, const hddvd_engine_value *args, size_t count,
                     size_t *title, const char **message)
{
    double epoch;
    if (!arg_number(args, count, 0, &epoch))
        return fail(message, "invalid title", HDDVD_EINVAL);
    if (!runtime->xpl || epoch != (double)runtime->playlists)
        return fail(message, "the title is no longer in the playlist", HDDVD_EINVAL);
    if (!arg_index(args, count, 1, (double)title_count(runtime), title))
        return fail(message, "invalid title", HDDVD_EINVAL);
    return HDDVD_OK;
}

static int language_code(const char *text, size_t length)
{
    size_t i;
    if (length < 2 || length > 3)
        return 0;
    for (i = 0; i < length; i++)
        if (text[i] < 'a' || text[i] > 'z')
            return 0;
    return 1;
}

static int queue_command(hddvd_runtime *runtime, const hddvd_runtime_command *command,
                         const char *text, size_t length, hddvd_engine_value *result,
                         const char **message)
{
    struct hddvd_runtime_pending *pending;
    if (runtime->queued_count == HDDVD_RUNTIME_MAX_COMMANDS)
        return fail(message, "too many player commands are waiting", HDDVD_ELIMIT);
    pending = &runtime->queued[runtime->queued_count];
    pending->text = malloc(length + 1);
    if (!pending->text)
        return fail(message, "out of memory", HDDVD_ENOMEM);
    if (length)
        memcpy(pending->text, text, length);
    pending->text[length] = '\0';
    pending->command = *command;
    pending->command.id = ++runtime->next_command;
    pending->command.text = pending->text;
    runtime->queued_count++;
    set_number(result, (double)pending->command.id);
    return HDDVD_OK;
}

/* command(type, a, b); see hddvd_runtime_command_type for what a and b are. */
static int make_command(hddvd_runtime *runtime, const hddvd_engine_value *args, size_t count,
                        hddvd_engine_value *result, const char **message)
{
    hddvd_runtime_command command;
    const char *text = "";
    size_t length = 0, index;
    double type, value;
    memset(&command, 0, sizeof(command));
    if (!arg_number(args, count, 0, &type) ||
        !(type >= HDDVD_RUNTIME_COMMAND_PLAY &&
          type <= HDDVD_RUNTIME_COMMAND_APPLICATION_TO_BOTTOM))
        return fail(message, "invalid command", HDDVD_EINVAL);
    command.type = (hddvd_runtime_command_type)(int)type;
    switch (command.type)
    {
        case HDDVD_RUNTIME_COMMAND_PLAY:
        case HDDVD_RUNTIME_COMMAND_PAUSE:
            break;
        case HDDVD_RUNTIME_COMMAND_JUMP:
        {
            hddvd_xpl_title_info info;
            if (!arg_index(args, count, 1, (double)title_count(runtime), &index))
                return fail(message, "invalid title", HDDVD_EINVAL);
            if (!arg_number(args, count, 2, &value) || !(value >= 0 && value < MAX_TICKS) ||
                value != (double)(uint64_t)value)
                return fail(message, "invalid time", HDDVD_EINVAL);
            if (!hddvd_xpl_get_title(runtime->xpl, index, &info) && info.has_duration &&
                ticks_of(info.duration) >= 0 && value > ticks_of(info.duration))
                return fail(message, "the time is after the end of the title", HDDVD_EINVAL);
            command.title = index;
            command.time.ticks = (uint64_t)value;
            break;
        }
        case HDDVD_RUNTIME_COMMAND_SELECT_AUDIO:
        case HDDVD_RUNTIME_COMMAND_SELECT_SUBTITLE:
            if (!arg_index(args, count, 1, 256.0, &index) || !index)
                return fail(message, "invalid track number", HDDVD_EINVAL);
            command.number = (uint32_t)index;
            break;
        case HDDVD_RUNTIME_COMMAND_SELECT_AUDIO_LANGUAGE:
        case HDDVD_RUNTIME_COMMAND_SELECT_SUBTITLE_LANGUAGE:
            if (count < 2 || args[1].type != HDDVD_ENGINE_STRING ||
                !language_code(args[1].string, args[1].length))
                return fail(message, "invalid language code", HDDVD_EINVAL);
            text = args[1].string;
            length = args[1].length;
            break;
        case HDDVD_RUNTIME_COMMAND_SUBTITLE_VISIBLE:
            if (!arg_number(args, count, 1, &value))
                return fail(message, "invalid visibility", HDDVD_EINVAL);
            command.number = value != 0.0;
            break;
        default:
            if (!arg_index(args, count, 1, (double)runtime->application_count, &index))
                return fail(message, "invalid application", HDDVD_EINVAL);
            command.number = (uint32_t)index;
            text = runtime->applications[index];
            length = strlen(text);
            break;
    }
    return queue_command(runtime, &command, text, length, result, message);
}

int hddvd_runtime_player_native(hddvd_runtime *runtime, unsigned index,
                                const hddvd_engine_value *args, size_t count,
                                hddvd_engine_value *result, const char **message)
{
    hddvd_xpl_title_info info;
    hddvd_xpl_chapter chapter;
    hddvd_xpl_track track;
    size_t title, at, i;
    double what, number;
    int rc;
    switch (index)
    {
        case PLAYER_TITLE_COUNT:
            set_number(result, (double)title_count(runtime));
            return HDDVD_OK;
        case PLAYER_TITLE:
            rc = arg_title(runtime, args, count, &title, message);
            if (rc || !arg_number(args, count, 2, &what) ||
                hddvd_xpl_get_title(runtime->xpl, title, &info))
                return rc ? rc : fail(message, "invalid title", HDDVD_EINVAL);
            if (what == 0)
                set_string(result, info.id);
            else if (what == 1)
                set_number(result, info.has_duration ? ticks_of(info.duration) : -1.0);
            else
                set_number(result, (double)hddvd_xpl_chapter_count(runtime->xpl, title));
            return HDDVD_OK;
        case PLAYER_CHAPTER:
            rc = arg_title(runtime, args, count, &title, message);
            if (rc)
                return rc;
            if (!arg_index(args, count, 2, (double)hddvd_xpl_chapter_count(runtime->xpl, title),
                           &at) ||
                hddvd_xpl_get_chapter(runtime->xpl, title, at, &chapter))
                return fail(message, "invalid chapter", HDDVD_EINVAL);
            set_number(result, ticks_of(chapter.begin));
            return HDDVD_OK;
        case PLAYER_TRACK_LANGUAGE:
            /* (playlist, title, 0 audio or 1 subtitle, track number). */
            rc = arg_title(runtime, args, count, &title, message);
            if (rc)
                return rc;
            if (!arg_number(args, count, 2, &what) || !arg_number(args, count, 3, &number))
                return fail(message, "invalid track", HDDVD_EINVAL);
            result->type = HDDVD_ENGINE_NULL;
            for (i = 0; i < hddvd_xpl_track_count(runtime->xpl, title); i++)
                if (!hddvd_xpl_get_track(runtime->xpl, title, i, &track) &&
                    track.kind ==
                        (what == 0 ? HDDVD_XPL_MEDIA_AUDIO : HDDVD_XPL_MEDIA_SUBPICTURE) &&
                    (double)track.track == number)
                {
                    set_string(result, track.language);
                    break;
                }
            return HDDVD_OK;
        case PLAYER_STATE:
            if (!arg_number(args, count, 0, &what))
                return fail(message, "invalid player field", HDDVD_EINVAL);
            switch ((int)what)
            {
                case 0:
                    set_number(result, runtime->player.title == SIZE_MAX
                                           ? -1.0
                                           : (double)runtime->player.title);
                    return HDDVD_OK;
                case 1:
                    set_number(result, (double)runtime->player.elapsed.ticks);
                    return HDDVD_OK;
                case 2:
                    set_number(result, (double)runtime->player.chapter);
                    return HDDVD_OK;
                case 3:
                    set_number(result, (double)runtime->player.state);
                    return HDDVD_OK;
                case 4:
                    set_number(result, (double)runtime->player.audio_track);
                    return HDDVD_OK;
                case 5:
                    set_number(result, (double)runtime->player.subtitle_track);
                    return HDDVD_OK;
                case 6:
                    result->type = HDDVD_ENGINE_BOOLEAN;
                    result->number = runtime->player.subtitle_visible ? 1.0 : 0.0;
                    return HDDVD_OK;
                case 7:
                    set_string(result, runtime->player.menu_language);
                    return HDDVD_OK;
                case 8:
                    set_number(result, runtime->xpl ? (double)runtime->playlists : 0.0);
                    return HDDVD_OK;
                default:
                    return fail(message, "invalid player field", HDDVD_EINVAL);
            }
        case PLAYER_COMMAND:
            return make_command(runtime, args, count, result, message);
        case PLAYER_APPLICATION:
            /* application(-1) counts, application(-2) is this runtime's index,
               application(i) an id. */
            if (!arg_number(args, count, 0, &number))
                return fail(message, "invalid application", HDDVD_EINVAL);
            if (number == -1)
                set_number(result, (double)runtime->application_count);
            else if (number == -2)
                set_number(result, runtime->application_self == SIZE_MAX
                                       ? -1.0
                                       : (double)runtime->application_self);
            else if (arg_index(args, count, 0, (double)runtime->application_count, &at))
                set_string(result, runtime->applications[at]);
            else
                return fail(message, "invalid application", HDDVD_EINVAL);
            return HDDVD_OK;
        default:
            return fail(message, "unknown host function", HDDVD_EINVAL);
    }
}

static void free_applications(hddvd_runtime *runtime)
{
    size_t i;
    for (i = 0; i < runtime->application_count; i++)
        free(runtime->applications[i]);
    runtime->application_count = 0;
    runtime->application_self = SIZE_MAX;
}

void hddvd_runtime_player_free(hddvd_runtime *runtime)
{
    size_t i;
    for (i = 0; i < runtime->queued_count; i++)
        free(runtime->queued[i].text);
    for (i = 0; i < runtime->taken_count; i++)
        free(runtime->taken[i].text);
    runtime->queued_count = runtime->taken_count = 0;
    free_applications(runtime);
}

/* Public API */

int hddvd_runtime_load_playlist(hddvd_runtime *runtime, const hddvd_xpl *xpl)
{
    if (!runtime)
        return HDDVD_EINVAL;
    runtime->xpl = xpl;
    runtime->playlists++;
    /* The reported title belonged to the previous playlist. */
    runtime->player.title = SIZE_MAX;
    return hddvd_runtime_run_entry(runtime, "playlistChanged", NULL, 0, NULL);
}

int hddvd_runtime_set_player(hddvd_runtime *runtime, const hddvd_runtime_player *player)
{
    if (!runtime || !player ||
        (player->title != SIZE_MAX && player->title >= title_count(runtime)) ||
        player->state > HDDVD_PLAYBACK_PAUSED ||
        !memchr(player->menu_language, '\0', sizeof(player->menu_language)) ||
        player->elapsed.ticks > (UINT64_C(1) << 53))
        return HDDVD_EINVAL;
    runtime->player = *player;
    return HDDVD_OK;
}

int hddvd_runtime_set_applications(hddvd_runtime *runtime, const char *const *ids, size_t count,
                                   size_t self)
{
    char *copies[HDDVD_RUNTIME_MAX_APPLICATIONS];
    size_t i;
    if (!runtime || (count && !ids) || count >= HDDVD_RUNTIME_MAX_APPLICATIONS ||
        (self != SIZE_MAX && self >= count))
        return HDDVD_EINVAL;
    for (i = 0; i < count; i++)
    {
        copies[i] = hddvd_strdup(ids[i] ? ids[i] : "");
        if (!copies[i])
        {
            while (i--)
                free(copies[i]);
            return HDDVD_ENOMEM;
        }
    }
    free_applications(runtime);
    for (i = 0; i < count; i++)
        runtime->applications[i] = copies[i];
    runtime->application_count = count;
    runtime->application_self = self;
    return HDDVD_OK;
}

size_t hddvd_runtime_command_count(const hddvd_runtime *runtime)
{
    return runtime ? runtime->queued_count : 0;
}

int hddvd_runtime_take_command(hddvd_runtime *runtime, hddvd_runtime_command *out)
{
    if (!runtime || !out)
        return HDDVD_EINVAL;
    if (!runtime->queued_count)
        return HDDVD_ENOTFOUND;
    /* Taken commands wait for acknowledgement; the queue cannot outgrow
       them, since both hold the same number. */
    if (runtime->taken_count == HDDVD_RUNTIME_MAX_COMMANDS)
        return HDDVD_ELIMIT;
    runtime->taken[runtime->taken_count++] = runtime->queued[0];
    memmove(runtime->queued, runtime->queued + 1,
            (runtime->queued_count - 1) * sizeof(runtime->queued[0]));
    runtime->queued_count--;
    *out = runtime->taken[runtime->taken_count - 1].command;
    return HDDVD_OK;
}

int hddvd_runtime_acknowledge(hddvd_runtime *runtime, uint64_t id, hddvd_command_result result)
{
    static const char *const names[] = {"play",
                                        "pause",
                                        "jump",
                                        "select audio",
                                        "select subtitle",
                                        "select audio language",
                                        "select subtitle language",
                                        "subtitle visibility",
                                        "activate application",
                                        "inactivate application",
                                        "application to top",
                                        "application to bottom"};
    size_t i;
    if (!runtime || result > HDDVD_COMMAND_FAILED)
        return HDDVD_EINVAL;
    for (i = 0; i < runtime->taken_count; i++)
        if (runtime->taken[i].command.id == id)
            break;
    if (i == runtime->taken_count)
        return HDDVD_EINVAL;
    if (result != HDDVD_COMMAND_ACCEPTED)
    {
        char message[96];
        snprintf(message, sizeof(message), "%s command %llu %s",
                 names[runtime->taken[i].command.type], (unsigned long long)id,
                 result == HDDVD_COMMAND_REJECTED ? "rejected" : "failed");
        hddvd_runtime_add_issue(runtime, HDDVD_RUNTIME_ISSUE_COMMAND, SIZE_MAX, message,
                                strlen(message));
    }
    free(runtime->taken[i].text);
    memmove(runtime->taken + i, runtime->taken + i + 1,
            (runtime->taken_count - i - 1) * sizeof(runtime->taken[0]));
    runtime->taken_count--;
    return HDDVD_OK;
}

/* Field names are identifiers, and not the event's own members. */
static int field_name(const char *name)
{
    static const char *const reserved[] = {
        "type",          "bubbles",         "cancelable",       "target",
        "currentTarget", "eventPhase",      "defaultPrevented", "cancelBubble",
        "initEvent",     "stopPropagation", "preventDefault"};
    size_t i;
    if (!name || !((*name >= 'a' && *name <= 'z') || (*name >= 'A' && *name <= 'Z') ||
                   *name == '_' || *name == '$'))
        return 0;
    for (i = 1; name[i]; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '$'))
            return 0;
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        if (!strcmp(name, reserved[i]))
            return 0;
    return 1;
}

int hddvd_runtime_dispatch_fields(hddvd_runtime *runtime, const char *type,
                                  const char *const *names, const char *const *values, size_t count)
{
    size_t i;
    int rc;
    if (!runtime || !type || !*type || count > HDDVD_RUNTIME_MAX_FIELDS ||
        (count && (!names || !values)))
        return HDDVD_EINVAL;
    for (i = 0; i < count; i++)
        if (!field_name(names[i]) || !values[i])
            return HDDVD_EINVAL;
    runtime->field_names = names;
    runtime->field_values = values;
    runtime->field_count = count;
    rc = hddvd_runtime_dispatch(runtime, type);
    runtime->field_names = runtime->field_values = NULL;
    runtime->field_count = 0;
    return rc;
}
