/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
/* Runs the plugin headless in libvlc over the synthetic disc in
   tests/fixtures/standard (see generate_objects.py): title 1 has chapters at
   0 and 45 frames (byte 24576), title 2 one chapter; 75 and 60 frames at
   30000/1001. Title and chapter changes are checked through the plugin's
   debug messages, which name the byte offset it moved to. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <limits.h>
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


static void forget(void)
{
    pthread_mutex_lock(&lock);
    messages_used = 0;
    messages[0] = '\0';
    error_title[0] = '\0';
    pthread_mutex_unlock(&lock);
}

/* The disc's own navigation: first play leads through the menus to title
   2; the user's title and chapter choices go through the navigator; left
   to play, title 2's post command plays title 1 cell by cell, whose last
   cell returns to first play. */
static void navigation(libvlc_instance_t *vlc)
{
    libvlc_title_description_t **titles;
    libvlc_chapter_description_t **chapters;
    libvlc_media_player_t *mp;
    int count;

    mp = open_disc(vlc, HDDVD_FIXTURE_DIR "/standard", true);
    assert(wait_for(has_titles, mp));
    assert(logged("navigation: title 1 cell 1 at offset 0"));
    count = libvlc_media_player_get_full_title_descriptions(mp, &titles);
    assert(count == 2);
    assert(titles[0]->i_duration == FRAME_US(75) && titles[1]->i_duration == FRAME_US(60));
    libvlc_title_descriptions_release(titles, (unsigned)count);
    count = libvlc_media_player_get_full_chapter_descriptions(mp, 0, &chapters);
    assert(count == 2);
    assert(chapters[0]->i_time_offset == 0 && chapters[1]->i_time_offset == FRAME_US(45));
    libvlc_chapter_descriptions_release(chapters, (unsigned)count);

    libvlc_media_player_set_title(mp, 0);
    assert(logged("navigation: title 0 cell 1 at offset 0"));
    libvlc_media_player_set_chapter(mp, 1);
    assert(logged("navigation: title 0 cell 2 at offset 24576"));

    /* Seeks: the navigator moves to the cell sought, and the disc's
       navigation carries on from there: cell 2 follows cell 1, and the
       end of title 1 returns through first play to title 2. This disc has
       nothing to decode, so it plays unpaced and pausing does not hold it;
       the title choice and the seek after it are sent together, and the
       input carries out both before it reads again. */
    forget();
    libvlc_media_player_set_title(mp, 0);
    libvlc_media_player_set_time(mp, FRAME_US(50), false);
    assert(logged("navigation: seek into title 0 cell 2 at offset"));
    forget();
    libvlc_media_player_set_title(mp, 0);
    libvlc_media_player_set_time(mp, FRAME_US(10), false);
    assert(logged("navigation: seek into title 0 cell 1 at offset"));
    assert(logged("navigation: title 0 cell 2 at offset 24576"));
    assert(logged("navigation: title 1 cell 1 at offset 0"));
    assert(!contains(messages, "seek outside"));
    libvlc_media_player_stop_async(mp);
    assert(wait_for(stopped, mp));
    libvlc_media_player_release(mp);
    forget();

    /* Playing: the cells follow each other as the disc's commands say. */
    mp = open_disc(vlc, HDDVD_FIXTURE_DIR "/standard", false);
    assert(logged("navigation: title 1 cell 1 at offset 0"));
    assert(logged("navigation: title 0 cell 1 at offset 0"));
    assert(logged("navigation: title 0 cell 2 at offset 24576"));
    libvlc_media_player_stop_async(mp);
    assert(wait_for(stopped, mp));
    pthread_mutex_lock(&lock);
    assert(!error_title[0]);
    pthread_mutex_unlock(&lock);
    libvlc_media_player_release(mp);
    forget();
}

/* With --no-hddvd-menu the first title plays through, to its end. */
static void plain(void)
{
    static const char *const args[] = {"--vout=vdummy", "--aout=adummy", "--no-video-title-show",
                                       "--verbose=2", "--no-hddvd-menu"};
    libvlc_instance_t *vlc = libvlc_new(5, args);
    libvlc_media_player_t *mp;
    assert(vlc);
    libvlc_log_set(vlc, on_log, NULL);
    libvlc_dialog_set_error_callback(vlc, on_error, NULL);
    mp = open_disc(vlc, HDDVD_FIXTURE_DIR "/standard", false);
    assert(logged("title 0 chapter 0 at offset 0"));
    pause_ms(300);
    assert(wait_for(stopped, mp));
    assert(!contains(messages, "navigation:"));
    pthread_mutex_lock(&lock);
    assert(!error_title[0]);
    pthread_mutex_unlock(&lock);
    libvlc_media_player_release(mp);
    libvlc_release(vlc);
    forget();
}
static void copy_file(const char *from, const char *to, long scramble_at)
{
    FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
    long at = 0;
    int c;
    assert(in && out);
    while ((c = fgetc(in)) != EOF)
    {
        /* Sets the PES scrambling bits of a video packet. */
        if (at++ == scramble_at)
            c |= 0x10;
        assert(fputc(c, out) != EOF);
    }
    fclose(in);
    assert(!fclose(out));
}

/* A copy of the disc whose title 1 has an encrypted video packet in its
   second sector (the pack header is 14 bytes, then the PES header). */
static void encrypted(libvlc_instance_t *vlc)
{
    static const char *const names[] = {"HV000I01.IFO", "HV001I01.IFO", "HV001T01.EVO"};
    char cwd_root[PATH_MAX], from[1024], to[1024];
    libvlc_media_player_t *mp;

    mkdir(COPY, 0755);
    mkdir(COPY "/HVDVD_TS", 0755);
    for (int i = 0; i < 3; i++)
    {
        snprintf(from, sizeof(from), HDDVD_FIXTURE_DIR "/standard/HVDVD_TS/%s", names[i]);
        snprintf(to, sizeof(to), COPY "/HVDVD_TS/%s", names[i]);
        copy_file(from, to, i == 2 ? 2048 + 20 : -1);
    }
    assert(realpath(COPY, cwd_root));
    mp = open_disc(vlc, cwd_root, false);
    /* A new player reads as stopped until its input starts. */
    assert(logged("encrypted EVO sector"));
    assert(wait_for(stopped, mp));
    assert(contains(error_title, "Encrypted HD DVD"));
    libvlc_media_player_release(mp);
}

/* A copy of the guesses fixture, with its title menu's scrambled audio
   packet left as it is or made clear. */
#define GUESS_COPY "vlc-plugin-guesses"
static void copy_guesses(bool clear)
{
    static const char *const names[] = {"HV000I01.IFO", "HV000M01.EVO", "HV001I01.IFO",
                                        "HV001M01.EVO", "HV001T01.EVO"};
    char from[1024], to[1024];
    mkdir(GUESS_COPY, 0755);
    mkdir(GUESS_COPY "/HVDVD_TS", 0755);
    for (int i = 0; i < 5; i++)
    {
        snprintf(from, sizeof(from), HDDVD_FIXTURE_DIR "/guesses/HVDVD_TS/%s", names[i]);
        snprintf(to, sizeof(to), GUESS_COPY "/HVDVD_TS/%s", names[i]);
        copy_file(from, to, -1);
    }
    if (clear)
    {
        FILE *f = fopen(GUESS_COPY "/HVDVD_TS/HV000M01.EVO", "r+b");
        int c;
        assert(f && !fseek(f, 2 * 2048 + 20, SEEK_SET));
        c = fgetc(f);
        assert(c == 0x91);
        assert(!fseek(f, 2 * 2048 + 20, SEEK_SET) && fputc(c & 0xcf, f) != EOF);
        assert(!fclose(f));
    }
}

/* --hddvd-guess on the guesses fixture: first play's menu cell, then the
   title menu, which holds (a still until a button is activated); VLC's
   navigation keys move between its buttons and press them. Its button 2
   links to PGC 2, whose button 2 does nothing and whose button 1 goes back
   up; the title menu's button 1 plays title 1, whose angle block plays its
   first cell only. Without the guesses, the first-play cell is refused. */
static void guesses(void)
{
    static const char *const args[] = {"--vout=vdummy", "--aout=adummy", "--no-video-title-show",
                                       "--verbose=2", "--hddvd-guess"};
    libvlc_instance_t *vlc = libvlc_new(5, args);
    libvlc_media_player_t *mp;
    char root[PATH_MAX];
    assert(vlc);
    libvlc_log_set(vlc, on_log, NULL);
    libvlc_dialog_set_error_callback(vlc, on_error, NULL);

    copy_guesses(true);
    assert(realpath(GUESS_COPY, root));
    mp = open_disc(vlc, root, false);
    assert(logged("guessing the disc's menu cells"));
    assert(logged("navigation: menu cell 1 of PGC 1 (title set 0), still 0, 0 buttons"));
    assert(logged("navigation: menu cell 1 of PGC 1 (title set 0), still 255, 2 buttons"));
    /* The still holds: nothing follows the menu by itself. A menu cell has
       no length, and cannot be sought in. */
    pause_ms(500);
    assert(libvlc_media_player_get_length(mp) == 0);
    assert(!contains(messages, "navigation: title"));
    assert(!contains(messages, "navigation ended"));

    libvlc_media_player_navigate(mp, libvlc_navigate_down);
    assert(logged("menu button 2 selected"));
    forget();
    libvlc_media_player_navigate(mp, libvlc_navigate_activate);
    assert(logged("menu button 2 activated"));
    assert(logged("navigation: menu cell 1 of PGC 2 (title set 0), still 255, 2 buttons"));
    forget();
    libvlc_media_player_navigate(mp, libvlc_navigate_activate);
    assert(logged("menu button 2 activated"));
    pause_ms(300);
    assert(!contains(messages, "navigation:"));
    libvlc_media_player_navigate(mp, libvlc_navigate_left);
    assert(logged("menu button 1 selected"));
    libvlc_media_player_navigate(mp, libvlc_navigate_activate);
    assert(logged("menu button 1 activated"));
    assert(logged("navigation: menu cell 1 of PGC 1 (title set 0), still 255, 2 buttons"));
    forget();
    libvlc_media_player_navigate(mp, libvlc_navigate_activate);
    assert(logged("menu button 1 activated"));
    assert(logged("navigation: title 0 cell 1 at offset 0"));
    assert(logged("navigation: title 0 cell 2 at offset 16384"));
    assert(logged("navigation: title 0 cell 4 at offset 32768"));
    assert(!contains(messages, "cell 3 at offset"));
    /* PGC 1's next PGC, whose cell links back to PGC 1. */
    assert(logged("navigation: title 0 cell 1 at offset 40960"));
    forget();
    assert(logged("navigation: title 0 cell 1 at offset 0"));
    libvlc_media_player_stop_async(mp);
    assert(wait_for(stopped, mp));
    pthread_mutex_lock(&lock);
    assert(!error_title[0]);
    pthread_mutex_unlock(&lock);
    libvlc_media_player_release(mp);
    forget();

    /* The title menu's scrambled packet cannot be read. */
    copy_guesses(false);
    mp = open_disc(vlc, root, false);
    assert(logged("navigation: menu cell 1 of PGC 1 (title set 0), still 255, 2 buttons"));
    assert(wait_for(stopped, mp));
    assert(contains(error_title, "Encrypted HD DVD"));
    libvlc_media_player_release(mp);
    libvlc_release(vlc);
    forget();
}

/* Without --hddvd-guess, the guesses fixture's first play is refused and
   its first title plays every cell of the angle block. */
static void no_guesses(libvlc_instance_t *vlc)
{
    char root[PATH_MAX];
    libvlc_media_player_t *mp;
    copy_guesses(true);
    assert(realpath(GUESS_COPY, root));
    mp = open_disc(vlc, root, false);
    assert(logged("disc navigation not followed: menu cells are not decoded"));
    assert(logged("title 0 chapter 0 at offset 0"));
    assert(wait_for(stopped, mp));
    assert(!contains(messages, "navigation: menu cell"));
    libvlc_media_player_release(mp);
    forget();
}

/* Opening the disc directory itself finds the titles too. */
static void directory(libvlc_instance_t *vlc)
{
    libvlc_media_t *media = libvlc_media_new_path(HDDVD_FIXTURE_DIR "/standard");
    libvlc_media_player_t *mp;
    assert(media);
    libvlc_media_add_option(media, ":start-paused");
    mp = libvlc_media_player_new_from_media(vlc, media, NULL, NULL);
    libvlc_media_release(media);
    assert(mp && libvlc_media_player_play(mp) == 0);
    assert(wait_for(has_titles, mp));
    libvlc_media_player_stop_async(mp);
    assert(wait_for(stopped, mp));
    libvlc_media_player_release(mp);
}

/* hddvd:// on a directory without a title table opens nothing. */
static void not_a_disc(libvlc_instance_t *vlc)
{
    libvlc_media_player_t *mp = open_disc(vlc, HDDVD_FIXTURE_DIR "/app", false);
    assert(logged("no Standard Content title table"));
    assert(wait_for(stopped, mp));
    libvlc_media_player_release(mp);
}

/* A failed check shows what the plugin said. */
static void dump(int signal_number)
{
    (void)signal_number;
    fputs(messages, stderr);
}

int main(void)
{
    static const char *const args[] = {"--vout=vdummy", "--aout=adummy", "--no-video-title-show",
                                       "--verbose=2"};
    libvlc_instance_t *vlc;
    signal(SIGABRT, dump);
    plain();
    guesses();
    vlc = libvlc_new(4, args);
    assert(vlc);
    libvlc_log_set(vlc, on_log, NULL);
    libvlc_dialog_set_error_callback(vlc, on_error, NULL);
    navigation(vlc);
    directory(vlc);
    forget();
    encrypted(vlc);
    forget();
    not_a_disc(vlc);
    forget();
    no_guesses(vlc);
    libvlc_release(vlc);
    return 0;
}
