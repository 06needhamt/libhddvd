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
    hddvd_xpl_clip_kind clip_kind;
    hddvd_xpl_chapter chapter;
    hddvd_xpl_track track;
    hddvd_xpl_application application;
    hddvd_xpl_application_resource resource;
    hddvd_xpl_event event;
    hddvd_xpl_time loading_begin;
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
    assert(hddvd_xpl_clip_count(xpl, 1) == 3);
    assert(hddvd_xpl_get_clip(xpl, 1, 0, &clip) == HDDVD_OK && clip.has_clip_begin &&
           clip.clip_begin.frames == 180 && !strcmp(clip.data_source, "Disc"));
    assert(hddvd_xpl_get_clip_kind(xpl, 1, 0, &clip_kind) == HDDVD_OK &&
           clip_kind == HDDVD_XPL_CLIP_PRIMARY_AUDIO_VIDEO);
    assert(hddvd_xpl_get_clip_kind(xpl, 1, 2, &clip_kind) == HDDVD_OK &&
           clip_kind == HDDVD_XPL_CLIP_SECONDARY_AUDIO_VIDEO);
    assert(hddvd_xpl_chapter_count(xpl, 1) == 2);
    assert(hddvd_xpl_get_chapter(xpl, 1, 1, &chapter) == HDDVD_OK &&
           !strcmp(chapter.id, "chapter-2") && chapter.begin.frames == 3600);
    assert(hddvd_xpl_track_count(xpl, 1) == 8);
    assert(hddvd_xpl_get_track(xpl, 1, 6, &track) == HDDVD_OK &&
           track.kind == HDDVD_XPL_MEDIA_SUBPICTURE && !track.selectable &&
           !strcmp(track.language, "en:01"));
    assert(hddvd_xpl_application_count(xpl, 1) == 1);
    assert(hddvd_xpl_get_application(xpl, 1, 0, &application) == HDDVD_OK &&
           application.kind == HDDVD_XPL_APPLICATION_SEGMENT && application.autorun &&
           application.z_order == 2);
    assert(hddvd_xpl_get_application_loading_begin(xpl, 1, 0, &loading_begin) == HDDVD_OK &&
           loading_begin.frames == 270 && loading_begin.frame_rate == 60);
    assert(hddvd_xpl_event_count(xpl, 1) == 2);
    assert(hddvd_xpl_get_event(xpl, 1, 0, &event) == HDDVD_OK &&
           event.kind == HDDVD_XPL_EVENT_SCHEDULED && !strcmp(event.id, "reminder") &&
           event.time.frames == 5400);
    assert(hddvd_xpl_get_event(xpl, 1, 1, &event) == HDDVD_OK &&
           event.kind == HDDVD_XPL_EVENT_PAUSE && !event.id && event.time.frames == 6000);
    assert(hddvd_xpl_application_count(xpl, SIZE_MAX) == 1);
    assert(hddvd_xpl_get_application(xpl, SIZE_MAX, 0, &application) == HDDVD_OK &&
           application.kind == HDDVD_XPL_APPLICATION_PLAYLIST);
    assert(hddvd_xpl_get_application_loading_begin(xpl, SIZE_MAX, 0, &loading_begin) ==
           HDDVD_ENOTFOUND);
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

static void test_clip_kinds(void)
{
    static const char playlist[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<PrimaryAudioVideoClip src='primary.map'/>"
        "<SubstituteAudioVideoClip src='substitute-av.map'/>"
        "<SubstituteAudioClip src='substitute-audio.map'/>"
        "<SecondaryAudioVideoClip src='secondary.map'/>"
        "<AdvancedSubtitleSegment src='subtitle.xas'/>"
        "</Title></TitleSet></Playlist>";
    static const hddvd_xpl_clip_kind expected[] = {
        HDDVD_XPL_CLIP_PRIMARY_AUDIO_VIDEO, HDDVD_XPL_CLIP_SUBSTITUTE_AUDIO_VIDEO,
        HDDVD_XPL_CLIP_SUBSTITUTE_AUDIO, HDDVD_XPL_CLIP_SECONDARY_AUDIO_VIDEO,
        HDDVD_XPL_CLIP_ADVANCED_SUBTITLE};
    hddvd_xpl *xpl = NULL;
    size_t i;

    assert(hddvd_xpl_open(playlist, sizeof(playlist) - 1, &xpl) == HDDVD_OK);
    assert(hddvd_xpl_clip_count(xpl, 0) == sizeof(expected) / sizeof(expected[0]));
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        hddvd_xpl_clip_kind kind;
        assert(hddvd_xpl_get_clip_kind(xpl, 0, i, &kind) == HDDVD_OK);
        assert(kind == expected[i]);
    }
    assert(hddvd_xpl_get_clip_kind(xpl, 0, i, NULL) == HDDVD_EINVAL);
    hddvd_xpl_close(xpl);
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
    static const char invalid_loading_begin[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title><ApplicationSegment src='app.xmf' "
        "loadingBegin='00:00:00:60'/></Title></TitleSet></Playlist>";
    static const char inverted_clip_range[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<PrimaryAudioVideoClip src='clip.map' titleTimeBegin='00:00:08:00' "
        "titleTimeEnd='00:00:07:00'/></Title></TitleSet></Playlist>";
    static const char chapter_after_title[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<Chapter titleTimeBegin='00:00:10:01'/></Title></TitleSet></Playlist>";
    static const char nonzero_first_chapter[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<Chapter titleTimeBegin='00:00:01:00'/></Title></TitleSet></Playlist>";
    static const char unordered_chapters[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<Chapter titleTimeBegin='00:00:00:00'/><Chapter titleTimeBegin='00:00:05:00'/>"
        "<Chapter titleTimeBegin='00:00:04:00'/></Title></TitleSet></Playlist>";
    static const char duplicate_chapter_time[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<Chapter titleTimeBegin='00:00:00:00'/><Chapter titleTimeBegin='00:00:00:00'/>"
        "</Title></TitleSet></Playlist>";
    static const char application_after_title[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<ApplicationSegment src='app.xmf' titleTimeBegin='00:00:11:00'/>"
        "</Title></TitleSet></Playlist>";
    static const char event_after_title[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<PauseAt titleTime='00:00:10:01'/></Title></TitleSet></Playlist>";
    hddvd_xpl *xpl = NULL;
    assert(hddvd_xpl_open(wrong_namespace, sizeof(wrong_namespace) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(invalid_time, sizeof(invalid_time) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(invalid_reference, sizeof(invalid_reference) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(unresolved_branch, sizeof(unresolved_branch) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(duplicate_title_id, sizeof(duplicate_title_id) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(duplicate_title_number, sizeof(duplicate_title_number) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(invalid_loading_begin, sizeof(invalid_loading_begin) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(inverted_clip_range, sizeof(inverted_clip_range) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(chapter_after_title, sizeof(chapter_after_title) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(nonzero_first_chapter, sizeof(nonzero_first_chapter) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(unordered_chapters, sizeof(unordered_chapters) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(duplicate_chapter_time, sizeof(duplicate_chapter_time) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(application_after_title, sizeof(application_after_title) - 1, &xpl) ==
           HDDVD_EPARSE);
    assert(hddvd_xpl_open(event_after_title, sizeof(event_after_title) - 1, &xpl) == HDDVD_EPARSE);
}

static void expect_state(const hddvd_xpl *xpl, size_t title, size_t application, uint64_t frames,
                         hddvd_xpl_application_state expected)
{
    hddvd_xpl_application_state state = (hddvd_xpl_application_state)-1;
    hddvd_xpl_time at = {frames, 60};
    assert(hddvd_xpl_get_application_state(xpl, title, application, at, &state) == HDDVD_OK);
    assert(state == expected);
}

static void test_playlist_metadata_and_lifecycle(void)
{
    size_t size;
    unsigned char *bytes = read_fixture("playlist_full.xpl", &size);
    hddvd_xpl *xpl = NULL;
    hddvd_xpl_application_state state;
    uint32_t major = 0, minor = 0;

    assert(hddvd_xpl_open(bytes, size, &xpl) == HDDVD_OK);
    assert(hddvd_xpl_get_version(xpl, &major, &minor) == HDDVD_OK && major == 1 && minor == 0);
    assert(hddvd_xpl_tick_rate(xpl) == 60);
    assert(!strcmp(hddvd_xpl_default_language(xpl), "en"));
    assert(hddvd_xpl_unmodeled_count(xpl) == 0);

    /* loadingBegin 270, titleTimeBegin 300, titleTimeEnd 6900 (frames). */
    expect_state(xpl, 1, 0, 0, HDDVD_XPL_APPLICATION_INACTIVE);
    expect_state(xpl, 1, 0, 269, HDDVD_XPL_APPLICATION_INACTIVE);
    expect_state(xpl, 1, 0, 270, HDDVD_XPL_APPLICATION_LOADING);
    expect_state(xpl, 1, 0, 299, HDDVD_XPL_APPLICATION_LOADING);
    expect_state(xpl, 1, 0, 300, HDDVD_XPL_APPLICATION_ACTIVE);
    expect_state(xpl, 1, 0, 6899, HDDVD_XPL_APPLICATION_ACTIVE);
    expect_state(xpl, 1, 0, 6900, HDDVD_XPL_APPLICATION_INACTIVE);
    /* Playlist applications without title times are active throughout. */
    expect_state(xpl, SIZE_MAX, 0, 0, HDDVD_XPL_APPLICATION_ACTIVE);
    expect_state(xpl, SIZE_MAX, 0, UINT64_MAX, HDDVD_XPL_APPLICATION_ACTIVE);

    assert(hddvd_xpl_get_application_state(xpl, 1, 0, (hddvd_xpl_time){300, 30}, &state) ==
           HDDVD_EINVAL);
    assert(hddvd_xpl_get_application_state(xpl, 1, 1, (hddvd_xpl_time){300, 60}, &state) ==
           HDDVD_EINVAL);
    assert(hddvd_xpl_get_application_state(xpl, 0, 0, (hddvd_xpl_time){0, 60}, &state) ==
           HDDVD_EINVAL);
    assert(hddvd_xpl_get_application_state(xpl, 1, 0, (hddvd_xpl_time){0, 60}, NULL) ==
           HDDVD_EINVAL);
    hddvd_xpl_close(xpl);
    free(bytes);
}

static void test_details(void)
{
    size_t size;
    unsigned char *bytes = read_fixture("playlist_full.xpl", &size);
    hddvd_xpl *xpl = NULL;
    hddvd_xpl_title_details title;
    hddvd_xpl_track track;
    hddvd_xpl_track_details details;
    hddvd_xpl_application_details application;
    hddvd_xpl_resource_details resource;
    const char *description;

    assert(hddvd_xpl_open(bytes, size, &xpl) == HDDVD_OK);
    assert(!strcmp(hddvd_xpl_description(xpl), "Original playlist description"));
    assert(hddvd_xpl_get_title_details(xpl, 1, &title) == HDDVD_OK);
    assert(!strcmp(title.description, "Original feature") &&
           !strcmp(title.alternative_sd_display_mode, "letterbox") && title.has_tick_base_divisor &&
           title.tick_base_divisor == 2);
    assert(hddvd_xpl_get_title_details(xpl, 2, &title) == HDDVD_OK && !title.description &&
           !title.alternative_sd_display_mode && !title.has_tick_base_divisor);
    assert(hddvd_xpl_get_clip_description(xpl, 1, 0, &description) == HDDVD_OK &&
           !strcmp(description, "Part one"));
    assert(hddvd_xpl_get_clip_description(xpl, 1, 1, &description) == HDDVD_OK && !description);

    /* Clip streams first: Video, Audio, SubVideo, SubAudio. */
    assert(hddvd_xpl_get_track_details(xpl, 1, 0, &details) == HDDVD_OK &&
           details.has_angle_number && details.angle_number == 1 && !details.secondary);
    assert(hddvd_xpl_get_track_details(xpl, 1, 1, &details) == HDDVD_OK &&
           !strcmp(details.description, "Original stereo") && !details.has_angle_number);
    assert(hddvd_xpl_get_track_details(xpl, 1, 2, &details) == HDDVD_OK && details.secondary &&
           !strcmp(details.description, "Inset"));
    assert(hddvd_xpl_get_track(xpl, 1, 3, &track) == HDDVD_OK &&
           track.kind == HDDVD_XPL_MEDIA_AUDIO && track.stream_number == 1);
    assert(hddvd_xpl_get_track_details(xpl, 1, 3, &details) == HDDVD_OK && details.secondary);
    assert(hddvd_xpl_get_track_details(xpl, 1, 7, &details) == HDDVD_OK && details.has_forced &&
           details.forced);
    assert(hddvd_xpl_get_track_details(xpl, 1, 6, &details) == HDDVD_OK && !details.has_forced);

    assert(hddvd_xpl_get_application_details(xpl, 1, 0, &application) == HDDVD_OK &&
           !strcmp(application.id, "segment") && !strcmp(application.sync_text, "hard") &&
           application.sync == HDDVD_XPL_SYNC_HARD);
    assert(hddvd_xpl_get_application_details(xpl, SIZE_MAX, 0, &application) == HDDVD_OK &&
           !application.id && application.sync == HDDVD_XPL_SYNC_SOFT);
    assert(hddvd_xpl_get_application_resource_details(xpl, SIZE_MAX, 0, 0, &resource) == HDDVD_OK &&
           !strcmp(resource.description, "Original archive") && resource.has_priority &&
           resource.priority == 3 && resource.has_loading_begin &&
           resource.loading_begin.frames == 60 && resource.loading_begin.frame_rate == 60);

    assert(hddvd_xpl_get_title_details(xpl, 3, &title) == HDDVD_EINVAL);
    assert(hddvd_xpl_get_clip_description(xpl, 1, 3, &description) == HDDVD_EINVAL);
    assert(hddvd_xpl_get_track_details(xpl, 1, 8, &details) == HDDVD_EINVAL);
    assert(hddvd_xpl_get_application_details(xpl, 1, 1, &application) == HDDVD_EINVAL);
    assert(hddvd_xpl_get_application_resource_details(xpl, SIZE_MAX, 0, 1, &resource) ==
           HDDVD_EINVAL);
    assert(hddvd_xpl_get_title_details(NULL, 0, &title) == HDDVD_EINVAL);
    assert(hddvd_xpl_description(NULL) == NULL);
    hddvd_xpl_close(xpl);
    free(bytes);
}

static void test_detail_values(void)
{
#define PLAYLIST(title, inner)                                                                     \
    "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"                          \
    "<TitleSet timeBase='60fps'><Title " title ">" inner "</Title></TitleSet></Playlist>"
    static const char *const invalid[] = {
        PLAYLIST("tickBaseDivisor='0'", ""),
        PLAYLIST("tickBaseDivisor='x'", ""),
        PLAYLIST("tickBaseDivisor='4294967296'", ""),
        PLAYLIST("", "<SubtitleTrack track='1' forced='maybe'/>"),
        PLAYLIST("", "<PrimaryAudioVideoClip src='a.MAP'><Video track='1' angleNumber='-1'/>"
                     "</PrimaryAudioVideoClip>"),
        PLAYLIST("", "<ApplicationSegment src='a.xmf'><ApplicationResource src='r' "
                     "priority='4294967296'/></ApplicationSegment>"),
        PLAYLIST("", "<ApplicationSegment src='a.xmf'><ApplicationResource src='r' "
                     "loadingBegin='00:00:00:60'/></ApplicationSegment>"),
    };
    static const char other_sync[] = PLAYLIST("", "<ApplicationSegment src='a.xmf' sync='later'/>"
                                                  "<ApplicationSegment src='b.xmf' sync='none'/>"
                                                  "<ApplicationSegment src='c.xmf'/>");
#undef PLAYLIST
    hddvd_xpl *xpl = NULL;
    hddvd_xpl_application_details application;
    size_t i;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(hddvd_xpl_open(invalid[i], strlen(invalid[i]), &xpl) == HDDVD_EPARSE && !xpl);
    /* Unrecognized sync values are kept, not rejected. */
    assert(hddvd_xpl_open(other_sync, sizeof(other_sync) - 1, &xpl) == HDDVD_OK);
    assert(hddvd_xpl_get_application_details(xpl, 0, 0, &application) == HDDVD_OK &&
           application.sync == HDDVD_XPL_SYNC_OTHER && !strcmp(application.sync_text, "later"));
    assert(hddvd_xpl_get_application_details(xpl, 0, 1, &application) == HDDVD_OK &&
           application.sync == HDDVD_XPL_SYNC_NONE);
    assert(hddvd_xpl_get_application_details(xpl, 0, 2, &application) == HDDVD_OK &&
           application.sync == HDDVD_XPL_SYNC_UNSPECIFIED && !application.sync_text);
    assert(hddvd_xpl_unmodeled_count(xpl) == 0);
    hddvd_xpl_close(xpl);
}

static void test_open_ended_application(void)
{
    static const char playlist[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title titleDuration='00:00:10:00'>"
        "<ApplicationSegment src='a.xmf' titleTimeBegin='00:00:02:00'/></Title>"
        "<Title><ApplicationSegment src='b.xmf'/></Title></TitleSet></Playlist>";
    hddvd_xpl *xpl = NULL;
    uint32_t major, minor;
    assert(hddvd_xpl_open(playlist, sizeof(playlist) - 1, &xpl) == HDDVD_OK);
    assert(hddvd_xpl_get_version(xpl, &major, &minor) == HDDVD_ENOTFOUND);
    assert(hddvd_xpl_tick_rate(xpl) == 0 && hddvd_xpl_default_language(xpl) == NULL);
    /* Without titleTimeEnd the window closes at the title duration. */
    expect_state(xpl, 0, 0, 119, HDDVD_XPL_APPLICATION_INACTIVE);
    expect_state(xpl, 0, 0, 120, HDDVD_XPL_APPLICATION_ACTIVE);
    expect_state(xpl, 0, 0, 599, HDDVD_XPL_APPLICATION_ACTIVE);
    expect_state(xpl, 0, 0, 600, HDDVD_XPL_APPLICATION_INACTIVE);
    /* Without a duration either, it stays active. */
    expect_state(xpl, 1, 0, UINT64_MAX, HDDVD_XPL_APPLICATION_ACTIVE);
    hddvd_xpl_close(xpl);
}

static void test_unmodeled_constructs(void)
{
    static const char playlist[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist' "
        "xmlns:x='urn:original-extension' majorVersion='1' x:note='a'>"
        "<TitleSet timeBase='60fps'>"
        "<Title id='one' futureAttribute='1'>"
        "<FutureElement level='2'><Chapter titleTimeBegin='00:00:00:00'/></FutureElement>"
        "<FutureElement/>"
        "<ApplicationSegment src='a.xmf' futureAttribute='3'/>"
        "</Title><Title id='two' futureAttribute='4'/></TitleSet></Playlist>";
    static const char *const expected[][2] = {{"Playlist", "x:note"},
                                              {"Title", "futureAttribute"},
                                              {"FutureElement", NULL},
                                              {"ApplicationSegment", "futureAttribute"}};
    static const size_t occurrences[] = {1, 2, 2, 1};
    hddvd_xpl *xpl = NULL;
    hddvd_xpl_unmodeled record;
    size_t i;
    assert(hddvd_xpl_open(playlist, sizeof(playlist) - 1, &xpl) == HDDVD_OK);
    /* Unknown containers are still searched for modeled descendants. */
    assert(hddvd_xpl_chapter_count(xpl, 0) == 1);
    assert(hddvd_xpl_unmodeled_count(xpl) == 4);
    for (i = 0; i < 4; i++)
    {
        assert(hddvd_xpl_get_unmodeled(xpl, i, &record) == HDDVD_OK);
        assert(!strcmp(record.element, expected[i][0]));
        assert(expected[i][1] ? record.attribute && !strcmp(record.attribute, expected[i][1])
                              : record.attribute == NULL);
        assert(record.occurrences == occurrences[i]);
    }
    assert(hddvd_xpl_get_unmodeled(xpl, 4, &record) == HDDVD_EINVAL);
    assert(hddvd_xpl_get_unmodeled(xpl, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_xpl_unmodeled_count(NULL) == 0);
    hddvd_xpl_close(xpl);
}

static void test_invalid_metadata(void)
{
    static const char bad_version[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist' majorVersion='x'>"
        "<TitleSet timeBase='60fps'><Title/></TitleSet></Playlist>";
    static const char bad_minor[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist' majorVersion='1' "
        "minorVersion='-1'><TitleSet timeBase='60fps'><Title/></TitleSet></Playlist>";
    static const char bad_tick_base[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps' tickBase='0fps'><Title/></TitleSet></Playlist>";
    hddvd_xpl *xpl = NULL;
    assert(hddvd_xpl_open(bad_version, sizeof(bad_version) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(bad_minor, sizeof(bad_minor) - 1, &xpl) == HDDVD_EPARSE);
    assert(hddvd_xpl_open(bad_tick_base, sizeof(bad_tick_base) - 1, &xpl) == HDDVD_EPARSE);
    assert(xpl == NULL);
}

/* Real playlists are stored in an AACS clear-text envelope and use numeric
   multiplexed values; both are exercised with original content. */
static void test_enveloped_playlist(void)
{
    static const char playlist[] =
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'><Title id='t'/>"
        "<PlaylistApplication src='a.xmf'>"
        "<PlaylistApplicationResource src='a.aca' multiplexed='11'/>"
        "<PlaylistApplicationResource src='b.aca' multiplexed='false'/>"
        "</PlaylistApplication></TitleSet></Playlist>";
    unsigned char bytes[1024];
    size_t length = sizeof(playlist) - 1;
    hddvd_xpl *xpl = NULL;
    hddvd_xpl_application_resource resource;
    uint64_t multiplexed = 0;
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "AACS", 4);
    bytes[4] = 0x12;
    bytes[7] = (unsigned char)(length >> 24);
    bytes[8] = (unsigned char)(length >> 16);
    bytes[9] = (unsigned char)(length >> 8);
    bytes[10] = (unsigned char)length;
    memcpy(bytes + 11, "VPLST000.XPL.AACS", 17);
    memcpy(bytes + 283, playlist, length);
    assert(hddvd_xpl_open(bytes, 283 + length + 4, &xpl) == HDDVD_OK);
    assert(hddvd_xpl_get_application_resource(xpl, SIZE_MAX, 0, 0, &resource) == HDDVD_OK &&
           resource.multiplexed);
    assert(hddvd_xpl_get_application_resource_multiplexed(xpl, SIZE_MAX, 0, 0, &multiplexed) ==
               HDDVD_OK &&
           multiplexed == 11);
    assert(hddvd_xpl_get_application_resource(xpl, SIZE_MAX, 0, 1, &resource) == HDDVD_OK &&
           !resource.multiplexed);
    assert(hddvd_xpl_get_application_resource_multiplexed(xpl, SIZE_MAX, 0, 1, &multiplexed) ==
               HDDVD_OK &&
           multiplexed == 0);
    assert(hddvd_xpl_get_application_resource_multiplexed(xpl, SIZE_MAX, 0, 2, &multiplexed) ==
           HDDVD_EINVAL);
    hddvd_xpl_close(xpl);
    /* A wrapped object that is not clear text is not a playlist. */
    bytes[4] = 0x02;
    assert(hddvd_xpl_open(bytes, 283 + length + 16, &xpl) == HDDVD_EPARSE);
}

int main(void)
{
    test_typed_playlist();
    test_compatibility_helper();
    test_clip_kinds();
    test_invalid_playlists();
    test_playlist_metadata_and_lifecycle();
    test_open_ended_application();
    test_unmodeled_constructs();
    test_details();
    test_detail_values();
    test_invalid_metadata();
    test_enveloped_playlist();
    return 0;
}
