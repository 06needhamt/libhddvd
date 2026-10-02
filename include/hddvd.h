#ifndef LIBHDDVD_H
#define LIBHDDVD_H
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#ifdef HDDVD_BUILD_SHARED
#define HDDVD_API __declspec(dllexport)
#elif defined(HDDVD_USE_SHARED)
#define HDDVD_API __declspec(dllimport)
#else
#define HDDVD_API
#endif
#else
#define HDDVD_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C"
{
#endif
#define HDDVD_VERSION_MAJOR 0
#define HDDVD_VERSION_MINOR 4
#define HDDVD_VERSION_PATCH 0
#define HDDVD_ABI_VERSION 1u
#define HDDVD_PATH_MAX 1024

typedef enum hddvd_status
{
    HDDVD_OK = 0,
    HDDVD_EINVAL = -1,
    HDDVD_ENOMEM = -2,
    HDDVD_EIO = -3,
    HDDVD_EPARSE = -4,
    HDDVD_ENOTFOUND = -5,
    HDDVD_ENOTSUP = -6,
    HDDVD_ELIMIT = -7,
    HDDVD_EPROTECTED = -8
} hddvd_status;
typedef struct hddvd_ctx hddvd_ctx;
typedef struct hddvd_stream hddvd_stream;
typedef struct hddvd_aca hddvd_aca;
typedef struct hddvd_uri hddvd_uri;
typedef struct hddvd_xmf hddvd_xmf;
typedef struct hddvd_xpl hddvd_xpl;
typedef struct hddvd_object hddvd_object;
typedef struct hddvd_script hddvd_script;
typedef struct hddvd_image_cache hddvd_image_cache;
typedef struct hddvd_display_list hddvd_display_list;
typedef struct hddvd_nav hddvd_nav;
typedef struct hddvd_evo_parser hddvd_evo_parser;
typedef void (*hddvd_log_fn)(void *, int, const char *);
typedef enum hddvd_subsystem
{
    HDDVD_SUBSYSTEM_NONE = 0,
    HDDVD_SUBSYSTEM_DISC,
    HDDVD_SUBSYSTEM_RESOURCE,
    HDDVD_SUBSYSTEM_URI,
    HDDVD_SUBSYSTEM_ARCHIVE,
    HDDVD_SUBSYSTEM_STREAM
} hddvd_subsystem;
typedef struct hddvd_error_detail
{
    int status;
    hddvd_subsystem subsystem;
    uint64_t byte_offset;
    uint8_t has_byte_offset;
    char resource[HDDVD_PATH_MAX];
    char message[256];
} hddvd_error_detail;
typedef int (*hddvd_xmf_cb)(void *, const char *, const void *, size_t);
typedef struct hddvd_aca_entry
{
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint8_t flags;
    char name[256];
} hddvd_aca_entry;
typedef struct hddvd_limits
{
    size_t max_aca_entries;
    uint64_t max_aca_member_size;
} hddvd_limits;
typedef struct hddvd_xmf_limits
{
    size_t max_nodes;
    size_t max_references;
} hddvd_xmf_limits;
typedef struct hddvd_xmf_region
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} hddvd_xmf_region;
typedef struct hddvd_xpl_limits
{
    size_t max_nodes;
    size_t max_items;
} hddvd_xpl_limits;
typedef struct hddvd_xpl_time
{
    uint64_t frames;
    uint32_t frame_rate;
} hddvd_xpl_time;
typedef enum hddvd_xpl_media_kind
{
    HDDVD_XPL_MEDIA_VIDEO = 1,
    HDDVD_XPL_MEDIA_AUDIO,
    HDDVD_XPL_MEDIA_SUBPICTURE
} hddvd_xpl_media_kind;
typedef enum hddvd_xpl_title_kind
{
    HDDVD_XPL_TITLE_FIRST_PLAY = 1,
    HDDVD_XPL_TITLE_REGULAR
} hddvd_xpl_title_kind;
typedef enum hddvd_xpl_application_kind
{
    HDDVD_XPL_APPLICATION_SEGMENT = 1,
    HDDVD_XPL_APPLICATION_PLAYLIST
} hddvd_xpl_application_kind;
typedef enum hddvd_xpl_event_kind
{
    HDDVD_XPL_EVENT_SCHEDULED = 1,
    HDDVD_XPL_EVENT_PAUSE
} hddvd_xpl_event_kind;
typedef struct hddvd_xpl_configuration
{
    uint64_t streaming_buffer_size;
    uint32_t aperture_width;
    uint32_t aperture_height;
    uint32_t main_video_default_color;
    uint32_t network_timeout;
    uint8_t has_streaming_buffer_size;
    uint8_t has_aperture;
    uint8_t has_main_video_default_color;
    uint8_t has_network_timeout;
} hddvd_xpl_configuration;
typedef struct hddvd_xpl_media_attribute
{
    hddvd_xpl_media_kind kind;
    uint32_t index;
    const char *codec;
    const char *channels;
} hddvd_xpl_media_attribute;
typedef struct hddvd_xpl_title_info
{
    hddvd_xpl_title_kind kind;
    const char *id;
    uint32_t number;
    const char *type;
    const char *display_name;
    const char *on_end;
    hddvd_xpl_time duration;
    uint8_t selectable;
    uint8_t has_number;
    uint8_t has_duration;
} hddvd_xpl_title_info;
typedef struct hddvd_xpl_clip
{
    const char *src;
    hddvd_xpl_time title_begin;
    hddvd_xpl_time title_end;
    hddvd_xpl_time clip_begin;
    const char *data_source;
    uint8_t seamless;
    uint8_t has_title_begin;
    uint8_t has_title_end;
    uint8_t has_clip_begin;
} hddvd_xpl_clip;
typedef struct hddvd_xpl_chapter
{
    const char *id;
    const char *display_name;
    hddvd_xpl_time begin;
} hddvd_xpl_chapter;
typedef struct hddvd_xpl_track
{
    hddvd_xpl_media_kind kind;
    uint32_t track;
    uint32_t stream_number;
    uint32_t media_attribute;
    const char *language;
    uint8_t selectable;
    uint8_t has_stream_number;
    uint8_t has_media_attribute;
} hddvd_xpl_track;
typedef struct hddvd_xpl_application
{
    hddvd_xpl_application_kind kind;
    const char *src;
    const char *language;
    const char *description;
    int32_t z_order;
    hddvd_xpl_time title_begin;
    hddvd_xpl_time title_end;
    uint8_t autorun;
    uint8_t has_z_order;
    uint8_t has_title_begin;
    uint8_t has_title_end;
} hddvd_xpl_application;
typedef struct hddvd_xpl_application_resource
{
    const char *src;
    uint64_t size;
    uint8_t multiplexed;
    uint8_t has_size;
} hddvd_xpl_application_resource;
typedef struct hddvd_xpl_event
{
    hddvd_xpl_event_kind kind;
    const char *id;
    hddvd_xpl_time time;
} hddvd_xpl_event;
typedef struct hddvd_script_limits
{
    size_t max_strings;
    size_t max_functions;
    size_t max_instructions;
    uint64_t max_bytes;
} hddvd_script_limits;
typedef struct hddvd_script_function
{
    uint32_t name_index;
    uint32_t first_instruction;
    uint32_t instruction_count;
} hddvd_script_function;
typedef struct hddvd_script_instruction
{
    uint8_t opcode;
    uint8_t flags;
    int32_t operand;
} hddvd_script_instruction;
typedef struct hddvd_rect
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} hddvd_rect;
typedef struct hddvd_image_plane
{
    const uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    size_t stride;
    void (*release)(void *, const uint8_t *);
    void *release_opaque;
} hddvd_image_plane;
typedef int (*hddvd_image_decode_fn)(void *, const char *, const void *, size_t,
                                     hddvd_image_plane *);
typedef struct hddvd_display_limits
{
    size_t max_images;
    uint64_t max_encoded_bytes;
    uint64_t max_decoded_pixels;
    uint32_t max_dimension;
    size_t max_quads;
} hddvd_display_limits;
typedef struct hddvd_image_source
{
    const char *identity;
    const void *encoded;
    size_t encoded_size;
} hddvd_image_source;
/* Parents must precede children. Geometry is parent-relative; clip, when
   non-empty, is node-relative. Equal z values retain authored order. */
typedef struct hddvd_scene_node
{
    uint64_t id;
    size_t parent;
    hddvd_rect bounds;
    hddvd_rect clip;
    int32_t z_index;
    uint8_t opacity;
    uint8_t visible;
    uint8_t focused;
    uint8_t hit_test;
    hddvd_image_source normal_image;
    hddvd_image_source focused_image;
} hddvd_scene_node;
typedef struct hddvd_display_quad
{
    uint64_t node_id;
    hddvd_image_plane image;
    hddvd_rect source;
    hddvd_rect destination;
    hddvd_rect clip;
    int32_t z_index;
    uint8_t opacity;
} hddvd_display_quad;
typedef struct hddvd_video_transform
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t logical_width;
    uint32_t logical_height;
} hddvd_video_transform;
typedef int (*hddvd_aca_entry_cb)(void *, const hddvd_aca_entry *);

typedef enum hddvd_object_type
{
    HDDVD_OBJECT_UNKNOWN = 0,
    HDDVD_OBJECT_MANIFEST = 1,
    HDDVD_OBJECT_MARKUP = 2,
    HDDVD_OBJECT_SCRIPT = 3,
    HDDVD_OBJECT_IMAGE = 4
} hddvd_object_type;
typedef struct hddvd_object_info
{
    uint8_t version;
    uint8_t flags;
    uint16_t raw_type;
    uint32_t unknown_1;
    uint64_t declared_length;
    uint64_t payload_offset;
    uint64_t payload_length;
    uint64_t trailer_offset;
    uint32_t trailer_length;
} hddvd_object_info;
/* A provider returns a newly allocated clear payload in *output.  The
   library releases it with free().  Providers must not call back into the
   object being opened. */
typedef int (*hddvd_decrypt_fn)(void *, const hddvd_object_info *, const void *, size_t, void **,
                                size_t *);

typedef enum hddvd_file_kind
{
    HDDVD_FILE_UNKNOWN = 0,
    HDDVD_FILE_IFO,
    HDDVD_FILE_BUP,
    HDDVD_FILE_VTI,
    HDDVD_FILE_MAP,
    HDDVD_FILE_EVO,
    HDDVD_FILE_XPL,
    HDDVD_FILE_ACA,
    HDDVD_FILE_DISCID
} hddvd_file_kind;
typedef struct hddvd_file_info
{
    hddvd_file_kind kind;
    uint64_t size;
    char path[HDDVD_PATH_MAX];
} hddvd_file_info;
typedef int (*hddvd_file_cb)(void *, const hddvd_file_info *);

typedef enum hddvd_stream_codec
{
    HDDVD_CODEC_UNKNOWN = 0,
    HDDVD_CODEC_MPEG2_VIDEO,
    HDDVD_CODEC_H264,
    HDDVD_CODEC_VC1,
    HDDVD_CODEC_MPEG_AUDIO,
    HDDVD_CODEC_AC3_EAC3,
    HDDVD_CODEC_DTS_HD,
    HDDVD_CODEC_LPCM,
    HDDVD_CODEC_TRUEHD_MLP,
    HDDVD_CODEC_SUBPICTURE
} hddvd_stream_codec;
typedef struct hddvd_stream_info
{
    uint8_t stream_id;
    uint8_t substream_id;
    hddvd_stream_codec codec;
    uint64_t packet_count;
} hddvd_stream_info;

/* Title time is always expressed in the MPEG 90 kHz clock.  Conversion
   helpers reject negative values and saturation rather than wrapping. */
typedef struct hddvd_time
{
    uint64_t ticks;
} hddvd_time;
typedef enum hddvd_playback_state
{
    HDDVD_PLAYBACK_STOPPED = 0,
    HDDVD_PLAYBACK_PLAYING,
    HDDVD_PLAYBACK_PAUSED
} hddvd_playback_state;
typedef enum hddvd_event_type
{
    HDDVD_EVENT_CLOCK = 0,
    HDDVD_EVENT_PLAYBACK_STATE,
    HDDVD_EVENT_DISCONTINUITY,
    HDDVD_EVENT_TITLE_END,
    HDDVD_EVENT_STREAM_SELECTION,
    HDDVD_EVENT_COMMAND_ACK,
    HDDVD_EVENT_TIMER,
    HDDVD_EVENT_INPUT
} hddvd_event_type;
typedef enum hddvd_command_type
{
    HDDVD_COMMAND_PLAY = 0,
    HDDVD_COMMAND_PAUSE,
    HDDVD_COMMAND_SEEK,
    HDDVD_COMMAND_SELECT_TITLE,
    HDDVD_COMMAND_SELECT_PLAYLIST,
    HDDVD_COMMAND_SELECT_STREAM,
    HDDVD_COMMAND_SET_MENU_VISIBLE
} hddvd_command_type;
typedef enum hddvd_command_result
{
    HDDVD_COMMAND_ACCEPTED = 0,
    HDDVD_COMMAND_REJECTED,
    HDDVD_COMMAND_FAILED
} hddvd_command_result;
typedef struct hddvd_event
{
    hddvd_event_type type;
    hddvd_time when;
    uint64_t sequence;
    union
    {
        hddvd_playback_state playback_state;
        struct
        {
            uint32_t stream_type;
            uint32_t stream_id;
        } stream;
        struct
        {
            uint64_t command_id;
            hddvd_command_result result;
        } acknowledgement;
        struct
        {
            uint64_t timer_id;
        } timer;
        struct
        {
            uint32_t key;
            uint8_t pressed;
        } input;
    } data;
} hddvd_event;
typedef struct hddvd_command
{
    hddvd_command_type type;
    uint64_t id;
    union
    {
        hddvd_time seek;
        uint32_t selection;
        uint8_t visible;
    } data;
} hddvd_command;
typedef struct hddvd_nav_limits
{
    size_t max_events;
    size_t max_timers;
} hddvd_nav_limits;
typedef void (*hddvd_event_fn)(void *, const hddvd_event *);
typedef void (*hddvd_command_fn)(void *, const hddvd_command *);

typedef struct hddvd_map_info
{
    uint64_t size;
    uint32_t sector_count;
    int structure_validated;
} hddvd_map_info;

typedef struct hddvd_ifo_info
{
    uint64_t size;
    char identifier[17];
    int recognized_identifier;
} hddvd_ifo_info;

/* Capability bits describe independently usable library facilities, not
   complete interactive-menu conformance. Unknown bits must be ignored so
   applications built against an older header remain forward compatible. */
typedef uint64_t hddvd_features;
typedef enum hddvd_feature
{
    HDDVD_FEATURE_OBJECT_ENVELOPE = UINT64_C(1) << 0,
    HDDVD_FEATURE_MARKUP_INSPECTION = UINT64_C(1) << 1,
    HDDVD_FEATURE_SCRIPT_DECODER = UINT64_C(1) << 2,
    HDDVD_FEATURE_DISPLAY_LIST = UINT64_C(1) << 3,
    HDDVD_FEATURE_NAV_SCHEDULER = UINT64_C(1) << 4,
    HDDVD_FEATURE_PROTECTION_PROVIDER = UINT64_C(1) << 5,
    HDDVD_FEATURE_SCRIPT_RUNTIME = UINT64_C(1) << 6,
    HDDVD_FEATURE_IMAGE_DECODER = UINT64_C(1) << 7
} hddvd_feature;

HDDVD_API const char *hddvd_version(void);
HDDVD_API uint32_t hddvd_abi_version(void);
HDDVD_API hddvd_features hddvd_get_features(void);
HDDVD_API const char *hddvd_feature_name(hddvd_feature);
HDDVD_API const char *hddvd_strerror(int);
HDDVD_API hddvd_ctx *hddvd_open(const char *);
HDDVD_API void hddvd_close(hddvd_ctx *);
HDDVD_API const char *hddvd_root(const hddvd_ctx *);
HDDVD_API int hddvd_has_advanced_content(const hddvd_ctx *);
HDDVD_API void hddvd_set_log_callback(hddvd_ctx *, hddvd_log_fn, void *);
/* Context diagnostics have the same thread affinity as their context.
   Successful context operations clear the previous diagnostic.
   Returned records are copies and remain owned by the caller. */
HDDVD_API int hddvd_get_last_error(const hddvd_ctx *, hddvd_error_detail *);
HDDVD_API void hddvd_clear_error(hddvd_ctx *);
/* Streams are single-threaded. Reads may be short only at EOF. Memory input
   is copied. Closing an ACA does not invalidate an open member stream. */
HDDVD_API int hddvd_stream_open_file(const char *, hddvd_stream **);
HDDVD_API int hddvd_stream_open_memory(const void *, size_t, hddvd_stream **);
HDDVD_API uint64_t hddvd_stream_size(const hddvd_stream *);
HDDVD_API int hddvd_stream_read_at(hddvd_stream *, uint64_t, void *, size_t, size_t *);
HDDVD_API void hddvd_stream_close(hddvd_stream *);
/* Object input is copied/retained by the returned immutable handle. */
HDDVD_API int hddvd_object_open(const void *, size_t, hddvd_object **);
HDDVD_API int hddvd_object_open_with_provider(const void *, size_t, hddvd_decrypt_fn, void *,
                                              hddvd_object **);
HDDVD_API int hddvd_object_open_stream(hddvd_stream *, hddvd_object **);
HDDVD_API int hddvd_object_open_unwrapped(hddvd_object_type, const void *, size_t, hddvd_object **);
HDDVD_API void hddvd_object_close(hddvd_object *);
HDDVD_API int hddvd_object_get_info(const hddvd_object *, hddvd_object_info *);
HDDVD_API hddvd_object_type hddvd_object_get_type(const hddvd_object *);
HDDVD_API const char *hddvd_object_name(const hddvd_object *);
HDDVD_API int hddvd_object_open_payload(const hddvd_object *, hddvd_stream **);
HDDVD_API int hddvd_object_get_trailer(const hddvd_object *, const void **, size_t *);
/* The decoder exposes validated, engine-neutral script structure. Strings
   are borrowed and all indexes remain valid until hddvd_script_close(). */
HDDVD_API void hddvd_script_limits_default(hddvd_script_limits *);
HDDVD_API int hddvd_script_open(const void *, size_t, hddvd_script **);
HDDVD_API int hddvd_script_open_with_limits(const void *, size_t, const hddvd_script_limits *,
                                            hddvd_script **);
HDDVD_API void hddvd_script_close(hddvd_script *);
HDDVD_API uint16_t hddvd_script_version(const hddvd_script *);
HDDVD_API size_t hddvd_script_string_count(const hddvd_script *);
HDDVD_API const char *hddvd_script_string(const hddvd_script *, size_t);
HDDVD_API size_t hddvd_script_function_count(const hddvd_script *);
HDDVD_API int hddvd_script_get_function(const hddvd_script *, size_t, hddvd_script_function *);
HDDVD_API size_t hddvd_script_instruction_count(const hddvd_script *);
HDDVD_API int hddvd_script_get_instruction(const hddvd_script *, size_t,
                                           hddvd_script_instruction *);
/* Decoders must return premultiplied RGBA8. The cache calls release exactly
   once after the last display list using a plane is closed. All objects are
   single-threaded and callbacks are made only by cache_get/build calls. */
HDDVD_API void hddvd_display_limits_default(hddvd_display_limits *);
HDDVD_API int hddvd_image_cache_create(hddvd_image_decode_fn, void *, const hddvd_display_limits *,
                                       hddvd_image_cache **);
HDDVD_API void hddvd_image_cache_destroy(hddvd_image_cache *);
HDDVD_API int hddvd_display_build(hddvd_image_cache *, uint32_t, uint32_t, const hddvd_scene_node *,
                                  size_t, const hddvd_display_list *, hddvd_display_list **);
HDDVD_API void hddvd_display_list_close(hddvd_display_list *);
HDDVD_API size_t hddvd_display_quad_count(const hddvd_display_list *);
HDDVD_API int hddvd_display_get_quad(const hddvd_display_list *, size_t, hddvd_display_quad *);
HDDVD_API size_t hddvd_display_damage_count(const hddvd_display_list *);
HDDVD_API int hddvd_display_get_damage(const hddvd_display_list *, size_t, hddvd_rect *);
HDDVD_API void hddvd_video_transform_fit(uint32_t, uint32_t, uint32_t, uint32_t,
                                         hddvd_video_transform *);
HDDVD_API int hddvd_video_to_logical(const hddvd_video_transform *, int32_t, int32_t, int32_t *,
                                     int32_t *);
HDDVD_API int hddvd_display_hit_test(const hddvd_display_list *, int32_t, int32_t, uint64_t *);
HDDVD_API int hddvd_scan_files(const hddvd_ctx *, hddvd_file_cb, void *);
HDDVD_API const char *hddvd_file_kind_name(hddvd_file_kind);
HDDVD_API int hddvd_xpl_find_start_ref(const char *, char **);
/* XPL objects are immutable. Strings in returned records are borrowed until
   hddvd_xpl_close(). Times are normalized to frames using TitleSet timeBase. */
HDDVD_API void hddvd_xpl_limits_default(hddvd_xpl_limits *);
HDDVD_API int hddvd_xpl_open(const void *, size_t, hddvd_xpl **);
HDDVD_API int hddvd_xpl_open_with_limits(const void *, size_t, const hddvd_xpl_limits *,
                                         hddvd_xpl **);
HDDVD_API void hddvd_xpl_close(hddvd_xpl *);
HDDVD_API const char *hddvd_xpl_namespace(const hddvd_xpl *);
HDDVD_API const char *hddvd_xpl_display_name(const hddvd_xpl *);
HDDVD_API const char *hddvd_xpl_type(const hddvd_xpl *);
HDDVD_API uint32_t hddvd_xpl_frame_rate(const hddvd_xpl *);
HDDVD_API int hddvd_xpl_get_configuration(const hddvd_xpl *, hddvd_xpl_configuration *);
HDDVD_API size_t hddvd_xpl_media_attribute_count(const hddvd_xpl *);
HDDVD_API int hddvd_xpl_get_media_attribute(const hddvd_xpl *, size_t, hddvd_xpl_media_attribute *);
HDDVD_API size_t hddvd_xpl_title_count(const hddvd_xpl *);
HDDVD_API int hddvd_xpl_get_title(const hddvd_xpl *, size_t, hddvd_xpl_title_info *);
HDDVD_API size_t hddvd_xpl_clip_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_clip(const hddvd_xpl *, size_t, size_t, hddvd_xpl_clip *);
HDDVD_API size_t hddvd_xpl_chapter_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_chapter(const hddvd_xpl *, size_t, size_t, hddvd_xpl_chapter *);
HDDVD_API size_t hddvd_xpl_track_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_track(const hddvd_xpl *, size_t, size_t, hddvd_xpl_track *);
/* Use SIZE_MAX as the title index for playlist-wide applications. */
HDDVD_API size_t hddvd_xpl_application_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_application(const hddvd_xpl *, size_t, size_t, hddvd_xpl_application *);
HDDVD_API size_t hddvd_xpl_application_resource_count(const hddvd_xpl *, size_t, size_t);
HDDVD_API int hddvd_xpl_get_application_resource(const hddvd_xpl *, size_t, size_t, size_t,
                                                 hddvd_xpl_application_resource *);
HDDVD_API size_t hddvd_xpl_event_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_event(const hddvd_xpl *, size_t, size_t, hddvd_xpl_event *);
HDDVD_API int hddvd_xmf_dump(const void *, size_t, const char *, void *);
/* XMF objects are immutable. All returned strings are borrowed and remain
   valid until hddvd_xmf_close(). Inputs are copied by the parser. */
HDDVD_API void hddvd_xmf_limits_default(hddvd_xmf_limits *);
HDDVD_API int hddvd_xmf_open(const void *, size_t, hddvd_xmf **);
HDDVD_API int hddvd_xmf_open_with_limits(const void *, size_t, const hddvd_xmf_limits *,
                                         hddvd_xmf **);
HDDVD_API void hddvd_xmf_close(hddvd_xmf *);
HDDVD_API const char *hddvd_xmf_application_id(const hddvd_xmf *);
HDDVD_API int hddvd_xmf_get_region(const hddvd_xmf *, hddvd_xmf_region *);
HDDVD_API size_t hddvd_xmf_script_count(const hddvd_xmf *);
HDDVD_API int hddvd_xmf_get_script(const hddvd_xmf *, size_t, const char **, const char **);
HDDVD_API size_t hddvd_xmf_markup_count(const hddvd_xmf *);
HDDVD_API int hddvd_xmf_get_markup(const hddvd_xmf *, size_t, const char **, const char **);
HDDVD_API size_t hddvd_xmf_resource_root_count(const hddvd_xmf *);
HDDVD_API const char *hddvd_xmf_resource_root(const hddvd_xmf *, size_t);
HDDVD_API int hddvd_aca_supported(void);
HDDVD_API int hddvd_aca_open(const char *, hddvd_aca **);
HDDVD_API int hddvd_aca_open_stream(hddvd_stream *, hddvd_aca **);
HDDVD_API int hddvd_aca_open_stream_with_limits(hddvd_stream *, const hddvd_limits *, hddvd_aca **);
HDDVD_API void hddvd_limits_default(hddvd_limits *);
HDDVD_API void hddvd_aca_close(hddvd_aca *);
HDDVD_API size_t hddvd_aca_count(const hddvd_aca *);
HDDVD_API int hddvd_aca_get_entry(const hddvd_aca *, size_t, hddvd_aca_entry *);
HDDVD_API int hddvd_aca_open_member(const hddvd_aca *, const char *, hddvd_stream **);
HDDVD_API int hddvd_aca_list(const char *, hddvd_aca_entry_cb, void *);
HDDVD_API int hddvd_aca_verify(const char *, size_t *);
HDDVD_API int hddvd_aca_read_member(const char *, const char *, void **, size_t *);
HDDVD_API int hddvd_aca_extract(const char *, const char *);
HDDVD_API int hddvd_aca_visit_xmf(const char *, hddvd_xmf_cb, void *);
/* URI getters return borrowed strings valid until hddvd_uri_free(). */
HDDVD_API int hddvd_uri_parse(const char *, hddvd_uri **);
HDDVD_API void hddvd_uri_free(hddvd_uri *);
HDDVD_API const char *hddvd_uri_disc_path(const hddvd_uri *);
HDDVD_API const char *hddvd_uri_archive_name(const hddvd_uri *);
HDDVD_API const char *hddvd_uri_member_name(const hddvd_uri *);
HDDVD_API int hddvd_open_resource(const hddvd_ctx *, const char *, hddvd_stream **);
HDDVD_API int hddvd_xmu_dump(const void *, size_t, const char *, void *);
HDDVD_API int hddvd_time_from_milliseconds(uint64_t, hddvd_time *);
HDDVD_API int hddvd_time_to_milliseconds(hddvd_time, uint64_t *);
HDDVD_API int hddvd_time_from_seconds(uint64_t, hddvd_time *);
/* Navigation objects and callbacks have navigation-thread affinity. Events
   with equal timestamps are delivered in posting order. The bounded queue
   rejects its newest event with HDDVD_ELIMIT and never discards old work.
   Host command callbacks run synchronously and must acknowledge later by
   posting HDDVD_EVENT_COMMAND_ACK (including if the command is rejected). */
HDDVD_API void hddvd_nav_limits_default(hddvd_nav_limits *);
HDDVD_API int hddvd_nav_create(const hddvd_nav_limits *, hddvd_event_fn, hddvd_command_fn, void *,
                               hddvd_nav **);
HDDVD_API void hddvd_nav_destroy(hddvd_nav *);
HDDVD_API int hddvd_nav_post(hddvd_nav *, const hddvd_event *);
HDDVD_API int hddvd_nav_schedule_timer(hddvd_nav *, uint64_t, hddvd_time);
HDDVD_API int hddvd_nav_cancel_timer(hddvd_nav *, uint64_t);
HDDVD_API int hddvd_nav_advance(hddvd_nav *, hddvd_time);
HDDVD_API size_t hddvd_nav_pump(hddvd_nav *, size_t);
HDDVD_API int hddvd_nav_command(hddvd_nav *, const hddvd_command *, uint64_t *);
/* The incremental EVO parser accepts arbitrarily split input. finish()
   reports a truncated PES packet; metadata remains queryable afterward. */
HDDVD_API int hddvd_evo_parser_create(hddvd_evo_parser **);
HDDVD_API void hddvd_evo_parser_destroy(hddvd_evo_parser *);
HDDVD_API int hddvd_evo_parser_push(hddvd_evo_parser *, const void *, size_t);
HDDVD_API int hddvd_evo_parser_finish(hddvd_evo_parser *);
HDDVD_API size_t hddvd_evo_parser_stream_count(const hddvd_evo_parser *);
HDDVD_API int hddvd_evo_parser_get_stream(const hddvd_evo_parser *, size_t, hddvd_stream_info *);
HDDVD_API int hddvd_evo_probe(const char *, hddvd_stream_info *, size_t, size_t *);
HDDVD_API const char *hddvd_codec_name(hddvd_stream_codec);
HDDVD_API int hddvd_map_probe(const char *, hddvd_map_info *);
HDDVD_API int hddvd_ifo_probe(const char *, hddvd_ifo_info *);
HDDVD_API void hddvd_free(void *);
#ifdef __cplusplus
}
#endif
#endif
