/* Runs the plugin headless in libvlc over the synthetic disc in
   tests/fixtures/standard (see generate_objects.py): title 1 has chapters at
   0 and 45 frames (byte 24576), title 2 one chapter; 75 and 60 frames at
   30000/1001. Title and chapter changes are checked through the plugin's
   debug messages, which name the byte offset it moved to. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <pthread.h>
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

static void titles_and_chapters(libvlc_instance_t *vlc)
{
    libvlc_title_description_t **titles;
    libvlc_chapter_description_t **chapters;
    libvlc_media_player_t *mp;
    int count;

    mp = open_disc(vlc, HDDVD_FIXTURE_DIR "/standard", true);
    assert(wait_for(has_titles, mp));
    count = libvlc_media_player_get_full_title_descriptions(mp, &titles);
    assert(count == 2);
    assert(titles[0]->i_duration == FRAME_US(75) && titles[1]->i_duration == FRAME_US(60));
    libvlc_title_descriptions_release(titles, (unsigned)count);
    count = libvlc_media_player_get_full_chapter_descriptions(mp, 0, &chapters);
    assert(count == 2);
    assert(chapters[0]->i_time_offset == 0 && chapters[1]->i_time_offset == FRAME_US(45));
    libvlc_chapter_descriptions_release(chapters, (unsigned)count);
    count = libvlc_media_player_get_full_chapter_descriptions(mp, 1, &chapters);
    assert(count == 1);
    libvlc_chapter_descriptions_release(chapters, (unsigned)count);

    /* Chapter 2 of title 1 starts with its second cell, sector 12. */
    libvlc_media_player_set_chapter(mp, 1);
    assert(logged("title 0 chapter 1 at offset 24576"));
    libvlc_media_player_set_title(mp, 1);
    assert(logged("title 1 chapter 0 at offset 0"));

    /* Title 2 plays to its end. */
    libvlc_media_player_set_pause(mp, 0);
    assert(wait_for(stopped, mp));
    assert(libvlc_media_player_get_state(mp) == libvlc_Stopped);
    pthread_mutex_lock(&lock);
    assert(!error_title[0]);
    pthread_mutex_unlock(&lock);
    libvlc_media_player_release(mp);
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
    char cwd_root[1024], from[1024], to[1024];
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

int main(void)
{
    static const char *const args[] = {"--vout=vdummy", "--aout=adummy", "--no-video-title-show",
                                       "--verbose=2"};
    libvlc_instance_t *vlc = libvlc_new(4, args);
    assert(vlc);
    libvlc_log_set(vlc, on_log, NULL);
    libvlc_dialog_set_error_callback(vlc, on_error, NULL);
    titles_and_chapters(vlc);
    directory(vlc);
    encrypted(vlc);
    not_a_disc(vlc);
    libvlc_release(vlc);
    return 0;
}
