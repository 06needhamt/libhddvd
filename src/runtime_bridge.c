/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "runtime_internal.h"
#include <stdlib.h>

/* Feeds navigation-queue events to a script runtime and its commands to the
   host (hddvd_runtime_bridge_event in hddvd.h). The runtime still makes no
   callbacks; the bridge polls it after each call. */

struct hddvd_runtime_bridge
{
    hddvd_runtime *runtime;
    hddvd_runtime_command_fn command_fn;
    void *opaque;
    /* Scheduler time of the previous event, and the runtime clock. */
    hddvd_time last;
    hddvd_time clock;
};

int hddvd_runtime_bridge_create(hddvd_runtime *runtime, hddvd_runtime_command_fn command_fn,
                                void *opaque, hddvd_runtime_bridge **out)
{
    hddvd_runtime_bridge *bridge;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!runtime)
        return HDDVD_EINVAL;
    bridge = (hddvd_runtime_bridge *)calloc(1, sizeof(*bridge));
    if (!bridge)
        return HDDVD_ENOMEM;
    bridge->runtime = runtime;
    bridge->command_fn = command_fn;
    bridge->opaque = opaque;
    bridge->clock.ticks = runtime->now;
    *out = bridge;
    return HDDVD_OK;
}

void hddvd_runtime_bridge_destroy(hddvd_runtime_bridge *bridge)
{
    free(bridge);
}

static void keep(int *rc, int status)
{
    if (*rc == HDDVD_OK && status != HDDVD_OK)
        *rc = status;
}

/* Hands waiting commands to the host, or rejects them without a callback. */
static int forward(hddvd_runtime_bridge *bridge)
{
    hddvd_runtime_command command;
    for (;;)
    {
        int rc = hddvd_runtime_take_command(bridge->runtime, &command);
        if (rc == HDDVD_ENOTFOUND || rc == HDDVD_ELIMIT)
            return HDDVD_OK;
        if (rc != HDDVD_OK)
            return rc;
        if (bridge->command_fn)
            bridge->command_fn(bridge->opaque, &command);
        else
        {
            rc = hddvd_runtime_acknowledge(bridge->runtime, command.id, HDDVD_COMMAND_REJECTED);
            if (rc != HDDVD_OK)
                return rc;
        }
    }
}

static int report(hddvd_runtime_bridge *bridge, const hddvd_runtime_player *player,
                  const char *type)
{
    int rc = hddvd_runtime_set_player(bridge->runtime, player);
    if (rc != HDDVD_OK || !type)
        return rc;
    return hddvd_runtime_dispatch(bridge->runtime, type);
}

int hddvd_runtime_bridge_event(hddvd_runtime_bridge *bridge, const hddvd_event *event)
{
    hddvd_runtime_player player;
    const char *type = NULL;
    int rc = HDDVD_OK, changed = 0;
    if (!bridge || !event)
        return HDDVD_EINVAL;
    player = bridge->runtime->player;
    if (event->type != HDDVD_EVENT_DISCONTINUITY && event->when.ticks > bridge->last.ticks &&
        event->when.ticks - bridge->last.ticks <= UINT64_MAX - bridge->clock.ticks)
        bridge->clock.ticks += event->when.ticks - bridge->last.ticks;
    bridge->last = event->when;
    if (player.title != SIZE_MAX && player.elapsed.ticks != event->when.ticks)
    {
        player.elapsed = event->when;
        keep(&rc, hddvd_runtime_set_player(bridge->runtime, &player));
    }
    if (bridge->clock.ticks > bridge->runtime->now)
        keep(&rc, hddvd_runtime_advance(bridge->runtime, bridge->clock));
    switch (event->type)
    {
        case HDDVD_EVENT_INPUT:
            keep(&rc, hddvd_runtime_key(bridge->runtime, event->data.input.key,
                                        event->data.input.pressed != 0, NULL));
            break;
        case HDDVD_EVENT_PLAYBACK_STATE:
            player.state = event->data.playback_state;
            type = "play_state";
            changed = 1;
            break;
        case HDDVD_EVENT_TITLE_BEGIN:
            player.title = event->data.position.title;
            player.chapter = 0;
            player.elapsed = event->when;
            type = "title_begin";
            changed = 1;
            break;
        case HDDVD_EVENT_TITLE_END:
            keep(&rc, hddvd_runtime_dispatch(bridge->runtime, "title_end"));
            break;
        case HDDVD_EVENT_CHAPTER:
            player.chapter = event->data.position.chapter;
            type = "chapter";
            changed = 1;
            break;
        case HDDVD_EVENT_STREAM_SELECTION:
            if (event->data.stream.stream_type == HDDVD_STREAM_TYPE_AUDIO)
            {
                player.audio_track = event->data.stream.stream_id;
                type = "audio_track";
                changed = 1;
            }
            else if (event->data.stream.stream_type == HDDVD_STREAM_TYPE_SUBTITLE)
            {
                player.subtitle_track = event->data.stream.stream_id;
                type = "subtitle_track";
                changed = 1;
            }
            break;
        case HDDVD_EVENT_SCRIPT_ACK:
            keep(&rc,
                 hddvd_runtime_acknowledge(bridge->runtime, event->data.acknowledgement.command_id,
                                           event->data.acknowledgement.result));
            break;
        default:
            break;
    }
    if (changed)
        keep(&rc, report(bridge, &player, type));
    keep(&rc, forward(bridge));
    return rc;
}
