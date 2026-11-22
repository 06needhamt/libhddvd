/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The script bridge: scheduler events run scripts in queue order, player
   events reach listeners with the reported state, script timers follow the
   scheduler clock, and script commands go to the host and come back as
   HDDVD_EVENT_SCRIPT_ACK. */

static const char playlist[] =
    "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
    "<TitleSet timeBase='60fps'>"
    "<Title id='Menu' titleDuration='00:00:10:00'/>"
    "<Title id='Feature' titleDuration='00:10:00:00'><ChapterList>"
    "<Chapter titleTimeBegin='00:00:00:00'/><Chapter titleTimeBegin='00:05:00:00'/>"
    "</ChapterList><TrackNavigationList>"
    "<AudioTrack track='1' langcode='en:00'/><AudioTrack track='2' langcode='fr:00'/>"
    "<SubpictureTrack track='1' langcode='de:00'/>"
    "</TrackNavigationList></Title>"
    "</TitleSet></Playlist>";

/* 90 kHz ticks of n frames at 60000/1001 per second. */
#define FRAMES(n) ((uint64_t)(n) * 3003u / 2u)

struct host
{
    hddvd_runtime_bridge *bridge;
    hddvd_runtime_command commands[80];
    char texts[80][16];
    size_t command_count;
    int last_rc;
    size_t events;
};

static void on_event(void *opaque, const hddvd_event *event)
{
    struct host *host = (struct host *)opaque;
    host->events++;
    host->last_rc = hddvd_runtime_bridge_event(host->bridge, event);
}

static void on_command(void *opaque, const hddvd_runtime_command *command)
{
    struct host *host = (struct host *)opaque;
    assert(host->command_count < 80);
    host->commands[host->command_count] = *command;
    /* The text is borrowed until acknowledged; keep a copy to check. */
    strncpy(host->texts[host->command_count], command->text, 15);
    host->command_count++;
}

static void ok(hddvd_runtime *runtime, const char *source)
{
    int rc = hddvd_runtime_evaluate(runtime, "test.js", source, strlen(source));
    if (rc)
    {
        hddvd_runtime_diagnostic d;
        hddvd_runtime_get_diagnostic(runtime, &d);
        fprintf(stderr, "script failed (%d): %s\n%s\n", rc, d.message ? d.message : "", source);
    }
    assert(rc == HDDVD_OK);
}

static hddvd_event event_at(hddvd_event_type type, uint64_t ticks)
{
    hddvd_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.when.ticks = ticks;
    return e;
}

static void post(hddvd_nav *nav, const hddvd_event *e)
{
    assert(hddvd_nav_post(nav, e) == HDDVD_OK);
}

static void run_to(hddvd_nav *nav, uint64_t ticks)
{
    hddvd_time t;
    t.ticks = ticks;
    assert(hddvd_nav_advance(nav, t) == HDDVD_OK);
    hddvd_nav_pump(nav, 0);
}

/* A clock event, delivered. */
static void tick(hddvd_nav *nav, uint64_t ticks)
{
    hddvd_event e = event_at(HDDVD_EVENT_CLOCK, ticks);
    post(nav, &e);
    run_to(nav, ticks);
}

static void ack(hddvd_nav *nav, uint64_t ticks, uint64_t id, hddvd_command_result result)
{
    hddvd_event e = event_at(HDDVD_EVENT_SCRIPT_ACK, ticks);
    e.data.acknowledgement.command_id = id;
    e.data.acknowledgement.result = result;
    post(nav, &e);
}

static void flow(void)
{
    struct host host;
    hddvd_runtime *runtime = NULL;
    hddvd_xpl *xpl = NULL;
    hddvd_nav *nav = NULL;
    hddvd_event e;
    hddvd_runtime_issue issue;

    memset(&host, 0, sizeof(host));
    assert(hddvd_runtime_create(&runtime) == HDDVD_OK);
    assert(hddvd_xpl_open(playlist, strlen(playlist), &xpl) == HDDVD_OK);
    assert(hddvd_runtime_load_playlist(runtime, xpl) == HDDVD_OK);
    assert(hddvd_runtime_bridge_create(runtime, on_command, &host, &host.bridge) == HDDVD_OK);
    assert(hddvd_nav_create(NULL, on_event, NULL, &host, &nav) == HDDVD_OK);

    ok(runtime,
       "function expect(c, what) { if (!c) throw new Error('expected ' + what); }"
       "var log = [];"
       "function note(evt) {"
       "  var t = Player.playlist.currentTitle;"
       "  log.push(evt.type + ':' + (t ? t.attributes.id : '-') + ':' +"
       "           Player.playlist.currentChapter.number + ':' + Player.playlist.playState + ':' +"
       "           Player.track.currentAudioTrackNumber + ':' +"
       "           Player.track.currentSubtitleTrackNumber);"
       "}"
       "var names = ['title_begin', 'title_end', 'chapter', 'play_state', 'audio_track',"
       "             'subtitle_track'];"
       "for (var i = 0; i < names.length; i++) addEventListener(names[i], note, false);"
       "addEventListener('controller_key_down', function (evt) {"
       "  log.push('key:' + evt.key);"
       "  if (evt.key == 13) Player.playlist.titles.Feature.chapters[1].jump('00:00:01:00');"
       "}, false);"
       "var ticks = 0;"
       "var timer = createTimer('00:00:00:30', 1, function () { ticks++; });"
       "timer.enabled = true;");

    /* Title 1 begins, then plays; a chapter and two track changes follow. */
    e = event_at(HDDVD_EVENT_TITLE_BEGIN, 0);
    e.data.position.title = 1;
    post(nav, &e);
    e = event_at(HDDVD_EVENT_PLAYBACK_STATE, 0);
    e.data.playback_state = HDDVD_PLAYBACK_PLAYING;
    post(nav, &e);
    e = event_at(HDDVD_EVENT_CHAPTER, FRAMES(10));
    e.data.position.chapter = 2;
    post(nav, &e);
    e = event_at(HDDVD_EVENT_STREAM_SELECTION, FRAMES(10));
    e.data.stream.stream_type = HDDVD_STREAM_TYPE_AUDIO;
    e.data.stream.stream_id = 2;
    post(nav, &e);
    e = event_at(HDDVD_EVENT_STREAM_SELECTION, FRAMES(10));
    e.data.stream.stream_type = HDDVD_STREAM_TYPE_SUBTITLE;
    e.data.stream.stream_id = 1;
    post(nav, &e);
    /* Another stream type changes nothing scripts see. */
    e = event_at(HDDVD_EVENT_STREAM_SELECTION, FRAMES(10));
    e.data.stream.stream_type = HDDVD_STREAM_TYPE_OTHER;
    post(nav, &e);
    run_to(nav, FRAMES(10));
    assert(host.events == 6 && host.last_rc == HDDVD_OK);
    ok(runtime, "expect(log.join() == 'title_begin:Feature:0:0:0:0,play_state:Feature:0:1:0:0,"
                "chapter:Feature:2:1:0:0,audio_track:Feature:2:1:2:0,"
                "subtitle_track:Feature:2:1:2:1', 'player events ' + log.join());"
                "expect(Player.playlist.currentTitle.elapsedTime == '00:00:00:10', 'elapsed');"
                "expect(ticks == 0, 'no tick yet'); log = [];");

    /* Script timers follow the scheduler clock: a clock event at 40 frames
       fires the 30-frame timer. */
    tick(nav, FRAMES(40));
    ok(runtime, "expect(ticks == 1, 'one tick ' + ticks);"
                "expect(Player.playlist.currentTitle.elapsedTime == '00:00:00:40', 'elapsed');");

    /* A seek back does not move script time backwards or forwards. */
    {
        hddvd_time t;
        t.ticks = FRAMES(4);
        assert(hddvd_nav_discontinuity(nav, t) == HDDVD_OK);
        hddvd_nav_pump(nav, 0);
        ok(runtime, "expect(ticks == 1, 'still one tick');"
                    "expect(Player.playlist.currentTitle.elapsedTime == '00:00:00:04', 'seek');");
        /* Script time is 40 frames; having fired late, the timer is next
           due at 70. */
        tick(nav, FRAMES(32));
        ok(runtime, "expect(ticks == 1, 'one tick at 68');");
        tick(nav, FRAMES(34));
        ok(runtime, "expect(ticks == 2, 'two ticks ' + ticks); timer.enabled = false;");
    }

    /* Enter makes a jump command; the host gets it after the key task. */
    e = event_at(HDDVD_EVENT_INPUT, FRAMES(36));
    e.data.input.key = 13;
    e.data.input.pressed = 1;
    post(nav, &e);
    run_to(nav, FRAMES(36));
    assert(host.command_count == 1);
    assert(host.commands[0].type == HDDVD_RUNTIME_COMMAND_JUMP);
    assert(host.commands[0].title == 1);
    assert(host.commands[0].time.ticks == FRAMES(18000 + 60));
    ok(runtime, "expect(log.join() == 'key:13', 'key ' + log.join()); log = [];");
    assert(hddvd_runtime_command_count(runtime) == 0);

    /* The acknowledgement goes back through the queue; a rejection is an
       issue, and an unknown id is an error. */
    ack(nav, FRAMES(36), host.commands[0].id, HDDVD_COMMAND_REJECTED);
    run_to(nav, FRAMES(36));
    assert(host.last_rc == HDDVD_OK);
    assert(hddvd_runtime_issue_count(runtime) == 1);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_OK);
    assert(issue.kind == HDDVD_RUNTIME_ISSUE_COMMAND);
    hddvd_runtime_clear_issues(runtime);
    ack(nav, FRAMES(36), host.commands[0].id, HDDVD_COMMAND_ACCEPTED);
    run_to(nav, FRAMES(36));
    assert(host.last_rc == HDDVD_EINVAL);

    /* More than 64 commands: the rest wait until acknowledgements arrive,
       and keep their order. */
    host.command_count = 0;
    ok(runtime, "for (var i = 1; i <= 64; i++) Player.track.selectAudioTrackNumber(1 + i % 2);");
    tick(nav, FRAMES(38));
    assert(host.command_count == 64);
    ok(runtime, "for (var i = 1; i <= 6; i++) Player.track.selectAudioTrackNumber(1);");
    tick(nav, FRAMES(38));
    assert(host.command_count == 64);
    assert(hddvd_runtime_command_count(runtime) == 6);
    {
        size_t i;
        for (i = 0; i < 6; i++)
            ack(nav, FRAMES(38), host.commands[i].id, HDDVD_COMMAND_ACCEPTED);
        run_to(nav, FRAMES(38));
        assert(host.command_count == 70);
        assert(hddvd_runtime_command_count(runtime) == 0);
        for (i = 1; i < 70; i++)
            assert(host.commands[i].id > host.commands[i - 1].id);
        for (i = 6; i < 70; i++)
            ack(nav, FRAMES(38), host.commands[i].id, HDDVD_COMMAND_ACCEPTED);
        run_to(nav, FRAMES(38));
        assert(host.last_rc == HDDVD_OK && hddvd_runtime_issue_count(runtime) == 0);
    }

    /* Title end, then a title the playlist lacks. */
    e = event_at(HDDVD_EVENT_TITLE_END, FRAMES(40));
    post(nav, &e);
    run_to(nav, FRAMES(40));
    ok(runtime,
       "expect(log.join() == 'title_end:Feature:2:1:2:1', 'end ' + log.join()); log = [];");
    e = event_at(HDDVD_EVENT_TITLE_BEGIN, FRAMES(40));
    e.data.position.title = 9;
    post(nav, &e);
    run_to(nav, FRAMES(40));
    assert(host.last_rc == HDDVD_EINVAL);
    ok(runtime, "expect(log.length == 0, 'no event for a bad title');");

    /* A throwing listener is reported but the event is still handled. */
    ok(runtime, "addEventListener('chapter', function () { throw new Error('boom'); }, false);");
    e = event_at(HDDVD_EVENT_CHAPTER, FRAMES(40));
    e.data.position.chapter = 1;
    post(nav, &e);
    run_to(nav, FRAMES(40));
    assert(host.last_rc == HDDVD_ESCRIPT);
    ok(runtime, "expect(Player.playlist.currentChapter.number == 1, 'chapter kept');");

    hddvd_nav_destroy(nav);
    hddvd_runtime_bridge_destroy(host.bridge);
    hddvd_runtime_destroy(runtime);
    hddvd_xpl_close(xpl);
}

/* With no callback, the bridge rejects commands itself. */
static void no_callback(void)
{
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_bridge *bridge = NULL;
    hddvd_event e = event_at(HDDVD_EVENT_CLOCK, 0);
    hddvd_runtime_issue issue;
    assert(hddvd_runtime_create(&runtime) == HDDVD_OK);
    assert(hddvd_runtime_bridge_create(runtime, NULL, NULL, &bridge) == HDDVD_OK);
    ok(runtime, "Player.playlist.pause();");
    assert(hddvd_runtime_bridge_event(bridge, &e) == HDDVD_OK);
    assert(hddvd_runtime_command_count(runtime) == 0);
    assert(hddvd_runtime_issue_count(runtime) == 1);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_OK);
    assert(issue.kind == HDDVD_RUNTIME_ISSUE_COMMAND);
    assert(hddvd_runtime_bridge_event(bridge, NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_bridge_event(NULL, &e) == HDDVD_EINVAL);
    hddvd_runtime_bridge_destroy(bridge);
    hddvd_runtime_destroy(runtime);
}

int main(void)
{
    hddvd_runtime_bridge *bridge = (hddvd_runtime_bridge *)&bridge;
    assert(hddvd_runtime_bridge_create(NULL, NULL, NULL, &bridge) == HDDVD_EINVAL);
    assert(bridge == NULL);
    assert(hddvd_runtime_bridge_create(NULL, NULL, NULL, NULL) == HDDVD_EINVAL);
    hddvd_runtime_bridge_destroy(NULL);
    if (!hddvd_runtime_engine())
    {
        puts("no script engine; bridge not tested");
        return 0;
    }
    flow();
    no_callback();
    return 0;
}
