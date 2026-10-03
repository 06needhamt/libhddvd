#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_fixture(const char *name, size_t *size)
{
    char path[1024];
    FILE *file;
    long end;
    unsigned char *bytes;
    snprintf(path, sizeof(path), "%s/%s", HDDVD_FIXTURE_DIR, name);
    file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    end = ftell(file);
    assert(end > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)end);
    assert(bytes && fread(bytes, 1, (size_t)end, file) == (size_t)end);
    fclose(file);
    *size = (size_t)end;
    return bytes;
}

static void test_typed_playlist(void)
{
    size_t size;
    unsigned char *bytes = read_fixture("playlist_full.xpl", &size);
    hddvd_xpl *xpl = NULL;
    hddvd_xpl_configuration configuration;
    hddvd_xpl_media_attribute media;
    hddvd_xpl_title_info title;
    hddvd_xpl_clip clip;
    hddvd_xpl_chapter chapter;
    hddvd_xpl_track track;
    hddvd_xpl_application application;
    hddvd_xpl_application_resource resource;
    hddvd_xpl_event event;
    hddvd_xpl_limits limits;
    size_t title_index;

    assert(hddvd_xpl_open(bytes, size, &xpl) == HDDVD_OK);
    assert(!strcmp(hddvd_xpl_namespace(xpl), "http://www.dvdforum.org/2005/HDDVDVideo/Playlist"));
    assert(!strcmp(hddvd_xpl_display_name(xpl), "Original test playlist"));
    assert(!strcmp(hddvd_xpl_type(xpl), "Advanced"));
    assert(hddvd_xpl_frame_rate(xpl) == 60);
    assert(hddvd_xpl_get_configuration(xpl, &configuration) == HDDVD_OK);
    assert(configuration.has_aperture && configuration.aperture_width == 1920 &&
           configuration.aperture_height == 1080 && configuration.has_network_timeout &&
           configuration.network_timeout == 15000);
    assert(hddvd_xpl_media_attribute_count(xpl) == 3);
    assert(hddvd_xpl_get_media_attribute(xpl, 1, &media) == HDDVD_OK &&
           media.kind == HDDVD_XPL_MEDIA_AUDIO && media.index == 2 && !strcmp(media.codec, "DD+") &&
           !strcmp(media.channels, "2"));
    assert(hddvd_xpl_title_count(xpl) == 3);
    assert(hddvd_xpl_get_title(xpl, 1, &title) == HDDVD_OK &&
           title.kind == HDDVD_XPL_TITLE_REGULAR && !strcmp(title.id, "feature") &&
           !strcmp(title.on_end, "menu") && title.duration.frames == 7200);
    assert(hddvd_xpl_find_title(xpl, "menu", &title_index) == HDDVD_OK && title_index == 2);
    assert(hddvd_xpl_get_on_end_target(xpl, 1, &title_index) == HDDVD_OK && title_index == 2);
    assert(hddvd_xpl_get_on_end_target(xpl, 0, &title_index) == HDDVD_ENOTFOUND);
    assert(hddvd_xpl_clip_count(xpl, 1) == 2);
    assert(hddvd_xpl_get_clip(xpl, 1, 0, &clip) == HDDVD_OK && clip.has_clip_begin &&
           clip.clip_begin.frames == 180 && !strcmp(clip.data_source, "Disc"));
    assert(hddvd_xpl_chapter_count(xpl, 1) == 2);
    assert(hddvd_xpl_get_chapter(xpl, 1, 1, &chapter) == HDDVD_OK &&
           !strcmp(chapter.id, "chapter-2") && chapter.begin.frames == 3600);
    assert(hddvd_xpl_track_count(xpl, 1) == 5);
    assert(hddvd_xpl_get_track(xpl, 1, 4, &track) == HDDVD_OK &&
           track.kind == HDDVD_XPL_MEDIA_SUBPICTURE && !track.selectable &&
           !strcmp(track.language, "en:01"));
    assert(hddvd_xpl_application_count(xpl, 1) == 1);
    assert(hddvd_xpl_get_application(xpl, 1, 0, &application) == HDDVD_OK &&
           application.kind == HDDVD_XPL_APPLICATION_SEGMENT && application.autorun &&
           application.z_order == 2);
    assert(hddvd_xpl_event_count(xpl, 1) == 2);
    assert(hddvd_xpl_get_event(xpl, 1, 0, &event) == HDDVD_OK &&
           event.kind == HDDVD_XPL_EVENT_SCHEDULED && !strcmp(event.id, "reminder") &&
           event.time.frames == 5400);
    assert(hddvd_xpl_get_event(xpl, 1, 1, &event) == HDDVD_OK &&
           event.kind == HDDVD_XPL_EVENT_PAUSE && !event.id && event.time.frames == 6000);
    assert(hddvd_xpl_application_count(xpl, SIZE_MAX) == 1);
    assert(hddvd_xpl_get_application(xpl, SIZE_MAX, 0, &application) == HDDVD_OK &&
           application.kind == HDDVD_XPL_APPLICATION_PLAYLIST);
    assert(hddvd_xpl_application_resource_count(xpl, SIZE_MAX, 0) == 1);
    assert(hddvd_xpl_get_application_resource(xpl, SIZE_MAX, 0, 0, &resource) == HDDVD_OK &&
           resource.has_size && resource.size == 1234 && resource.multiplexed);
    assert(hddvd_xpl_get_title(xpl, 3, &title) == HDDVD_EINVAL);
    hddvd_xpl_close(xpl);

    hddvd_xpl_limits_default(&limits);
    limits.max_items = 2;
    assert(hddvd_xpl_open_with_limits(bytes, size, &limits, &xpl) == HDDVD_ELIMIT);
    hddvd_xpl_limits_default(&limits);
    limits.max_nodes = 2;
    assert(hddvd_xpl_open_with_limits(bytes, size, &limits, &xpl) == HDDVD_ELIMIT);
    free(bytes);
}

static void test_compatibility_helper(void)
{
    const char *path = "test.xpl";
    FILE *file = fopen(path, "wb");
    char *ref = NULL;
    assert(file);
    fputs("<?xml version='1.0'?><p:playlist xmlns:p='urn:test'><p:start><p:application "
          "ref='file:///ADV_OBJ/app.aca'/></p:start></p:playlist>",
          file);
    fclose(file);
    assert(hddvd_xpl_find_start_ref(path, &ref) == HDDVD_OK);
    assert(strcmp(ref, "file:///ADV_OBJ/app.aca") == 0);
    hddvd_free(ref);
    remove(path);
}

static void test_invalid_playlists(void)
{
    static const char wrong_namespace[] =
        "<Playlist xmlns='urn:not-hddvd'><TitleSet timeBase='60fps'>"
        "<FirstPlayTitle titleDuration='00:00:01:00'/></TitleSet></Playlist>";
    static const char invalid_time[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><FirstPlayTitle titleDuration='00:00:01:60'/>"
        "</TitleSet></Playlist>";
    static const char invalid_reference[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><FirstPlayTitle titleDuration='00:00:01:00'>"
        "<PrimaryAudioVideoClip titleTimeBegin='00:00:00:00'/></FirstPlayTitle>"
        "</TitleSet></Playlist>";
    static const char unresolved_branch[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title id='one' onEnd='missing'/></TitleSet></Playlist>";
    static const char duplicate_title_id[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title id='same'/><Title id='same'/>"
        "</TitleSet></Playlist>";
    static const char duplicate_title_number[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title id='one' titleNumber='7'/>"
        "<Title id='two' titleNumber='7'/></TitleSet></Playlist>";
    hddvd_xpl *xpl = NULL;
    assert(hddvd_xpl_open(wrong_namespace, sizeof(wrong_namespace) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(invalid_time, sizeof(invalid_time) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(invalid_reference, sizeof(invalid_reference) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(unresolved_branch, sizeof(unresolved_branch) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(duplicate_title_id, sizeof(duplicate_title_id) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(duplicate_title_number, sizeof(duplicate_title_number) - 1, &xpl) ==
           HDDVD_EPARSE);
}

int main(void)
{
    test_typed_playlist();
    test_compatibility_helper();
    test_invalid_playlists();
    return 0;
}
