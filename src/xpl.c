#include "internal.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XPL_NS "http://www.dvdforum.org/2005/HDDVDVideo/Playlist"
#define XPL_MAX_NODES 32768u
#define XPL_MAX_ITEMS 8192u

struct xpl_application
{
    hddvd_xpl_application value;
    hddvd_xpl_application_resource *resources;
    size_t resource_count;
};

struct xpl_title
{
    hddvd_xpl_title_info value;
    hddvd_xpl_clip *clips;
    size_t clip_count;
    hddvd_xpl_chapter *chapters;
    size_t chapter_count;
    hddvd_xpl_track *tracks;
    size_t track_count;
    struct xpl_application *applications;
    size_t application_count;
    hddvd_xpl_event *events;
    size_t event_count;
};

struct hddvd_xpl
{
    char *namespace_uri;
    char *display_name;
    char *type;
    uint32_t frame_rate;
    hddvd_xpl_configuration configuration;
    hddvd_xpl_media_attribute *media;
    size_t media_count;
    struct xpl_title *titles;
    size_t title_count;
    struct xpl_application *applications;
    size_t application_count;
    size_t item_count;
    size_t item_limit;
};

static int node_is(const xmlNode *node, const char *name)
{
    return node && node->type == XML_ELEMENT_NODE && !xmlStrcmp(node->name, (const xmlChar *)name);
}

static int in_namespace(const xmlNode *node)
{
    return node && node->ns && node->ns->href &&
           !xmlStrcmp(node->ns->href, (const xmlChar *)XPL_NS);
}

static char *dup_prop(xmlNodePtr node, const char *name)
{
    xmlChar *value = xmlGetProp(node, (const xmlChar *)name);
    char *copy;
    if (!value)
        return NULL;
    copy = hddvd_strdup((const char *)value);
    xmlFree(value);
    return copy;
}

static int count_nodes(xmlNodePtr node, size_t limit, size_t *count)
{
    for (; node; node = node->next)
    {
        int rc;
        if (node->type != XML_ELEMENT_NODE)
            continue;
        if (!in_namespace(node))
            return HDDVD_EPARSE;
        if (*count == SIZE_MAX || ++*count > limit)
            return HDDVD_ELIMIT;
        rc = count_nodes(node->children, limit, count);
        if (rc)
            return rc;
    }
    return HDDVD_OK;
}

static int parse_u64(const char *value, uint64_t *result)
{
    char *end;
    unsigned long long parsed;
    if (!value || !*value || *value == '-')
        return HDDVD_EPARSE;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno || *end)
        return HDDVD_EPARSE;
    *result = (uint64_t)parsed;
    return HDDVD_OK;
}

static int uint_prop(xmlNodePtr node, const char *name, uint64_t *result, uint8_t *present)
{
    char *value = dup_prop(node, name);
    int rc;
    *present = value != NULL;
    if (!value)
        return HDDVD_OK;
    rc = parse_u64(value, result);
    free(value);
    return rc;
}

static int boolean_value(const char *value, uint8_t *result)
{
    if (!value)
        return HDDVD_OK;
    if (!strcmp(value, "true") || !strcmp(value, "1"))
        *result = 1;
    else if (!strcmp(value, "false") || !strcmp(value, "0"))
        *result = 0;
    else
        return HDDVD_EPARSE;
    return HDDVD_OK;
}

static int bool_prop(xmlNodePtr node, const char *name, uint8_t *result)
{
    char *value = dup_prop(node, name);
    int rc = boolean_value(value, result);
    free(value);
    return rc;
}

static int frame_rate_value(const char *value, uint32_t *result)
{
    char *end;
    unsigned long rate;
    if (!value || !*value)
        return HDDVD_EPARSE;
    errno = 0;
    rate = strtoul(value, &end, 10);
    if (errno || rate == 0 || rate > UINT32_MAX || strcmp(end, "fps"))
        return HDDVD_EPARSE;
    *result = (uint32_t)rate;
    return HDDVD_OK;
}

static int parse_time(const char *value, uint32_t rate, hddvd_xpl_time *result)
{
    unsigned long long hours;
    unsigned minutes;
    unsigned seconds;
    unsigned frames;
    char tail;
    uint64_t total;
    if (!value ||
        sscanf(value, "%llu:%u:%u:%u%c", &hours, &minutes, &seconds, &frames, &tail) != 4 ||
        minutes >= 60 || seconds >= 60 || frames >= rate)
        return HDDVD_EPARSE;
    if (hours > (UINT64_MAX / 60u - minutes) / 60u)
        return HDDVD_ELIMIT;
    total = ((uint64_t)hours * 60u + minutes) * 60u + seconds;
    if (total > (UINT64_MAX - frames) / rate)
        return HDDVD_ELIMIT;
    result->frames = total * rate + frames;
    result->frame_rate = rate;
    return HDDVD_OK;
}

static int time_prop(xmlNodePtr node, const char *name, uint32_t rate, hddvd_xpl_time *result,
                     uint8_t *present)
{
    char *value = dup_prop(node, name);
    int rc;
    *present = value != NULL;
    if (!value)
        return HDDVD_OK;
    rc = parse_time(value, rate, result);
    free(value);
    return rc;
}

static int append_item(hddvd_xpl *xpl, void **items, size_t *count, size_t size, const void *value)
{
    void *grown;
    if (xpl->item_count >= xpl->item_limit || *count == SIZE_MAX || *count + 1 > SIZE_MAX / size)
        return HDDVD_ELIMIT;
    grown = realloc(*items, (*count + 1) * size);
    if (!grown)
        return HDDVD_ENOMEM;
    *items = grown;
    memcpy((unsigned char *)grown + *count * size, value, size);
    (*count)++;
    xpl->item_count++;
    return HDDVD_OK;
}

static void free_application(struct xpl_application *application)
{
    size_t i;
    free((char *)application->value.src);
    free((char *)application->value.language);
    free((char *)application->value.description);
    for (i = 0; i < application->resource_count; i++)
        free((char *)application->resources[i].src);
    free(application->resources);
}

static void free_title(struct xpl_title *title)
{
    size_t i;
    free((char *)title->value.id);
    free((char *)title->value.type);
    free((char *)title->value.display_name);
    free((char *)title->value.on_end);
    for (i = 0; i < title->clip_count; i++)
    {
        free((char *)title->clips[i].src);
        free((char *)title->clips[i].data_source);
    }
    for (i = 0; i < title->chapter_count; i++)
    {
        free((char *)title->chapters[i].id);
        free((char *)title->chapters[i].display_name);
    }
    for (i = 0; i < title->track_count; i++)
        free((char *)title->tracks[i].language);
    for (i = 0; i < title->application_count; i++)
        free_application(&title->applications[i]);
    for (i = 0; i < title->event_count; i++)
        free((char *)title->events[i].id);
    free(title->clips);
    free(title->chapters);
    free(title->tracks);
    free(title->applications);
    free(title->events);
}

void hddvd_xpl_close(hddvd_xpl *xpl)
{
    size_t i;
    if (!xpl)
        return;
    free(xpl->namespace_uri);
    free(xpl->display_name);
    free(xpl->type);
    for (i = 0; i < xpl->media_count; i++)
    {
        free((char *)xpl->media[i].codec);
        free((char *)xpl->media[i].channels);
    }
    for (i = 0; i < xpl->title_count; i++)
        free_title(&xpl->titles[i]);
    for (i = 0; i < xpl->application_count; i++)
        free_application(&xpl->applications[i]);
    free(xpl->media);
    free(xpl->titles);
    free(xpl->applications);
    free(xpl);
}

void hddvd_xpl_limits_default(hddvd_xpl_limits *limits)
{
    if (limits)
    {
        limits->max_nodes = XPL_MAX_NODES;
        limits->max_items = XPL_MAX_ITEMS;
    }
}

static int parse_configuration(xmlNodePtr node, hddvd_xpl *xpl)
{
    xmlNodePtr child;
    for (child = node->children; child; child = child->next)
    {
        uint64_t number;
        uint8_t present;
        char *value;
        if (child->type != XML_ELEMENT_NODE)
            continue;
        if (node_is(child, "StreamingBuffer"))
        {
            int rc = uint_prop(child, "size", &xpl->configuration.streaming_buffer_size,
                               &xpl->configuration.has_streaming_buffer_size);
            if (rc)
                return rc;
        }
        else if (node_is(child, "Aperture"))
        {
            unsigned width;
            unsigned height;
            char tail;
            value = dup_prop(child, "size");
            if (!value || sscanf(value, "%ux%u%c", &width, &height, &tail) != 2 || !width ||
                !height)
            {
                free(value);
                return HDDVD_EPARSE;
            }
            free(value);
            xpl->configuration.aperture_width = width;
            xpl->configuration.aperture_height = height;
            xpl->configuration.has_aperture = 1;
        }
        else if (node_is(child, "MainVideoDefaultColor"))
        {
            char *end;
            unsigned long color;
            value = dup_prop(child, "color");
            if (!value || !*value)
            {
                free(value);
                return HDDVD_EPARSE;
            }
            errno = 0;
            color = strtoul(value, &end, 16);
            if (errno || *end || color > UINT32_MAX)
            {
                free(value);
                return HDDVD_EPARSE;
            }
            free(value);
            xpl->configuration.main_video_default_color = (uint32_t)color;
            xpl->configuration.has_main_video_default_color = 1;
        }
        else if (node_is(child, "NetworkTimeout"))
        {
            int rc = uint_prop(child, "timeout", &number, &present);
            if (rc || !present || number > UINT32_MAX)
                return rc ? rc : HDDVD_EPARSE;
            xpl->configuration.network_timeout = (uint32_t)number;
            xpl->configuration.has_network_timeout = 1;
        }
    }
    return HDDVD_OK;
}

static hddvd_xpl_media_kind media_kind(xmlNodePtr node)
{
    if (node_is(node, "VideoAttributeItem") || node_is(node, "Video") ||
        node_is(node, "VideoTrack") || node_is(node, "SubVideo"))
        return HDDVD_XPL_MEDIA_VIDEO;
    if (node_is(node, "AudioAttributeItem") || node_is(node, "Audio") ||
        node_is(node, "AudioTrack"))
        return HDDVD_XPL_MEDIA_AUDIO;
    if (node_is(node, "SubpictureAttributeItem") || node_is(node, "Subtitle") ||
        node_is(node, "SubpictureTrack") || node_is(node, "SubtitleTrack"))
        return HDDVD_XPL_MEDIA_SUBPICTURE;
    return 0;
}

static int parse_media(xmlNodePtr node, hddvd_xpl *xpl)
{
    xmlNodePtr child;
    for (child = node->children; child; child = child->next)
    {
        hddvd_xpl_media_attribute item;
        uint64_t index;
        uint8_t present;
        int rc;
        if (child->type != XML_ELEMENT_NODE || !media_kind(child))
            continue;
        memset(&item, 0, sizeof(item));
        item.kind = media_kind(child);
        rc = uint_prop(child, "index", &index, &present);
        if (rc || !present || index > UINT32_MAX)
            return rc ? rc : HDDVD_EPARSE;
        item.index = (uint32_t)index;
        item.codec = dup_prop(child, "codec");
        item.channels = dup_prop(child, "channels");
        if (!item.codec)
        {
            free((char *)item.channels);
            return HDDVD_EPARSE;
        }
        rc = append_item(xpl, (void **)&xpl->media, &xpl->media_count, sizeof(item), &item);
        if (rc)
        {
            free((char *)item.codec);
            free((char *)item.channels);
            return rc;
        }
    }
    return HDDVD_OK;
}

static int parse_clip(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_title *title)
{
    hddvd_xpl_clip clip;
    int rc;
    memset(&clip, 0, sizeof(clip));
    clip.src = dup_prop(node, "src");
    clip.data_source = dup_prop(node, "dataSource");
    if (!clip.src)
        return HDDVD_EPARSE;
    rc = time_prop(node, "titleTimeBegin", xpl->frame_rate, &clip.title_begin,
                   &clip.has_title_begin);
    if (!rc)
        rc = time_prop(node, "titleTimeEnd", xpl->frame_rate, &clip.title_end, &clip.has_title_end);
    if (!rc)
        rc = time_prop(node, "clipTimeBegin", xpl->frame_rate, &clip.clip_begin,
                       &clip.has_clip_begin);
    if (!rc)
        rc = bool_prop(node, "seamless", &clip.seamless);
    if (!rc)
        rc = append_item(xpl, (void **)&title->clips, &title->clip_count, sizeof(clip), &clip);
    if (rc)
    {
        free((char *)clip.src);
        free((char *)clip.data_source);
    }
    return rc;
}

static int parse_chapter(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_title *title)
{
    hddvd_xpl_chapter chapter;
    uint8_t present;
    int rc;
    memset(&chapter, 0, sizeof(chapter));
    chapter.id = dup_prop(node, "id");
    chapter.display_name = dup_prop(node, "displayName");
    rc = time_prop(node, "titleTimeBegin", xpl->frame_rate, &chapter.begin, &present);
    if (!rc && !present)
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = append_item(xpl, (void **)&title->chapters, &title->chapter_count, sizeof(chapter),
                         &chapter);
    if (rc)
    {
        free((char *)chapter.id);
        free((char *)chapter.display_name);
    }
    return rc;
}

static int parse_track(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_title *title)
{
    hddvd_xpl_track track;
    uint64_t number = 0;
    uint8_t present;
    int rc;
    memset(&track, 0, sizeof(track));
    track.kind = media_kind(node);
    if (!track.kind)
        return HDDVD_OK;
    rc = uint_prop(node, "track", &number, &present);
    if (rc || !present || number > UINT32_MAX)
        return rc ? rc : HDDVD_EPARSE;
    track.track = (uint32_t)number;
    rc = uint_prop(node, "streamNumber", &number, &track.has_stream_number);
    if (!rc && track.has_stream_number && number > UINT32_MAX)
        rc = HDDVD_EPARSE;
    track.stream_number = (uint32_t)number;
    if (!rc)
        rc = uint_prop(node, "mediaAttr", &number, &track.has_media_attribute);
    if (!rc && track.has_media_attribute && number > UINT32_MAX)
        rc = HDDVD_EPARSE;
    track.media_attribute = (uint32_t)number;
    if (!rc)
        rc = bool_prop(node, "selectable", &track.selectable);
    track.language = dup_prop(node, "langcode");
    if (!rc)
        rc = append_item(xpl, (void **)&title->tracks, &title->track_count, sizeof(track), &track);
    if (rc)
        free((char *)track.language);
    return rc;
}

static int parse_resource(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_application *application)
{
    hddvd_xpl_application_resource resource;
    int rc;
    memset(&resource, 0, sizeof(resource));
    resource.src = dup_prop(node, "src");
    if (!resource.src)
        return HDDVD_EPARSE;
    rc = uint_prop(node, "size", &resource.size, &resource.has_size);
    if (!rc)
        rc = bool_prop(node, "multiplexed", &resource.multiplexed);
    if (!rc)
        rc = append_item(xpl, (void **)&application->resources, &application->resource_count,
                         sizeof(resource), &resource);
    if (rc)
        free((char *)resource.src);
    return rc;
}

static int parse_application(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_application **applications,
                             size_t *count, hddvd_xpl_application_kind kind)
{
    struct xpl_application application;
    xmlNodePtr child;
    char *z_order;
    char *end;
    long value;
    int rc = HDDVD_OK;
    memset(&application, 0, sizeof(application));
    application.value.kind = kind;
    application.value.src = dup_prop(node, "src");
    application.value.language = dup_prop(node, "language");
    application.value.description = dup_prop(node, "description");
    if (!application.value.src)
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = bool_prop(node, "autorun", &application.value.autorun);
    if (!rc)
        rc = time_prop(node, "titleTimeBegin", xpl->frame_rate, &application.value.title_begin,
                       &application.value.has_title_begin);
    if (!rc)
        rc = time_prop(node, "titleTimeEnd", xpl->frame_rate, &application.value.title_end,
                       &application.value.has_title_end);
    z_order = dup_prop(node, "zOrder");
    if (!rc && z_order)
    {
        errno = 0;
        value = strtol(z_order, &end, 10);
        if (errno || *end || value < INT32_MIN || value > INT32_MAX)
            rc = HDDVD_EPARSE;
        else
        {
            application.value.z_order = (int32_t)value;
            application.value.has_z_order = 1;
        }
    }
    free(z_order);
    for (child = node->children; !rc && child; child = child->next)
        if (node_is(child, "PlaylistApplicationResource") || node_is(child, "ApplicationResource"))
            rc = parse_resource(child, xpl, &application);
    if (!rc)
        rc = append_item(xpl, (void **)applications, count, sizeof(application), &application);
    if (rc)
        free_application(&application);
    return rc;
}

static int parse_event(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_title *title)
{
    hddvd_xpl_event event;
    uint8_t present;
    int rc;
    memset(&event, 0, sizeof(event));
    event.kind = node_is(node, "PauseAt") ? HDDVD_XPL_EVENT_PAUSE : HDDVD_XPL_EVENT_SCHEDULED;
    event.id = dup_prop(node, "id");
    if (event.kind == HDDVD_XPL_EVENT_SCHEDULED && !event.id)
        return HDDVD_EPARSE;
    rc = time_prop(node, "titleTime", xpl->frame_rate, &event.time, &present);
    if (!rc && !present)
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = append_item(xpl, (void **)&title->events, &title->event_count, sizeof(event), &event);
    if (rc)
        free((char *)event.id);
    return rc;
}

static int walk_title_children(xmlNodePtr node, hddvd_xpl *xpl, struct xpl_title *title)
{
    xmlNodePtr child;
    for (child = node->children; child; child = child->next)
    {
        int rc = HDDVD_OK;
        if (child->type != XML_ELEMENT_NODE)
            continue;
        if (node_is(child, "PrimaryAudioVideoClip"))
        {
            xmlNodePtr stream;
            rc = parse_clip(child, xpl, title);
            for (stream = child->children; !rc && stream; stream = stream->next)
                if (stream->type == XML_ELEMENT_NODE && media_kind(stream))
                    rc = parse_track(stream, xpl, title);
        }
        else if (node_is(child, "Chapter"))
            rc = parse_chapter(child, xpl, title);
        else if (node_is(child, "VideoTrack") || node_is(child, "AudioTrack") ||
                 node_is(child, "SubpictureTrack") || node_is(child, "SubtitleTrack"))
            rc = parse_track(child, xpl, title);
        else if (node_is(child, "ApplicationSegment"))
            rc = parse_application(child, xpl, &title->applications, &title->application_count,
                                   HDDVD_XPL_APPLICATION_SEGMENT);
        else if (node_is(child, "Event") || node_is(child, "PauseAt"))
            rc = parse_event(child, xpl, title);
        if (rc)
            return rc;
        if (!node_is(child, "PrimaryAudioVideoClip") && child->children)
        {
            rc = walk_title_children(child, xpl, title);
            if (rc)
                return rc;
        }
    }
    return HDDVD_OK;
}

static int parse_title(xmlNodePtr node, hddvd_xpl *xpl, hddvd_xpl_title_kind kind)
{
    struct xpl_title title;
    uint64_t number = 0;
    uint8_t present;
    char *duration;
    int rc;
    memset(&title, 0, sizeof(title));
    title.value.kind = kind;
    title.value.id = dup_prop(node, "id");
    title.value.type = dup_prop(node, "type");
    title.value.display_name = dup_prop(node, "displayName");
    title.value.on_end = dup_prop(node, "onEnd");
    rc = uint_prop(node, "titleNumber", &number, &present);
    if (!rc && present && number > UINT32_MAX)
        rc = HDDVD_EPARSE;
    title.value.number = (uint32_t)number;
    title.value.has_number = present;
    if (!rc)
        rc = bool_prop(node, "selectable", &title.value.selectable);
    duration = dup_prop(node, "titleDuration");
    title.value.has_duration = duration != NULL;
    if (!rc && duration)
        rc = parse_time(duration, xpl->frame_rate, &title.value.duration);
    free(duration);
    if (!rc)
        rc = walk_title_children(node, xpl, &title);
    if (!rc)
        rc = append_item(xpl, (void **)&xpl->titles, &xpl->title_count, sizeof(title), &title);
    if (rc)
        free_title(&title);
    return rc;
}

static int parse_title_set(xmlNodePtr node, hddvd_xpl *xpl)
{
    xmlNodePtr child;
    char *time_base = dup_prop(node, "timeBase");
    int rc;
    if (!time_base)
        return HDDVD_EPARSE;
    rc = frame_rate_value(time_base, &xpl->frame_rate);
    free(time_base);
    if (rc)
        return rc;
    for (child = node->children; child; child = child->next)
    {
        if (node_is(child, "FirstPlayTitle"))
            rc = parse_title(child, xpl, HDDVD_XPL_TITLE_FIRST_PLAY);
        else if (node_is(child, "Title"))
            rc = parse_title(child, xpl, HDDVD_XPL_TITLE_REGULAR);
        else if (node_is(child, "PlaylistApplication"))
            rc = parse_application(child, xpl, &xpl->applications, &xpl->application_count,
                                   HDDVD_XPL_APPLICATION_PLAYLIST);
        else
            continue;
        if (rc)
            return rc;
    }
    return HDDVD_OK;
}

int hddvd_xpl_open_with_limits(const void *input, size_t input_size, const hddvd_xpl_limits *limits,
                               hddvd_xpl **out)
{
    hddvd_xpl *xpl;
    xmlDocPtr document;
    xmlNodePtr root;
    xmlNodePtr child;
    size_t nodes = 0;
    int rc = HDDVD_OK;
    if (!input || !input_size || !limits || !out || !limits->max_nodes || !limits->max_items ||
        input_size > INT_MAX)
        return HDDVD_EINVAL;
    *out = NULL;
    document = xmlReadMemory(input, (int)input_size, "playlist.xpl", NULL,
                             XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR |
                                 XML_PARSE_NOWARNING);
    if (!document)
        return HDDVD_EPARSE;
    root = xmlDocGetRootElement(document);
    if (!node_is(root, "Playlist") || !in_namespace(root))
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = count_nodes(root, limits->max_nodes, &nodes);
    xpl = rc ? NULL : calloc(1, sizeof(*xpl));
    if (!rc && !xpl)
        rc = HDDVD_ENOMEM;
    if (!rc)
    {
        xpl->item_limit = limits->max_items;
        xpl->namespace_uri = hddvd_strdup((const char *)root->ns->href);
        xpl->display_name = dup_prop(root, "displayName");
        xpl->type = dup_prop(root, "type");
        if (!xpl->namespace_uri)
            rc = HDDVD_ENOMEM;
    }
    for (child = root ? root->children : NULL; !rc && child; child = child->next)
    {
        if (node_is(child, "Configuration"))
            rc = parse_configuration(child, xpl);
        else if (node_is(child, "MediaAttributeList"))
            rc = parse_media(child, xpl);
        else if (node_is(child, "TitleSet"))
            rc = parse_title_set(child, xpl);
    }
    if (!rc && (!xpl->frame_rate || !xpl->title_count))
        rc = HDDVD_EPARSE;
    xmlFreeDoc(document);
    if (rc)
    {
        hddvd_xpl_close(xpl);
        return rc;
    }
    *out = xpl;
    return HDDVD_OK;
}

int hddvd_xpl_open(const void *input, size_t input_size, hddvd_xpl **out)
{
    hddvd_xpl_limits limits;
    hddvd_xpl_limits_default(&limits);
    return hddvd_xpl_open_with_limits(input, input_size, &limits, out);
}

const char *hddvd_xpl_namespace(const hddvd_xpl *xpl)
{
    return xpl ? xpl->namespace_uri : NULL;
}

const char *hddvd_xpl_display_name(const hddvd_xpl *xpl)
{
    return xpl ? xpl->display_name : NULL;
}

const char *hddvd_xpl_type(const hddvd_xpl *xpl)
{
    return xpl ? xpl->type : NULL;
}

uint32_t hddvd_xpl_frame_rate(const hddvd_xpl *xpl)
{
    return xpl ? xpl->frame_rate : 0;
}

int hddvd_xpl_get_configuration(const hddvd_xpl *xpl, hddvd_xpl_configuration *configuration)
{
    if (!xpl || !configuration)
        return HDDVD_EINVAL;
    *configuration = xpl->configuration;
    return HDDVD_OK;
}

size_t hddvd_xpl_media_attribute_count(const hddvd_xpl *xpl)
{
    return xpl ? xpl->media_count : 0;
}

int hddvd_xpl_get_media_attribute(const hddvd_xpl *xpl, size_t index,
                                  hddvd_xpl_media_attribute *value)
{
    if (!xpl || !value || index >= xpl->media_count)
        return HDDVD_EINVAL;
    *value = xpl->media[index];
    return HDDVD_OK;
}

size_t hddvd_xpl_title_count(const hddvd_xpl *xpl)
{
    return xpl ? xpl->title_count : 0;
}

int hddvd_xpl_get_title(const hddvd_xpl *xpl, size_t index, hddvd_xpl_title_info *value)
{
    if (!xpl || !value || index >= xpl->title_count)
        return HDDVD_EINVAL;
    *value = xpl->titles[index].value;
    return HDDVD_OK;
}

size_t hddvd_xpl_clip_count(const hddvd_xpl *xpl, size_t title)
{
    return xpl && title < xpl->title_count ? xpl->titles[title].clip_count : 0;
}

int hddvd_xpl_get_clip(const hddvd_xpl *xpl, size_t title, size_t index, hddvd_xpl_clip *value)
{
    if (!xpl || !value || title >= xpl->title_count || index >= xpl->titles[title].clip_count)
        return HDDVD_EINVAL;
    *value = xpl->titles[title].clips[index];
    return HDDVD_OK;
}

size_t hddvd_xpl_chapter_count(const hddvd_xpl *xpl, size_t title)
{
    return xpl && title < xpl->title_count ? xpl->titles[title].chapter_count : 0;
}

int hddvd_xpl_get_chapter(const hddvd_xpl *xpl, size_t title, size_t index,
                          hddvd_xpl_chapter *value)
{
    if (!xpl || !value || title >= xpl->title_count || index >= xpl->titles[title].chapter_count)
        return HDDVD_EINVAL;
    *value = xpl->titles[title].chapters[index];
    return HDDVD_OK;
}

size_t hddvd_xpl_track_count(const hddvd_xpl *xpl, size_t title)
{
    return xpl && title < xpl->title_count ? xpl->titles[title].track_count : 0;
}

int hddvd_xpl_get_track(const hddvd_xpl *xpl, size_t title, size_t index, hddvd_xpl_track *value)
{
    if (!xpl || !value || title >= xpl->title_count || index >= xpl->titles[title].track_count)
        return HDDVD_EINVAL;
    *value = xpl->titles[title].tracks[index];
    return HDDVD_OK;
}

static const struct xpl_application *applications_for(const hddvd_xpl *xpl, size_t title,
                                                      size_t *count)
{
    if (!xpl)
        return NULL;
    if (title == SIZE_MAX)
    {
        *count = xpl->application_count;
        return xpl->applications;
    }
    if (title >= xpl->title_count)
        return NULL;
    *count = xpl->titles[title].application_count;
    return xpl->titles[title].applications;
}

size_t hddvd_xpl_application_count(const hddvd_xpl *xpl, size_t title)
{
    size_t count = 0;
    (void)applications_for(xpl, title, &count);
    return count;
}

int hddvd_xpl_get_application(const hddvd_xpl *xpl, size_t title, size_t index,
                              hddvd_xpl_application *value)
{
    size_t count = 0;
    const struct xpl_application *applications = applications_for(xpl, title, &count);
    if (!applications || !value || index >= count)
        return HDDVD_EINVAL;
    *value = applications[index].value;
    return HDDVD_OK;
}

size_t hddvd_xpl_application_resource_count(const hddvd_xpl *xpl, size_t title, size_t application)
{
    size_t count = 0;
    const struct xpl_application *applications = applications_for(xpl, title, &count);
    return applications && application < count ? applications[application].resource_count : 0;
}

int hddvd_xpl_get_application_resource(const hddvd_xpl *xpl, size_t title, size_t application,
                                       size_t index, hddvd_xpl_application_resource *value)
{
    size_t count = 0;
    const struct xpl_application *applications = applications_for(xpl, title, &count);
    if (!applications || !value || application >= count ||
        index >= applications[application].resource_count)
        return HDDVD_EINVAL;
    *value = applications[application].resources[index];
    return HDDVD_OK;
}

size_t hddvd_xpl_event_count(const hddvd_xpl *xpl, size_t title)
{
    return xpl && title < xpl->title_count ? xpl->titles[title].event_count : 0;
}

int hddvd_xpl_get_event(const hddvd_xpl *xpl, size_t title, size_t index, hddvd_xpl_event *value)
{
    if (!xpl || !value || title >= xpl->title_count || index >= xpl->titles[title].event_count)
        return HDDVD_EINVAL;
    *value = xpl->titles[title].events[index];
    return HDDVD_OK;
}

static int name_is(xmlNodePtr node, const char *name)
{
    return node && node->type == XML_ELEMENT_NODE &&
           xmlStrcasecmp(node->name, (const xmlChar *)name) == 0;
}

static xmlChar *first_uri_attr(xmlNodePtr node)
{
    static const char *names[] = {"ref", "src", "href", "uri", NULL};
    int i;
    for (i = 0; names[i]; i++)
    {
        xmlChar *value = xmlGetProp(node, (const xmlChar *)names[i]);
        if (value)
            return value;
    }
    return NULL;
}

static int walk_start(xmlNodePtr node, int in_start, char **out)
{
    for (; node; node = node->next)
    {
        int here = in_start || name_is(node, "start");
        if (node->type == XML_ELEMENT_NODE && here)
        {
            xmlChar *value = first_uri_attr(node);
            if (value && *value)
            {
                *out = hddvd_strdup((const char *)value);
                xmlFree(value);
                return *out ? HDDVD_OK : HDDVD_ENOMEM;
            }
            if (value)
                xmlFree(value);
        }
        if (node->children)
        {
            int rc = walk_start(node->children, here, out);
            if (rc == HDDVD_OK || rc == HDDVD_ENOMEM)
                return rc;
        }
    }
    return HDDVD_ENOTFOUND;
}

int hddvd_xpl_find_start_ref(const char *path, char **out)
{
    xmlDocPtr document;
    int rc;
    if (!path || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    document = xmlReadFile(
        path, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!document)
        return HDDVD_EPARSE;
    rc = walk_start(xmlDocGetRootElement(document), 0, out);
    xmlFreeDoc(document);
    return rc;
}
