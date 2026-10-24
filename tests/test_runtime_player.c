#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* HDi Player and application host objects: playlist titles and chapters,
   player state, the command queue and its acknowledgements, applications,
   general parameters, Diagnostics, events with fields, and the stubs. */

/* Three titles: Menu, Feature (two chapters, two audio tracks and a
   subpicture track) and one without an id. */
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
    "<Title titleDuration='00:00:05:00'/>"
    "</TitleSet></Playlist>";

/* 90 kHz ticks of n frames at 60000/1001 per second. */
#define FRAMES(n) ((uint64_t)(n) * 3003u / 2u)

static int run(hddvd_runtime *runtime, const char *source)
{
    return hddvd_runtime_evaluate(runtime, "test.js", source, strlen(source));
}

static void ok(hddvd_runtime *runtime, const char *source)
{
    int rc = run(runtime, source);
    if (rc)
    {
        hddvd_runtime_diagnostic d;
        hddvd_runtime_get_diagnostic(runtime, &d);
        fprintf(stderr, "script failed (%d): %s\n%s\n", rc, d.message ? d.message : "", source);
    }
    assert(rc == HDDVD_OK);
}

static int has(const char *text, const char *part)
{
    return text && strstr(text, part) != NULL;
}

static hddvd_runtime *create(void)
{
    hddvd_runtime *runtime = NULL;
    assert(hddvd_runtime_create(&runtime) == HDDVD_OK);
    ok(runtime, "function expect(c, what) { if (!c) throw new Error('expected ' + what); }"
                "function throws(f, pattern) {"
                "  try { f(); } catch (e) { return pattern.test(e.message); } return false; }");
    return runtime;
}

static hddvd_xpl *open_xpl(void)
{
    hddvd_xpl *xpl = NULL;
    assert(hddvd_xpl_open(playlist, strlen(playlist), &xpl) == HDDVD_OK);
    return xpl;
}

static hddvd_runtime_command take(hddvd_runtime *runtime)
{
    hddvd_runtime_command command;
    assert(hddvd_runtime_take_command(runtime, &command) == HDDVD_OK);
    return command;
}

static void titles(void)
{
    hddvd_runtime *runtime = create();
    hddvd_xpl *xpl = open_xpl();
    ok(runtime, "expect(Player.playlist.titles.length === 0, 'no playlist');"
                "expect(Player.playlist.currentTitle === null, 'no title');");
    assert(hddvd_runtime_load_playlist(runtime, xpl) == HDDVD_OK);
    ok(runtime,
       "var t = Player.playlist.titles;"
       "expect(t.length === 3 && t.Menu === t[0] && t['Feature'] === t[1], 'titles by id');"
       "expect(t[1].attributes.id === 'Feature' && t[1].attributes.titleNumber === 2, 'id');"
       "expect(t[1].attributes.titleDuration === '00:10:00:00', 'duration');"
       "expect(t[1].chapters.length === 2 && t[1].chapters[1].number === 2, 'chapters');"
       "expect(t[0].chapters.length === 0, 'no chapters');"
       "expect(t[2].attributes.id === null, 'a title without an id');"
       "expect(t[1].elapsedTime === null, 'not playing');"
       "var old = t[1];");
    /* A new playlist replaces the titles; old title objects throw. */
    assert(hddvd_runtime_load_playlist(runtime, xpl) == HDDVD_OK);
    ok(runtime, "expect(Player.playlist.titles[1] !== old, 'new objects');"
                "expect(throws(function () { old.jump('00:00:00:00'); }, /no longer/), 'stale');"
                "expect(throws(function () { old.attributes.id; }, /no longer/), 'stale id');");
    assert(hddvd_runtime_command_count(runtime) == 0);
    assert(hddvd_runtime_load_playlist(runtime, NULL) == HDDVD_OK);
    ok(runtime, "expect(Player.playlist.titles.length === 0, 'removed');");
    assert(hddvd_runtime_load_playlist(NULL, xpl) == HDDVD_EINVAL);
    hddvd_runtime_destroy(runtime);
    hddvd_xpl_close(xpl);
}

static void state(void)
{
    hddvd_runtime *runtime = create();
    hddvd_xpl *xpl = open_xpl();
    hddvd_runtime_player player;
    assert(hddvd_runtime_load_playlist(runtime, xpl) == HDDVD_OK);
    memset(&player, 0, sizeof(player));
    player.title = 1;
    player.elapsed.ticks = FRAMES(60 * 75 + 30);
    player.chapter = 2;
    player.state = HDDVD_PLAYBACK_PLAYING;
    player.audio_track = 2;
    player.subtitle_track = 1;
    player.subtitle_visible = 1;
    strcpy(player.menu_language, "en");
    assert(hddvd_runtime_set_player(runtime, &player) == HDDVD_OK);
    ok(runtime,
       "var p = Player.playlist, t = p.titles;"
       "expect(p.currentTitle === t.Feature, 'current title');"
       "expect(p.currentTitle.elapsedTime === '00:01:15:30', p.currentTitle.elapsedTime);"
       "expect(t.Menu.elapsedTime === null, 'only the current title has a time');"
       "expect(p.currentChapter.number === 2, 'chapter');"
       "expect(p.playState === p.PLAYSTATE_PLAY, 'playing');"
       "expect(Player.track.currentAudioTrackNumber === 2, 'audio');"
       "expect(Player.track.selectedAudioLanguageCode === 'fr:00', 'audio language');"
       "expect(Player.track.currentSubtitleTrackNumber === 1, 'subtitle');"
       "expect(Player.track.selectedSubtitleLanguageCode === 'de:00', 'subtitle language');"
       "expect(Player.subtitle.visible === true, 'subtitles shown');"
       "expect(Player.menuLanguage === 'en', 'menu language');");
    player.state = HDDVD_PLAYBACK_PAUSED;
    player.audio_track = 9;
    assert(hddvd_runtime_set_player(runtime, &player) == HDDVD_OK);
    ok(runtime, "expect(Player.playlist.playState === Player.playlist.PLAYSTATE_PAUSE, 'paused');"
                "expect(Player.track.selectedAudioLanguageCode === null, 'no such track');");
    /* Invalid reports change nothing. */
    player.title = 3;
    assert(hddvd_runtime_set_player(runtime, &player) == HDDVD_EINVAL);
    player.title = SIZE_MAX;
    player.state = (hddvd_playback_state)7;
    assert(hddvd_runtime_set_player(runtime, &player) == HDDVD_EINVAL);
    player.state = HDDVD_PLAYBACK_STOPPED;
    memset(player.menu_language, 'x', sizeof(player.menu_language));
    assert(hddvd_runtime_set_player(runtime, &player) == HDDVD_EINVAL);
    assert(hddvd_runtime_set_player(runtime, NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_set_player(NULL, &player) == HDDVD_EINVAL);
    ok(runtime, "expect(Player.playlist.currentTitle === Player.playlist.titles.Feature, 'kept');");
    /* Loading a playlist forgets the reported title. */
    assert(hddvd_runtime_load_playlist(runtime, xpl) == HDDVD_OK);
    ok(runtime, "expect(Player.playlist.currentTitle === null, 'forgotten');");
    hddvd_runtime_destroy(runtime);
    hddvd_xpl_close(xpl);
}

static void commands(void)
{
    hddvd_runtime *runtime = create();
    hddvd_xpl *xpl = open_xpl();
    hddvd_runtime_command command;
    hddvd_runtime_issue issue;
    uint64_t first;
    size_t i;
    assert(hddvd_runtime_load_playlist(runtime, xpl) == HDDVD_OK);
    ok(runtime, "var t = Player.playlist.titles;"
                "t.Feature.jump('00:00:01:00', false);"
                "t.Feature.chapters[1].jump('00:00:00:30', false);"
                "Player.playlist.pause(); Player.playlist.play();"
                "Player.track.selectAudioTrackNumber(2);"
                "Player.track.selectSubtitleTrackNumber('1');"
                "Player.track.selectAudioLanguage('fr', 1);"
                "Player.track.selectSubtitleLanguage('de');"
                "Player.subtitle.visible = true;");
    assert(hddvd_runtime_command_count(runtime) == 9);
    command = take(runtime);
    first = command.id;
    assert(command.type == HDDVD_RUNTIME_COMMAND_JUMP && command.title == 1 &&
           command.time.ticks == FRAMES(60) && !strcmp(command.text, ""));
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_JUMP && command.id == first + 1 &&
           command.time.ticks == FRAMES(5 * 3600 + 30));
    assert(take(runtime).type == HDDVD_RUNTIME_COMMAND_PAUSE);
    assert(take(runtime).type == HDDVD_RUNTIME_COMMAND_PLAY);
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_SELECT_AUDIO && command.number == 2);
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_SELECT_SUBTITLE && command.number == 1);
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_SELECT_AUDIO_LANGUAGE &&
           !strcmp(command.text, "fr"));
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_SELECT_SUBTITLE_LANGUAGE &&
           !strcmp(command.text, "de"));
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_SUBTITLE_VISIBLE && command.number == 1);
    assert(hddvd_runtime_take_command(runtime, &command) == HDDVD_ENOTFOUND);
    /* Acknowledgements: accepted ones are silent, others become issues;
       each id once. */
    for (i = 0; i < 8; i++)
        assert(hddvd_runtime_acknowledge(runtime, first + i, HDDVD_COMMAND_ACCEPTED) == HDDVD_OK);
    assert(hddvd_runtime_issue_count(runtime) == 0);
    assert(hddvd_runtime_acknowledge(runtime, first + 8, HDDVD_COMMAND_REJECTED) == HDDVD_OK);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_OK);
    assert(issue.kind == HDDVD_RUNTIME_ISSUE_COMMAND && has(issue.message, "subtitle") &&
           has(issue.message, "rejected"));
    assert(hddvd_runtime_acknowledge(runtime, first + 8, HDDVD_COMMAND_ACCEPTED) == HDDVD_EINVAL);
    assert(hddvd_runtime_acknowledge(runtime, 999, HDDVD_COMMAND_ACCEPTED) == HDDVD_EINVAL);
    assert(hddvd_runtime_acknowledge(NULL, first, HDDVD_COMMAND_ACCEPTED) == HDDVD_EINVAL);
    assert(hddvd_runtime_take_command(runtime, NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_take_command(NULL, &command) == HDDVD_EINVAL);
    assert(hddvd_runtime_command_count(NULL) == 0);
    /* Invalid calls throw and queue nothing. */
    ok(runtime,
       "var t = Player.playlist.titles;"
       "expect(throws(function () { t.Feature.jump('00:10:00:01'); }, /end of the title/),"
       "  'past the end');"
       "expect(throws(function () { t.Feature.jump('1:00'); }, /HH:MM:SS:FF/), 'bad time');"
       "expect(throws(function () { Player.track.selectAudioTrackNumber(0); },"
       "  /track number/), 'track 0');"
       "expect(throws(function () { Player.track.selectAudioLanguage('English'); },"
       "  /language code/), 'bad language');"
       "expect(throws(function () { application.moveToTop(); }, /which application/),"
       "  'no application list');");
    assert(hddvd_runtime_command_count(runtime) == 0);
    /* 64 commands wait to be taken, and 64 taken ones to be acknowledged. */
    ok(runtime, "for (var i = 0; i < 64; i++) Player.playlist.play();"
                "expect(throws(function () { Player.playlist.play(); }, /too many/), 'full');");
    assert(hddvd_runtime_command_count(runtime) == 64);
    for (i = 0; i < 64; i++)
        take(runtime);
    ok(runtime, "Player.playlist.pause();");
    assert(hddvd_runtime_take_command(runtime, &command) == HDDVD_ELIMIT);
    assert(hddvd_runtime_acknowledge(runtime, first + 9, HDDVD_COMMAND_FAILED) == HDDVD_OK);
    assert(take(runtime).type == HDDVD_RUNTIME_COMMAND_PAUSE);
    /* Unacknowledged commands are released with the runtime. */
    hddvd_runtime_destroy(runtime);
    hddvd_xpl_close(xpl);
}

static void applications(void)
{
    static const char *const ids[] = {"menu", NULL, "game"};
    hddvd_runtime *runtime = create();
    hddvd_runtime_command command;
    char *many[64];
    size_t i;
    assert(hddvd_runtime_set_applications(runtime, ids, 3, 0) == HDDVD_OK);
    ok(runtime, "var apps = application.advancedApplications;"
                "expect(apps.length === 3 && apps[1].id === '' && apps[2].id === 'game', 'list');"
                "expect(apps[2].zOrder === 2, 'z-order');"
                "expect(application.thisAdvancedApplication.id === 'menu', 'this');"
                "expect(application.attributes.id === 'menu', 'attributes');"
                "application.moveToTop(); application.moveToBottom();"
                "apps[2].activate(); application.advancedApplications[1].inactivate();");
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_APPLICATION_TO_TOP && command.number == 0 &&
           !strcmp(command.text, "menu"));
    assert(take(runtime).type == HDDVD_RUNTIME_COMMAND_APPLICATION_TO_BOTTOM);
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_ACTIVATE_APPLICATION && command.number == 2 &&
           !strcmp(command.text, "game"));
    command = take(runtime);
    assert(command.type == HDDVD_RUNTIME_COMMAND_INACTIVATE_APPLICATION &&
           !strcmp(command.text, ""));
    /* A runtime that is not in the list has no thisAdvancedApplication. */
    assert(hddvd_runtime_set_applications(runtime, ids, 3, SIZE_MAX) == HDDVD_OK);
    ok(runtime, "expect(application.thisAdvancedApplication === null, 'not listed');"
                "expect(application.attributes.id === null, 'no id');");
    assert(hddvd_runtime_set_applications(runtime, ids, 3, 3) == HDDVD_EINVAL);
    assert(hddvd_runtime_set_applications(runtime, NULL, 3, 0) == HDDVD_EINVAL);
    assert(hddvd_runtime_set_applications(NULL, ids, 3, 0) == HDDVD_EINVAL);
    for (i = 0; i < 64; i++)
        many[i] = "x";
    assert(hddvd_runtime_set_applications(runtime, (const char *const *)many, 64, 0) ==
           HDDVD_EINVAL);
    assert(hddvd_runtime_set_applications(runtime, NULL, 0, SIZE_MAX) == HDDVD_OK);
    ok(runtime, "expect(application.advancedApplications.length === 0, 'empty');");
    hddvd_runtime_destroy(runtime);
}

static void parameters_and_stubs(void)
{
    hddvd_runtime *runtime = create();
    hddvd_runtime_issue issue;
    ok(runtime,
       "var g = Player.generalParameters;"
       "expect(g.getValue('login') === null, 'missing');"
       "g.setValue('login', 'abc'); expect(g.getValue('login') === 'abc', 'set');"
       "g.setValue('__proto__', 'x'); expect(g.getValue('__proto__') === 'x', 'any name');"
       "expect(({}).x === undefined, 'no pollution');"
       "g.setValue('login', undefined); expect(g.getValue('login') === null, 'removed');"
       "expect(throws(function () { FileIO.remove('x', false, function () {}); },"
       "  /FileIO.remove is not available/), 'FileIO');"
       "expect(FileIO.SUCCEEDED !== FileIO.FAILED, 'constants');"
       "expect(throws(function () { XMLParser.parseString('<a/>'); }, /not available/), 'XML');"
       "expect(throws(function () { Network.createHTTPClient('u', Network.HTTP_GET, 1); },"
       "  /not available/), 'network');"
       "expect(PersistentStorageManager.contentId === '', 'content id');"
       "expect(throws(function () { PersistentStorageManager.getPersistentStorageDevices(1); },"
       "  /not available/), 'storage');"
       "expect(Player.capabilities.network.connected === false, 'offline');"
       "expect(throws(function () { Player.audio.main.setVolumes(1); },"
       "  /Player.audio.main.setVolumes is not supported/), 'mixing');"
       "expect(throws(function () { Player.audio.effect.play(); }, /not supported/), 'effect');"
       "expect(throws(function () { Player.standardContentPlayer.getSPRM(16); },"
       "  /not supported/), 'SPRM');"
       "expect(Node.ELEMENT_NODE === 1, 'Node');"
       "expect(throws(function () { document.getProperties(); },"
       "  /document.getProperties is not supported/), 'properties');"
       "Diagnostics.trace('menu ready', Diagnostics.TRACE_LEVEL_INFO);");
    assert(hddvd_runtime_command_count(runtime) == 0);
    assert(hddvd_runtime_issue_count(runtime) == 1);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_OK);
    assert(issue.kind == HDDVD_RUNTIME_ISSUE_TRACE && !strcmp(issue.message, "menu ready"));
    hddvd_runtime_destroy(runtime);
}

static void events_with_fields(void)
{
    static const char *const names[] = {"oldValue", "newValue"};
    static const char *const values[] = {"1", "2"};
    static const char *const reserved[] = {"type"};
    static const char *const digit[] = {"1x"};
    static const char *const empty[] = {""};
    const char *many[17];
    size_t i;
    hddvd_runtime *runtime = create();
    ok(runtime,
       "var seen = '';"
       "addEventListener('play_state', function (e) {"
       "  seen = e.type + ':' + e.oldValue + '>' + e.newValue;"
       "  if (e.newValue == Player.playlist.PLAYSTATE_PAUSE) seen += ':paused'; }, false);");
    assert(hddvd_runtime_dispatch_fields(runtime, "play_state", names, values, 2) == HDDVD_OK);
    ok(runtime, "expect(seen === 'play_state:1>2:paused', seen);");
    assert(hddvd_runtime_dispatch_fields(runtime, "play_state", NULL, NULL, 0) == HDDVD_OK);
    ok(runtime, "expect(seen === 'play_state:undefined>undefined', seen);");
    assert(hddvd_runtime_dispatch_fields(runtime, "x", reserved, values, 1) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch_fields(runtime, "x", digit, values, 1) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch_fields(runtime, "x", empty, values, 1) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch_fields(runtime, "x", names, NULL, 2) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch_fields(runtime, "", names, values, 2) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch_fields(NULL, "x", names, values, 2) == HDDVD_EINVAL);
    for (i = 0; i < 17; i++)
        many[i] = "a";
    assert(hddvd_runtime_dispatch_fields(runtime, "x", many, many, 17) == HDDVD_EINVAL);
    /* Fields belong to that one dispatch. */
    assert(hddvd_runtime_dispatch(runtime, "play_state") == HDDVD_OK);
    ok(runtime, "expect(seen === 'play_state:undefined>undefined', seen);");
    hddvd_runtime_destroy(runtime);
}

int main(void)
{
    if (!hddvd_runtime_engine())
    {
        hddvd_runtime_command command;
        assert(hddvd_runtime_take_command(NULL, &command) == HDDVD_EINVAL);
        assert(hddvd_runtime_load_playlist(NULL, NULL) == HDDVD_EINVAL);
        puts("no script engine; player objects not tested");
        return 0;
    }
    titles();
    state();
    commands();
    applications();
    parameters_and_stubs();
    events_with_fields();
    return 0;
}
