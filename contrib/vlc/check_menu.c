/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
/* Headless libvlc checks of the HD DVD module on an Advanced Content disc
   with a menu (make_ac_disc.py): the menu application runs, VLC's
   navigation keys and the mouse reach it, and its commands are carried out:
   pause and play, track choices, subtitle visibility, application order,
   and a jump that changes the title. Mouse events come through the testwin
   window plugin (testwin.c, found through VLC_PLUGIN_PATH), which hands
   itself to check_window below. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <vlc/vlc.h>

/* Microseconds in n frames at 30000/1001. */
#define FRAME_US(n) ((libvlc_time_t)(n) * 100100 / 3)
#define COPY "vlc-plugin-disc"

static char messages[1 << 16];
static size_t messages_used;
static char error_title[256];
/* VLC logs and reports errors from its own threads. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void on_log(void *data, int level, const libvlc_log_t *ctx, const char *format,
                   va_list args)
{
    const char *module = NULL, *file = NULL;
    unsigned line;
    int n;
    (void)data;
    (void)level;
    libvlc_log_get_context(ctx, &module, &file, &line);
    if (!module || strcmp(module, "hddvd"))
        return;
    pthread_mutex_lock(&lock);
    if (messages_used >= sizeof(messages) - 2)
    {
        pthread_mutex_unlock(&lock);
        return;
    }
    n = vsnprintf(messages + messages_used, sizeof(messages) - messages_used - 1, format, args);
    if (n > 0)
        messages_used += (size_t)n < sizeof(messages) - messages_used - 1
                             ? (size_t)n
                             : sizeof(messages) - messages_used - 2;
    messages[messages_used++] = '\n';
    messages[messages_used] = '\0';
    pthread_mutex_unlock(&lock);
}

static void on_error(void *data, const char *title, const char *text)
{
    (void)data;
    (void)text;
    /* Only the plugin's own; a VLC built without decoders reports those. */
    if (!strstr(title, "HD DVD"))
        return;
    pthread_mutex_lock(&lock);
    snprintf(error_title, sizeof(error_title), "%s", title);
    pthread_mutex_unlock(&lock);
}

/* The window the video is in, from the testwin plugin. */
static void *window;
static unsigned window_width, window_height;
static void (*mouse_move)(void *, int, int);
static void (*mouse_press)(void *, int);
static void (*mouse_release)(void *, int);

void check_window(void *w, unsigned width, unsigned height, void (*move)(void *, int, int),
                  void (*press)(void *, int), void (*release)(void *, int));
void check_window(void *w, unsigned width, unsigned height, void (*move)(void *, int, int),
                  void (*press)(void *, int), void (*release)(void *, int))
{
    pthread_mutex_lock(&lock);
    window = w;
    window_width = width;
    window_height = height;
    mouse_move = move;
    mouse_press = press;
    mouse_release = release;
    pthread_mutex_unlock(&lock);
}

static void pause_ms(long ms)
{
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

/* Polls until check() holds, for up to 10 s. */
static bool wait_for(bool (*check)(libvlc_media_player_t *), libvlc_media_player_t *mp)
{
    for (int i = 0; i < 1000; i++)
    {
        if (check(mp))
            return true;
        pause_ms(10);
    }
    return false;
}

static bool has_titles(libvlc_media_player_t *mp)
{
    return libvlc_media_player_get_title_count(mp) == 2;
}

static bool stopped(libvlc_media_player_t *mp)
{
    libvlc_state_t state = libvlc_media_player_get_state(mp);
    return state == libvlc_Stopped || state == libvlc_Error;
}

static bool contains(const char *haystack, const char *text)
{
    bool found;
    pthread_mutex_lock(&lock);
    found = strstr(haystack, text) != NULL;
    pthread_mutex_unlock(&lock);
    return found;
}

/* Waits up to 10 s for a message from the plugin. */
static bool logged(const char *text)
{
    for (int i = 0; i < 1000; i++)
    {
        if (contains(messages, text))
            return true;
        pause_ms(10);
    }
    return false;
}

static libvlc_media_player_t *open_disc(libvlc_instance_t *vlc, const char *root, bool paused)
{
    char mrl[1024];
    libvlc_media_t *media;
    libvlc_media_player_t *mp;
    snprintf(mrl, sizeof(mrl), "hddvd://%s", root);
    media = libvlc_media_new_location(mrl);
    assert(media);
    if (paused)
        libvlc_media_add_option(media, ":start-paused");
    mp = libvlc_media_player_new_from_media(vlc, media, NULL, NULL);
    libvlc_media_release(media);
    assert(mp && libvlc_media_player_play(mp) == 0);
    return mp;
}


static bool has_window(libvlc_media_player_t *mp)
{
    bool found;
    (void)mp;
    pthread_mutex_lock(&lock);
    found = window != NULL;
    pthread_mutex_unlock(&lock);
    return found;
}

/* Clicks at a point of the 1920x1080 menu, which the module letterboxes
   into the video, which fills the window. */
static void click(int x, int y)
{
    double scale;
    int wx, wy;
    pthread_mutex_lock(&lock);
    scale = (double)window_width / 1920 < (double)window_height / 1080
                ? (double)window_width / 1920
                : (double)window_height / 1080;
    wx = (int)((window_width - 1920 * scale) / 2 + x * scale);
    wy = (int)((window_height - 1080 * scale) / 2 + y * scale);
    mouse_move(window, wx, wy);
    pause_ms(100);
    mouse_press(window, 0);
    pause_ms(100);
    mouse_release(window, 0);
    pthread_mutex_unlock(&lock);
}

/* Moves the mouse to a point of the 1920x1080 video without pressing. */
static void hover(int x, int y)
{
    double scale;
    pthread_mutex_lock(&lock);
    scale = (double)window_width / 1920 < (double)window_height / 1080
                ? (double)window_width / 1920
                : (double)window_height / 1080;
    mouse_move(window, (int)((window_width - 1920 * scale) / 2 + x * scale),
               (int)((window_height - 1080 * scale) / 2 + y * scale));
    pthread_mutex_unlock(&lock);
}

static int64_t decoded_frames(libvlc_media_player_t *mp)
{
    libvlc_media_t *media = libvlc_media_player_get_media(mp);
    libvlc_media_stats_t stats;
    int64_t frames = -1;
    if (media && libvlc_media_get_stats(media, &stats))
        frames = stats.i_decoded_video;
    if (media)
        libvlc_media_release(media);
    return frames;
}

/* Whether video is decoded within a second (libvlc's time is extrapolated
   from the clock, so it moves on while nothing is read). */
static bool advancing(libvlc_media_player_t *mp)
{
    int64_t before = decoded_frames(mp);
    pause_ms(1000);
    return decoded_frames(mp) - before > 5;
}

/* Waits up to 10 s for video decoding to stop. */
static bool stalls(libvlc_media_player_t *mp)
{
    for (int i = 0; i < 10; i++)
        if (!advancing(mp))
            return true;
    return false;
}

static bool on_title_1(libvlc_media_player_t *mp)
{
    return libvlc_media_player_get_title(mp) == 1;
}

static bool overlay(libvlc_media_player_t *mp)
{
    (void)mp;
    return contains(messages, "menu overlay shown");
}

/* A failed check shows what the plugin said. */
static void dump(int signal_number)
{
    (void)signal_number;
    fputs(messages, stderr);
}

static void dump_at_exit(void)
{
    dump(0);
}

/* --hddvd-guess on a Standard Content disc whose title menu cell has a
   1920x1080 still (make_std_menu_disc.py) and the buttons of the guesses
   fixture, at 100,300 and 400,300, 200x50, after a first-play cell with a
   2-second still: moving over button 2 selects it, and a click on button 1
   plays title 1. */
static void standard_mouse(const char *root)
{
    static const char *const args[] = {"--vout=vdummy", "--aout=adummy", "--no-video-title-show",
                                       "--verbose=2", "--hddvd-guess"};
    libvlc_instance_t *vlc = libvlc_new(5, args);
    libvlc_media_player_t *mp;
    assert(vlc);
    libvlc_log_set(vlc, on_log, NULL);
    libvlc_dialog_set_error_callback(vlc, on_error, NULL);
    pthread_mutex_lock(&lock);
    messages_used = 0;
    messages[0] = '\0';
    error_title[0] = '\0';
    window = NULL;
    pthread_mutex_unlock(&lock);
    mp = open_disc(vlc, root, false);
    /* First play's still holds for its 2 seconds. */
    assert(logged("navigation: menu cell 1 of PGC 1 (title set 0), still 2, 0 buttons"));
    pause_ms(1500);
    assert(!contains(messages, "still 255"));
    assert(logged("navigation: menu cell 1 of PGC 1 (title set 0), still 255, 2 buttons"));
    assert(wait_for(has_window, mp));
    hover(500, 325);
    assert(logged("menu pointer at 500,325: button 2"));
    assert(logged("menu button 2 selected"));
    assert(!contains(messages, "activated"));
    hover(50, 50);
    assert(logged("menu pointer at 50,50: button 0"));
    click(200, 325);
    assert(logged("menu pointer at 200,325 pressed: button 1"));
    assert(logged("menu button 1 selected"));
    assert(logged("menu button 1 activated"));
    assert(logged("navigation: title 0 cell 1 at offset 0"));
    libvlc_media_player_stop_async(mp);
    assert(wait_for(stopped, mp));
    pthread_mutex_lock(&lock);
    assert(!error_title[0]);
    pthread_mutex_unlock(&lock);
    libvlc_media_player_release(mp);
    libvlc_release(vlc);
}

/* argv: the disc, the same disc with an encrypted second title, and
   optionally "overlay" when the VLC under test can decode the video,
   then optionally the Standard Content menu disc. */
int main(int argc, char **argv)
{
    static const char *const args[] = {"--vout=vdummy", "--aout=adummy", "--no-video-title-show",
                                       "--verbose=2"};
    bool want_overlay = argc >= 4 && !strcmp(argv[3], "overlay");
    libvlc_instance_t *vlc = libvlc_new(4, args);
    libvlc_media_player_t *mp;
    assert(vlc && argc >= 3);
    signal(SIGABRT, dump);
    if (getenv("CHECK_VERBOSE"))
        atexit(dump_at_exit);
    libvlc_log_set(vlc, on_log, NULL);
    libvlc_dialog_set_error_callback(vlc, on_error, NULL);

    /* The menu runs for the first title and draws a bar and two buttons. */
    mp = open_disc(vlc, argv[1], !want_overlay);
    assert(wait_for(has_titles, mp));
    assert(logged("menu file:///dvddisc/ADV_OBJ/menu.xmf runs for title 0"));
    assert(logged("menu changed: 3 items"));
    assert(logged("menu overlay: 3 regions") || !want_overlay);
    if (want_overlay)
    {
        /* The video has an output: the overlay is on it, with its images. */
        assert(wait_for(overlay, mp));
        assert(!contains(messages, "cannot be decoded"));

        /* A click on "other": the application pauses playback and makes
           its choices, which are carried out or refused. */
        assert(wait_for(has_window, mp));
        assert(advancing(mp));
        click(360, 980);
        assert(logged("menu pointer at"));
        assert(logged(" pressed: focus"));
        assert(logged("menu command: pause"));
        assert(logged("menu command: audio track 1 (stream 0)\n"));
        assert(logged("menu command: subtitles hidden"));
        assert(logged("menu command: application order"));
        assert(logged("menu command 9 not carried out"));
        assert(!contains(messages, "menu command 3 not carried out"));
        assert(!contains(messages, "menu command 5 not carried out"));
        assert(stalls(mp));
        /* A second click plays on. */
        click(360, 980);
        assert(logged("menu command: play"));
        assert(advancing(mp));
        /* A click on "play" jumps to the second title, five seconds in. */
        click(860, 980);
    }
    else
    {
        /* Right moves the focus; Enter on "play" jumps to the second
           title, five seconds in. */
        libvlc_media_player_navigate(mp, libvlc_navigate_right);
        assert(logged("menu key 0x27: focus"));
        libvlc_media_player_navigate(mp, libvlc_navigate_activate);
    }
    assert(logged("menu command: jump to title 1"));
    assert(logged("title 1 chapter 0 at offset"));
    /* VLC reports the new title once it is playing again. */
    libvlc_media_player_set_pause(mp, 0);
    assert(wait_for(on_title_1, mp));
    libvlc_media_player_stop_async(mp);
    assert(wait_for(stopped, mp));
    pthread_mutex_lock(&lock);
    assert(!error_title[0]);
    messages_used = 0;
    messages[0] = '\0';
    pthread_mutex_unlock(&lock);
    libvlc_media_player_release(mp);

    /* The same jump into an encrypted title ends with the dialog. */
    mp = open_disc(vlc, argv[2], false);
    assert(logged("runs for title 0"));
    libvlc_media_player_navigate(mp, libvlc_navigate_right);
    assert(logged("menu key 0x27: focus"));
    libvlc_media_player_navigate(mp, libvlc_navigate_activate);
    assert(logged("menu command: jump to title 1"));
    assert(logged("encrypted EVO sector"));
    assert(wait_for(stopped, mp));
    assert(contains(error_title, "Encrypted HD DVD"));
    libvlc_media_player_release(mp);
    libvlc_release(vlc);
    if (argc >= 5)
        standard_mouse(argv[4]);
    return 0;
}
