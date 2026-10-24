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
    HDDVD_EPROTECTED = -8,
    HDDVD_ESCRIPT = -9
} hddvd_status;
typedef struct hddvd_ctx hddvd_ctx;
typedef struct hddvd_stream hddvd_stream;
typedef struct hddvd_aca hddvd_aca;
typedef struct hddvd_uri hddvd_uri;
typedef struct hddvd_xmf hddvd_xmf;
typedef struct hddvd_xmu hddvd_xmu;
typedef struct hddvd_xmu_styles hddvd_xmu_styles;
typedef struct hddvd_application hddvd_application;
typedef struct hddvd_xpl hddvd_xpl;
typedef struct hddvd_object hddvd_object;
typedef struct hddvd_script hddvd_script;
typedef struct hddvd_runtime hddvd_runtime;
typedef struct hddvd_image_cache hddvd_image_cache;
typedef struct hddvd_display_list hddvd_display_list;
typedef struct hddvd_nav hddvd_nav;
typedef struct hddvd_runtime_bridge hddvd_runtime_bridge;
typedef struct hddvd_evo_parser hddvd_evo_parser;
typedef struct hddvd_vti hddvd_vti;
typedef struct hddvd_tmap hddvd_tmap;
typedef struct hddvd_vts hddvd_vts;
typedef struct hddvd_vmg hddvd_vmg;
typedef struct hddvd_title_map hddvd_title_map;
typedef struct hddvd_scene hddvd_scene;
typedef void (*hddvd_log_fn)(void *, int, const char *);
typedef enum hddvd_subsystem
{
    HDDVD_SUBSYSTEM_NONE = 0,
    HDDVD_SUBSYSTEM_DISC,
    HDDVD_SUBSYSTEM_RESOURCE,
    HDDVD_SUBSYSTEM_URI,
    HDDVD_SUBSYSTEM_ARCHIVE,
    HDDVD_SUBSYSTEM_STREAM,
    HDDVD_SUBSYSTEM_TIME_MAP
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
typedef enum hddvd_dependency_kind
{
    HDDVD_DEPENDENCY_SCRIPT = 1,
    HDDVD_DEPENDENCY_MARKUP,
    HDDVD_DEPENDENCY_IMAGE
} hddvd_dependency_kind;
typedef struct hddvd_dependency
{
    hddvd_dependency_kind kind;
    const char *id;
    const char *uri;
    int status;
} hddvd_dependency;
typedef enum hddvd_requirement_kind
{
    HDDVD_REQUIREMENT_ELEMENT = 1,
    HDDVD_REQUIREMENT_STYLE_PROPERTY
} hddvd_requirement_kind;
typedef struct hddvd_requirement
{
    hddvd_requirement_kind kind;
    const char *namespace_uri;
    const char *name;
    size_t occurrences;
    uint8_t supported;
} hddvd_requirement;
typedef struct hddvd_xmu_limits
{
    size_t max_depth;
    size_t max_nodes;
    size_t max_attributes;
} hddvd_xmu_limits;
typedef struct hddvd_xmu_node
{
    const char *namespace_uri;
    const char *name;
    const char *id;
    const char *text;
    size_t parent;
    size_t first_child;
    size_t next_sibling;
    size_t child_count;
    size_t attribute_count;
} hddvd_xmu_node;
typedef struct hddvd_xmu_attribute
{
    const char *namespace_uri;
    const char *name;
    const char *value;
} hddvd_xmu_attribute;
typedef enum hddvd_xmu_display
{
    HDDVD_XMU_DISPLAY_BLOCK = 0,
    HDDVD_XMU_DISPLAY_NONE = 1,
    HDDVD_XMU_DISPLAY_AUTO = 2
} hddvd_xmu_display;
typedef enum hddvd_xmu_style_property
{
    HDDVD_XMU_STYLE_X = 1u << 0,
    HDDVD_XMU_STYLE_Y = 1u << 1,
    HDDVD_XMU_STYLE_WIDTH = 1u << 2,
    HDDVD_XMU_STYLE_HEIGHT = 1u << 3,
    HDDVD_XMU_STYLE_DISPLAY = 1u << 4,
    HDDVD_XMU_STYLE_Z_INDEX = 1u << 5,
    HDDVD_XMU_STYLE_BACKGROUND_IMAGE = 1u << 6,
    HDDVD_XMU_STYLE_SCALE_TO_FIT = 1u << 7,
    HDDVD_XMU_STYLE_NAV_LEFT = 1u << 8,
    HDDVD_XMU_STYLE_NAV_RIGHT = 1u << 9,
    HDDVD_XMU_STYLE_NAV_UP = 1u << 10,
    HDDVD_XMU_STYLE_NAV_DOWN = 1u << 11,
    HDDVD_XMU_STYLE_POSITION = 1u << 12,
    /* width or height holds a whole percentage instead of pixels. */
    HDDVD_XMU_STYLE_WIDTH_PERCENT = 1u << 13,
    HDDVD_XMU_STYLE_HEIGHT_PERCENT = 1u << 14
} hddvd_xmu_style_property;
typedef struct hddvd_xmu_style
{
    uint32_t properties;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t z_index;
    hddvd_xmu_display display;
    uint8_t position_absolute;
    uint8_t scale_to_fit;
    size_t nav_left;
    size_t nav_right;
    size_t nav_up;
    size_t nav_down;
    size_t background_image_count;
} hddvd_xmu_style;
/* Style properties beyond hddvd_xmu_style, each flagged in properties when
   declared. "inherit" is recorded as declared (opacity_inherit, or the
   INHERIT visibility); computed values include style references but do not
   inherit from parent elements. */
typedef enum hddvd_xmu_detail_property
{
    HDDVD_XMU_DETAIL_OPACITY = 1u << 0,
    HDDVD_XMU_DETAIL_VISIBILITY = 1u << 1,
    HDDVD_XMU_DETAIL_COLOR = 1u << 2,
    HDDVD_XMU_DETAIL_BACKGROUND_COLOR = 1u << 3,
    HDDVD_XMU_DETAIL_FONT = 1u << 4,
    HDDVD_XMU_DETAIL_FONT_SIZE = 1u << 5,
    HDDVD_XMU_DETAIL_FONT_STYLE = 1u << 6,
    HDDVD_XMU_DETAIL_TEXT_ALIGN = 1u << 7,
    HDDVD_XMU_DETAIL_DISPLAY_ALIGN = 1u << 8,
    HDDVD_XMU_DETAIL_WRAP = 1u << 9,
    HDDVD_XMU_DETAIL_TEXT_ALTITUDE = 1u << 10,
    HDDVD_XMU_DETAIL_ANCHOR = 1u << 11,
    HDDVD_XMU_DETAIL_PADDING_BEFORE = 1u << 12,
    HDDVD_XMU_DETAIL_PADDING_AFTER = 1u << 13,
    HDDVD_XMU_DETAIL_PADDING_START = 1u << 14,
    HDDVD_XMU_DETAIL_PADDING_END = 1u << 15,
    HDDVD_XMU_DETAIL_BORDER = 1u << 16,
    HDDVD_XMU_DETAIL_BACKGROUND_X = 1u << 17,
    HDDVD_XMU_DETAIL_BACKGROUND_Y = 1u << 18,
    HDDVD_XMU_DETAIL_BACKGROUND_REPEAT = 1u << 19,
    HDDVD_XMU_DETAIL_BACKGROUND_FRAME = 1u << 20,
    HDDVD_XMU_DETAIL_CROP = 1u << 21,
    HDDVD_XMU_DETAIL_CONTENT_WIDTH = 1u << 22,
    HDDVD_XMU_DETAIL_CONTENT_HEIGHT = 1u << 23,
    HDDVD_XMU_DETAIL_NAV_LEFT_UP = 1u << 24,
    HDDVD_XMU_DETAIL_NAV_LEFT_DOWN = 1u << 25,
    HDDVD_XMU_DETAIL_NAV_RIGHT_UP = 1u << 26,
    HDDVD_XMU_DETAIL_NAV_RIGHT_DOWN = 1u << 27,
    HDDVD_XMU_DETAIL_NAV_INDEX = 1u << 28
} hddvd_xmu_detail_property;
typedef enum hddvd_xmu_visibility
{
    HDDVD_XMU_VISIBILITY_VISIBLE = 0,
    HDDVD_XMU_VISIBILITY_HIDDEN,
    HDDVD_XMU_VISIBILITY_INHERIT
} hddvd_xmu_visibility;
typedef enum hddvd_xmu_font_style
{
    HDDVD_XMU_FONT_STYLE_NORMAL = 0,
    HDDVD_XMU_FONT_STYLE_ITALIC,
    HDDVD_XMU_FONT_STYLE_OBLIQUE
} hddvd_xmu_font_style;
/* textAlign uses START/CENTER/END/LEFT/RIGHT; displayAlign uses BEFORE
   (START), CENTER and AFTER (END). */
typedef enum hddvd_xmu_align
{
    HDDVD_XMU_ALIGN_START = 0,
    HDDVD_XMU_ALIGN_CENTER,
    HDDVD_XMU_ALIGN_END,
    HDDVD_XMU_ALIGN_LEFT,
    HDDVD_XMU_ALIGN_RIGHT
} hddvd_xmu_align;
/* backgroundPositionHorizontal (left/center/right) and
   backgroundPositionVertical (top/center/bottom), or a pixel or percentage
   offset. */
typedef enum hddvd_xmu_background_position
{
    HDDVD_XMU_BACKGROUND_START = 0,
    HDDVD_XMU_BACKGROUND_CENTER,
    HDDVD_XMU_BACKGROUND_END,
    HDDVD_XMU_BACKGROUND_OFFSET,
    /* The offset field holds a whole percentage. */
    HDDVD_XMU_BACKGROUND_PERCENT
} hddvd_xmu_background_position;
typedef struct hddvd_xmu_style_details
{
    uint32_t properties;
    /* opacity in thousandths, 0 to 1000. */
    uint16_t opacity;
    uint8_t opacity_inherit;
    hddvd_xmu_visibility visibility;
    /* Colors are 0xRRGGBBAA. */
    uint32_t color;
    uint32_t background_color;
    /* font is the declared font resource; anchor and border are kept as
       declared. Strings are borrowed until hddvd_xmu_styles_close(). */
    const char *font;
    const char *anchor;
    const char *border;
    uint32_t font_size;
    hddvd_xmu_font_style font_style;
    hddvd_xmu_align text_align;
    hddvd_xmu_align display_align;
    uint8_t wrap;
    uint8_t background_repeat;
    uint32_t text_altitude_percent;
    uint32_t padding_before;
    uint32_t padding_after;
    uint32_t padding_start;
    uint32_t padding_end;
    hddvd_xmu_background_position background_x;
    hddvd_xmu_background_position background_y;
    int32_t background_x_offset;
    int32_t background_y_offset;
    uint32_t background_frame;
    /* crop's four integers in declared order; their meaning is unconfirmed. */
    int32_t crop[4];
    uint32_t content_width_percent;
    uint32_t content_height_percent;
    /* Diagonal navigation targets; SIZE_MAX when "none" or unresolved. */
    size_t nav_left_up;
    size_t nav_left_down;
    size_t nav_right_up;
    size_t nav_right_down;
    /* navIndex: "none" sets the property with nav_index_none. */
    uint32_t nav_index;
    uint8_t nav_index_none;
} hddvd_xmu_style_details;
/* A style property that was declared but is not represented: an unknown
   property (value is NULL) or a known one with an unrecognized keyword. */
typedef struct hddvd_xmu_style_unmodeled
{
    const char *property;
    const char *value;
    size_t occurrences;
} hddvd_xmu_style_unmodeled;
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
typedef enum hddvd_xpl_clip_kind
{
    HDDVD_XPL_CLIP_PRIMARY_AUDIO_VIDEO = 1,
    HDDVD_XPL_CLIP_SUBSTITUTE_AUDIO_VIDEO,
    HDDVD_XPL_CLIP_SUBSTITUTE_AUDIO,
    HDDVD_XPL_CLIP_SECONDARY_AUDIO_VIDEO,
    HDDVD_XPL_CLIP_ADVANCED_SUBTITLE
} hddvd_xpl_clip_kind;
typedef enum hddvd_xpl_application_kind
{
    HDDVD_XPL_APPLICATION_SEGMENT = 1,
    HDDVD_XPL_APPLICATION_PLAYLIST
} hddvd_xpl_application_kind;
/* Authored lifecycle window of an application at a title time. ACTIVE covers
   [titleTimeBegin, titleTimeEnd), where a missing begin is the title start
   and a missing end is the title duration (or unbounded without one).
   LOADING covers [loadingBegin, titleTimeBegin). Whether an application with
   autorun="false" actually runs is a runtime decision, not part of this
   state. */
typedef enum hddvd_xpl_application_state
{
    HDDVD_XPL_APPLICATION_INACTIVE = 0,
    HDDVD_XPL_APPLICATION_LOADING,
    HDDVD_XPL_APPLICATION_ACTIVE
} hddvd_xpl_application_state;
/* A playlist construct that parsed successfully but that the typed model does
   not represent: an element name (attribute is NULL) or an attribute of a
   modeled element. Names are local names; records are deduplicated in
   document order. */
typedef struct hddvd_xpl_unmodeled
{
    const char *element;
    const char *attribute;
    size_t occurrences;
} hddvd_xpl_unmodeled;
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
/* Attributes recorded beside the core playlist records. Strings are
   borrowed until hddvd_xpl_close() and NULL when absent. Enumerated values
   are kept as authored, because only some of their spellings have been
   observed on discs. */
typedef struct hddvd_xpl_title_details
{
    const char *description;
    /* alternativeSDDisplayMode, such as "letterbox". */
    const char *alternative_sd_display_mode;
    uint32_t tick_base_divisor;
    uint8_t has_tick_base_divisor;
} hddvd_xpl_title_details;
typedef struct hddvd_xpl_track_details
{
    const char *description;
    uint32_t angle_number;
    uint8_t has_angle_number;
    uint8_t forced;
    uint8_t has_forced;
    /* Set for SubVideo and SubAudio, the secondary (picture-in-picture)
       streams of a clip. */
    uint8_t secondary;
} hddvd_xpl_track_details;
/* ApplicationSegment/PlaylistApplication sync. OTHER means a value outside
   hard, soft and none; the authored text is kept in the details record. */
typedef enum hddvd_xpl_sync
{
    HDDVD_XPL_SYNC_UNSPECIFIED = 0,
    HDDVD_XPL_SYNC_HARD,
    HDDVD_XPL_SYNC_SOFT,
    HDDVD_XPL_SYNC_NONE,
    HDDVD_XPL_SYNC_OTHER
} hddvd_xpl_sync;
typedef struct hddvd_xpl_application_details
{
    const char *id;
    const char *sync_text;
    hddvd_xpl_sync sync;
} hddvd_xpl_application_details;
typedef struct hddvd_xpl_resource_details
{
    const char *description;
    uint32_t priority;
    hddvd_xpl_time loading_begin;
    uint8_t has_priority;
    uint8_t has_loading_begin;
} hddvd_xpl_resource_details;
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
/* Budgets for a script runtime. Heap is the memory the engine holds in
   total; the others apply afresh to each evaluate or call. */
typedef struct hddvd_runtime_limits
{
    /* Largest source accepted by hddvd_runtime_evaluate, in bytes. */
    uint64_t max_source_bytes;
    uint64_t max_heap_bytes;
    /* Nested calls that always succeed. The engine stops scripts at
       max_recursion * HDDVD_RUNTIME_FRAME_BYTES of native stack, which the
       calling thread must have free; how much deeper a script can go depends
       on its frames and the engine build. */
    size_t max_recursion;
    /* Loop iterations and calls. The engine checks the count in blocks
       (10,000 with QuickJS), so a script may run up to one block past it. */
    uint64_t max_operations;
    /* Wall-clock time, checked at the same points as max_operations. */
    uint32_t max_milliseconds;
} hddvd_runtime_limits;
#define HDDVD_RUNTIME_FRAME_BYTES 16384u
/* The budget that ended an evaluate or call with HDDVD_ELIMIT. */
typedef enum hddvd_runtime_limit
{
    HDDVD_RUNTIME_LIMIT_NONE = 0,
    HDDVD_RUNTIME_LIMIT_SOURCE = 1,
    HDDVD_RUNTIME_LIMIT_HEAP = 2,
    HDDVD_RUNTIME_LIMIT_RECURSION = 3,
    HDDVD_RUNTIME_LIMIT_OPERATIONS = 4,
    HDDVD_RUNTIME_LIMIT_TIME = 5
} hddvd_runtime_limit;
/* Outcome of the last evaluate or call. Strings are borrowed until the next
   evaluate, call or destroy, and are NULL when absent. */
typedef struct hddvd_runtime_diagnostic
{
    int status;
    hddvd_runtime_limit limit;
    /* The engine's description, such as "TypeError: x is not a function". */
    const char *message;
    /* The engine's call trace, one frame per line. */
    const char *trace;
} hddvd_runtime_diagnostic;
/* Something a runtime noticed while running scripts on the host's behalf. */
typedef enum hddvd_runtime_issue_kind
{
    /* A listener, timer, handler or animation step threw; the rest still ran. */
    HDDVD_RUNTIME_ISSUE_EXCEPTION = 1,
    /* A script set a style value the style model cannot apply (an unknown
       property or keyword, or a malformed value). Scripts read it back, but
       hddvd_runtime_styles does not change. */
    HDDVD_RUNTIME_ISSUE_STYLE = 2,
    /* The host rejected or failed a player command (hddvd_runtime_acknowledge). */
    HDDVD_RUNTIME_ISSUE_COMMAND = 3,
    /* A script called Diagnostics.trace(). */
    HDDVD_RUNTIME_ISSUE_TRACE = 4
} hddvd_runtime_issue_kind;
/* message is borrowed until hddvd_runtime_clear_issues or destroy. node is
   the markup node concerned, or SIZE_MAX. */
typedef struct hddvd_runtime_issue
{
    hddvd_runtime_issue_kind kind;
    size_t node;
    const char *message;
} hddvd_runtime_issue;
/* Controller key codes, as disc scripts define them (VK_*). The runtime acts
   on these by default; hddvd_runtime_key passes any other code to scripts
   only. */
typedef enum hddvd_runtime_key_code
{
    HDDVD_KEY_ENTER = 0x0d,
    HDDVD_KEY_LEFT = 0x25,
    HDDVD_KEY_UP = 0x26,
    HDDVD_KEY_RIGHT = 0x27,
    HDDVD_KEY_DOWN = 0x28,
    HDDVD_KEY_UP_LEFT = 0x29,
    HDDVD_KEY_UP_RIGHT = 0x2a,
    HDDVD_KEY_DOWN_LEFT = 0x2b,
    HDDVD_KEY_DOWN_RIGHT = 0x2c
} hddvd_runtime_key_code;
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
/* Drawing beyond a scene node's image, from its computed style
   (hddvd_scene_get_decoration). Strings borrow from the markup. */
typedef enum hddvd_scene_decoration_property
{
    /* backgroundColor fills the node. */
    HDDVD_SCENE_FILL = 1u << 0,
    /* The node has text: a <p> element's text, or an <input>'s state:value. */
    HDDVD_SCENE_TEXT = 1u << 1,
    /* color was declared for the text; otherwise the host chooses. */
    HDDVD_SCENE_TEXT_COLOR = 1u << 2,
    /* backgroundPosition was declared: the image is drawn at its own size
       (or contentWidth/contentHeight percent of the node) and placed, not
       stretched to the node. */
    HDDVD_SCENE_IMAGE_PLACED = 1u << 3
} hddvd_scene_decoration_property;
typedef struct hddvd_scene_decoration
{
    uint32_t properties;
    uint32_t fill_color;
    const char *text;
    const char *font;
    /* Pixels; 0 when undeclared or a keyword size. */
    uint32_t font_size;
    uint32_t text_color;
    hddvd_xmu_font_style font_style;
    hddvd_xmu_align text_align;
    hddvd_xmu_align display_align;
    uint8_t wrap;
    hddvd_xmu_background_position image_x;
    hddvd_xmu_background_position image_y;
    int32_t image_x_offset;
    int32_t image_y_offset;
    /* Whole percentages of the node, 0 for the image's own size. */
    uint32_t image_width_percent;
    uint32_t image_height_percent;
} hddvd_scene_decoration;
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
/* Display-list items from hddvd_display_build_scene, in paint order. For
   each node: a FILL item, then its IMAGE, then a TEXT item. quad carries the
   node, destination, clip, z and opacity for every kind; image and source
   only for IMAGE. color is the fill color, or the text color when
   has_color; text fields borrow from the scene's markup. */
typedef enum hddvd_display_item_kind
{
    HDDVD_DISPLAY_ITEM_IMAGE = 1,
    HDDVD_DISPLAY_ITEM_FILL,
    HDDVD_DISPLAY_ITEM_TEXT
} hddvd_display_item_kind;
typedef struct hddvd_display_item
{
    hddvd_display_item_kind kind;
    hddvd_display_quad quad;
    uint32_t color;
    uint8_t has_color;
    const char *text;
    const char *font;
    uint32_t font_size;
    hddvd_xmu_font_style font_style;
    hddvd_xmu_align text_align;
    hddvd_xmu_align display_align;
    uint8_t wrap;
} hddvd_display_item;
typedef int (*hddvd_aca_entry_cb)(void *, const hddvd_aca_entry *);

typedef enum hddvd_object_type
{
    HDDVD_OBJECT_UNKNOWN = 0,
    HDDVD_OBJECT_MANIFEST = 1,
    HDDVD_OBJECT_MARKUP = 2,
    HDDVD_OBJECT_SCRIPT = 3,
    HDDVD_OBJECT_IMAGE = 4,
    /* Clear text whose kind (manifest, markup, playlist, script source) the
       envelope does not distinguish. */
    HDDVD_OBJECT_TEXT = 5
} hddvd_object_type;
/* HDDVDOBJ is libhddvd's documented synthetic envelope. AACS is the envelope
   observed on disc (docs/formats/aacs-object-envelope.md). */
typedef enum hddvd_object_format
{
    HDDVD_OBJECT_FORMAT_NONE = 0,
    HDDVD_OBJECT_FORMAT_HDDVDOBJ,
    HDDVD_OBJECT_FORMAT_AACS
} hddvd_object_format;
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
    HDDVD_EVENT_INPUT,
    HDDVD_EVENT_RATE_CHANGE,
    /* A title started playing: data.position.title is its playlist index. */
    HDDVD_EVENT_TITLE_BEGIN,
    /* Playback entered a chapter: data.position.chapter, 1-based. */
    HDDVD_EVENT_CHAPTER,
    /* The host finished a script command (hddvd_runtime_bridge):
       data.acknowledgement with the runtime command's id. */
    HDDVD_EVENT_SCRIPT_ACK
} hddvd_event_type;
/* HDDVD_EVENT_STREAM_SELECTION's data.stream.stream_type for the tracks
   scripts see; stream_id is then the 1-based track number, 0 for none. */
typedef enum hddvd_stream_type
{
    HDDVD_STREAM_TYPE_OTHER = 0,
    HDDVD_STREAM_TYPE_AUDIO,
    HDDVD_STREAM_TYPE_SUBTITLE
} hddvd_stream_type;
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
        /* Playback rate as numerator/denominator; 1/1 is normal speed. */
        struct
        {
            int32_t numerator;
            uint32_t denominator;
        } rate;
        struct
        {
            uint32_t title;
            uint32_t chapter;
        } position;
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
/* Player state as the host reports it to scripts (hddvd_runtime_set_player).
   Track numbers are 1-based, 0 for none; menu_language is a language code or
   "". */
typedef struct hddvd_runtime_player
{
    /* Playlist title index, or SIZE_MAX when none is playing. */
    size_t title;
    hddvd_time elapsed;
    /* 1-based, 0 when unknown. */
    uint32_t chapter;
    hddvd_playback_state state;
    uint32_t audio_track;
    uint32_t subtitle_track;
    uint8_t subtitle_visible;
    char menu_language[8];
} hddvd_runtime_player;
/* What a script asked the player to do. Fields not named are zero. */
typedef enum hddvd_runtime_command_type
{
    HDDVD_RUNTIME_COMMAND_PLAY = 0,
    HDDVD_RUNTIME_COMMAND_PAUSE,
    /* title and time: a title's or a chapter's jump(). */
    HDDVD_RUNTIME_COMMAND_JUMP,
    /* number: a track. */
    HDDVD_RUNTIME_COMMAND_SELECT_AUDIO,
    HDDVD_RUNTIME_COMMAND_SELECT_SUBTITLE,
    /* text: a language code. */
    HDDVD_RUNTIME_COMMAND_SELECT_AUDIO_LANGUAGE,
    HDDVD_RUNTIME_COMMAND_SELECT_SUBTITLE_LANGUAGE,
    /* number: 1 to show subtitles, 0 to hide them. */
    HDDVD_RUNTIME_COMMAND_SUBTITLE_VISIBLE,
    /* number: an index in the host's application list; text: its id. */
    HDDVD_RUNTIME_COMMAND_ACTIVATE_APPLICATION,
    HDDVD_RUNTIME_COMMAND_INACTIVATE_APPLICATION,
    HDDVD_RUNTIME_COMMAND_APPLICATION_TO_TOP,
    HDDVD_RUNTIME_COMMAND_APPLICATION_TO_BOTTOM
} hddvd_runtime_command_type;
/* text is borrowed until the command is acknowledged or the runtime is
   destroyed, and is "" when the command has none. */
typedef struct hddvd_runtime_command
{
    hddvd_runtime_command_type type;
    uint64_t id;
    size_t title;
    hddvd_time time;
    uint32_t number;
    const char *text;
} hddvd_runtime_command;
typedef struct hddvd_nav_limits
{
    size_t max_events;
    size_t max_timers;
} hddvd_nav_limits;
/* Unified context configuration. The subsystem-specific limit types above are
   retained for ABI compatibility and for callers that construct objects
   independently of a disc context. */
typedef struct hddvd_config_limits
{
    size_t max_archive_members;
    uint64_t max_archive_member_bytes;
    uint64_t max_resource_bytes;
    size_t max_xml_nodes;
    size_t max_xml_depth;
    size_t max_xml_attributes;
    size_t max_references;
    size_t max_playlist_items;
    size_t max_images;
    uint64_t max_encoded_image_bytes;
    uint64_t max_decoded_pixels;
    uint32_t max_image_dimension;
    size_t max_quads;
    size_t max_script_strings;
    size_t max_script_functions;
    size_t max_script_instructions;
    uint64_t max_script_bytes;
    uint64_t max_script_heap_bytes;
    size_t max_script_recursion;
    size_t max_timers;
    size_t max_queued_events;
} hddvd_config_limits;
typedef void (*hddvd_event_fn)(void *, const hddvd_event *);
typedef void (*hddvd_command_fn)(void *, const hddvd_command *);
typedef void (*hddvd_runtime_command_fn)(void *, const hddvd_runtime_command *);

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

/* Header facts confirmed against externally supplied discs
   (docs/formats/ifo-map.md). Files are whole 2048-byte sectors, and
   last_sector equals the sector count minus one in every observed file. */
typedef enum hddvd_ifo_kind
{
    HDDVD_IFO_UNKNOWN = 0,
    HDDVD_IFO_VMG,
    HDDVD_IFO_STANDARD_VTS,
    /* The VTI file (HVA00001.VTI) that Advanced Content time maps name. */
    HDDVD_IFO_ADVANCED_VTS
} hddvd_ifo_kind;
typedef struct hddvd_ifo_header
{
    hddvd_ifo_kind kind;
    char identifier[13];
    uint32_t last_sector;
    uint64_t size;
} hddvd_ifo_header;
typedef struct hddvd_map_header
{
    uint64_t size;
    uint32_t last_sector;
    /* The VTI file this time map names, such as "HVA00001.VTI". */
    char vti_name[64];
} hddvd_map_header;
/* One EVOB record from a VTI file. start and end are the EVOB's first and
   last presentation times as stored (90 kHz); sector_count is the EVO file's
   length in 2048-byte sectors. A time map names its EVOB by number. */
typedef struct hddvd_vti_evob
{
    uint16_t number;
    uint16_t attribute_number;
    char file_name[256];
    hddvd_time start;
    hddvd_time end;
    uint32_t sector_count;
} hddvd_vti_evob;
/* One time-map entry, describing one EVOBU (a navigation pack and the
   packs up to the next one). Sectors are relative to the start of the EVO
   file; start is relative to the EVOB's start time. duration_units is the
   stored playback time, in units of 3003/8 ticks of the 90 kHz clock. */
typedef struct hddvd_tmap_entry
{
    uint32_t first_sector;
    uint32_t sector_count;
    uint32_t first_reference_sectors;
    uint32_t duration_units;
    hddvd_time start;
    hddvd_time duration;
} hddvd_tmap_entry;
/* Standard Content IFO tables (docs/formats/ifo-map.md). Sectors in a
   title set's tables are relative to the start of its title EVO. */
/* Stream attributes recorded in a title set IFO or a VTI
   (docs/formats/ifo-map.md). Only the fields below are decoded; raw is the
   record as stored, for everything else. */
typedef enum hddvd_stream_kind
{
    HDDVD_STREAM_AUDIO = 0,
    HDDVD_STREAM_SUBPICTURE = 1
} hddvd_stream_kind;
typedef struct hddvd_video_attributes
{
    /* Bits 7 and 6 of raw[0]: 0 for MPEG-2 and 1 for VC-1 were seen; other
       values are not decoded. */
    unsigned coding;
    uint8_t raw[4];
} hddvd_video_attributes;
typedef struct hddvd_stream_attributes
{
    /* The two-letter language code, or "" when the record has none. */
    char language[3];
    /* Audio records are 8 bytes; subpicture records 6 (the rest zero). */
    uint8_t raw[8];
} hddvd_stream_attributes;
typedef struct hddvd_vts_info
{
    /* Last sector of the whole title set (IFO, EVO and backup IFO). */
    uint32_t last_sector;
    uint32_t ifo_sectors;
    /* Where the menu and title EVOs start within the title set; 0 when a
       title set has no menu EVO. */
    uint32_t menu_evo_sector;
    uint32_t title_evo_sector;
    /* Length of the title EVO, from the end of the cell address table. */
    uint32_t title_evo_sectors;
} hddvd_vts_info;
/* A playback time as stored: BCD hours, minutes, seconds and frames. rate is
   the frame byte's top two bits; 2 is the only code observed (30 frames per
   second at 30000/1001). flag is the seconds byte's top bit, set in every
   observed time; its meaning is unknown. */
typedef struct hddvd_vts_time
{
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t frames;
    uint8_t rate;
    uint8_t flag;
} hddvd_vts_time;
/* A chapter (part of title): the program of a PGC where it starts. Numbers
   are 1-based, as stored. */
typedef struct hddvd_vts_chapter
{
    uint16_t pgc;
    uint16_t program;
} hddvd_vts_chapter;
typedef struct hddvd_vts_pgc
{
    /* From the PGC's search pointer: the entry flag (top bit) and the title
       number (second byte). */
    uint8_t entry;
    uint8_t title;
    uint16_t program_count;
    uint16_t cell_count;
    hddvd_vts_time playback_time;
    uint16_t pre_command_count;
    uint16_t post_command_count;
    uint16_t cell_command_count;
} hddvd_vts_pgc;
typedef struct hddvd_vts_cell
{
    uint32_t category;
    hddvd_vts_time playback_time;
    uint32_t first_vobu_sector;
    uint32_t first_ilvu_end_sector;
    uint32_t last_vobu_sector;
    uint32_t last_sector;
    uint16_t evob_id;
    uint8_t cell_id;
    /* 1-based cell command run after the cell, 0 for none; command_flags
       is the byte stored before it (0x10 to 0x70 observed), meaning unknown. */
    uint8_t command;
    uint8_t command_flags;
} hddvd_vts_cell;
typedef enum hddvd_vts_command_kind
{
    HDDVD_VTS_PRE_COMMAND = 0,
    HDDVD_VTS_POST_COMMAND,
    HDDVD_VTS_CELL_COMMAND
} hddvd_vts_command_kind;
/* One time-map entry: the VOBU playing at (index + 1) time units from the
   PGC's start. discontinuity is set when the next entry is in another cell,
   or there is none. */
typedef struct hddvd_vts_map_entry
{
    uint32_t sector;
    int discontinuity;
} hddvd_vts_map_entry;
/* A cell's extent from the cell address table. */
typedef struct hddvd_vts_evob_cell
{
    uint16_t evob_id;
    uint8_t cell_id;
    uint32_t first_sector;
    uint32_t last_sector;
} hddvd_vts_evob_cell;
/* A title from the disc's title table (HV000I01.IFO). */
typedef struct hddvd_vmg_title
{
    /* Stored byte, not decoded. */
    uint8_t playback_type;
    uint8_t angle_count;
    uint16_t chapter_count;
    uint16_t title_set;
    uint16_t title_set_title;
    /* First sector of the title set on the disc. */
    uint32_t title_set_sector;
} hddvd_vmg_title;
/* Where a title time plays from. evo_uri is a borrowed file:///dvddisc/ URI,
   valid until hddvd_title_map_close(). pts is the EVO presentation time of
   the title time (the EVOB start time plus evob_offset). Reading from sector
   reaches it: that EVOBU starts at entry_pts, at or before pts. */
typedef struct hddvd_title_position
{
    size_t clip;
    const char *evo_uri;
    uint16_t evob_number;
    hddvd_time evob_offset;
    hddvd_time pts;
    size_t entry;
    uint32_t sector;
    hddvd_time entry_pts;
} hddvd_title_position;

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
    HDDVD_FEATURE_IMAGE_DECODER = UINT64_C(1) << 7,
    HDDVD_FEATURE_TIME_MAP = UINT64_C(1) << 8
} hddvd_feature;

HDDVD_API const char *hddvd_version(void);
HDDVD_API uint32_t hddvd_abi_version(void);
HDDVD_API hddvd_features hddvd_get_features(void);
HDDVD_API const char *hddvd_feature_name(hddvd_feature);
HDDVD_API const char *hddvd_strerror(int);
HDDVD_API hddvd_ctx *hddvd_open(const char *);
HDDVD_API void hddvd_config_limits_default(hddvd_config_limits *);
HDDVD_API hddvd_ctx *hddvd_open_with_limits(const char *, const hddvd_config_limits *);
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
/* Object input is copied/retained by the returned immutable handle. Both
   HDDVDOBJ and AACS envelopes are accepted. AACS objects other than the
   observed clear types (0x02 image, 0x12 text) are treated as protected:
   they have no name and need a provider, or fail with HDDVD_EPROTECTED.
   For AACS, info.version is 0 and info.unknown_1 holds header bytes 5-6. */
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
HDDVD_API hddvd_object_format hddvd_object_get_format(const hddvd_object *);
/* Returns nonzero when input starts with a recognized envelope magic. */
HDDVD_API int hddvd_object_is_envelope(const void *, size_t);
/* The decoder exposes validated, engine-neutral script structure. Strings
   are borrowed and all indexes remain valid until hddvd_script_close(). */
HDDVD_API void hddvd_script_limits_default(hddvd_script_limits *);
HDDVD_API int hddvd_script_open(const void *, size_t, hddvd_script **);
HDDVD_API int hddvd_script_open_with_limits(const void *, size_t, const hddvd_script_limits *,
                                            hddvd_script **);
HDDVD_API int hddvd_script_open_with_config(const void *, size_t, const hddvd_config_limits *,
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
/* Script runtime: runs HDi ECMAScript source in a sandboxed engine chosen at
   build time (docs/decisions/0001-script-engine.md). Builds without an engine
   report no HDDVD_FEATURE_SCRIPT_RUNTIME, hddvd_runtime_engine() returns NULL
   and hddvd_runtime_create* fail with HDDVD_ENOTSUP.

   Scripts see the ECMAScript built-ins and the HDi host objects described
   below, with no file, network or process access; Math.random is seeded
   identically in every runtime. A runtime is single-threaded and makes no
   callbacks: after each call the host reads the focus, generation and
   issues. Separate runtimes may be used on separate threads. */
HDDVD_API const char *hddvd_runtime_engine(void);
/* Heap and recursion come from the config's max_script_heap_bytes and
   max_script_recursion, operations from max_script_instructions, source size
   from max_script_bytes, and time defaults to one second. Creating a runtime
   installs the host objects, which counts against the heap budget only. */
HDDVD_API void hddvd_runtime_limits_default(hddvd_runtime_limits *);
HDDVD_API int hddvd_runtime_create(hddvd_runtime **);
HDDVD_API int hddvd_runtime_create_with_limits(const hddvd_runtime_limits *, hddvd_runtime **);
HDDVD_API int hddvd_runtime_create_with_config(const hddvd_config_limits *, hddvd_runtime **);
HDDVD_API void hddvd_runtime_destroy(hddvd_runtime *);
/* Runs a script in the global scope. Source is UTF-16 with a byte-order mark
   (as on disc) or UTF-8, with or without one; name labels it in traces and
   may be NULL. Returns HDDVD_EPARSE for invalid text or a syntax error,
   HDDVD_ESCRIPT for an uncaught exception, and HDDVD_ELIMIT when a budget
   runs out. An allocation refused by the heap budget is reported even when
   the script catches it. */
HDDVD_API int hddvd_runtime_evaluate(hddvd_runtime *, const char *name, const void *source,
                                     size_t size);
/* Calls a global function with no arguments. HDDVD_ENOTFOUND when the name
   is not a function; otherwise as hddvd_runtime_evaluate. */
HDDVD_API int hddvd_runtime_call(hddvd_runtime *, const char *function);
HDDVD_API int hddvd_runtime_get_diagnostic(const hddvd_runtime *, hddvd_runtime_diagnostic *);
/* HDi host objects (docs/hdi-host-api.md). Every runtime has these globals:
   - document: getElementById(), body, createEvent(), and each markup id as
     a property (document.okBtn) unless the name is already a member;
   - elements: id, core.id, localName, parentNode, firstChild, nextSibling,
     hasChildNodes(), getAttribute(), attributes.getNamedItem(), state and
     style;
   - element.state: focused, enabled, actioned, pointer and value, plus
     getProperty(), setProperty() and unsetProperty(). Values are strings
     ("true"/"false" for flags); a value set by script is converted with
     String(). Setting focused to "true" moves the focus there, and "false"
     on the focused element clears it. unsetProperty() only validates the
     name: script values stay until changed, because markup timing, which
     would take the property back, is not modeled;
   - element.style: a property per name the style model knows (x, y, width,
     height, display, visibility, opacity, backgroundImage, backgroundFrame,
     navUp, ...) plus getProperty(), setProperty(), unsetProperty() and
     animateProperty(name, "v1;v2;...", seconds). Reads return the script's
     value, else the markup's text, else "auto" (display), "visible"
     (visibility), "1" (opacity) or "". unsetProperty(), or setting "",
     returns to markup.
     animateProperty() steps through the values over the duration on
     hddvd_runtime_advance: numbers with the same unit (px, % or none)
     interpolate linearly, other values switch at each keyframe;
   - application: addEventListener(), removeEventListener(),
     dispatchEvent(), createEvent() and createTimer(); the global functions
     of the same names, and setMarkupLoadedHandler(), act on it too;
   - events: type, bubbles, cancelable, target, currentTarget,
     defaultPrevented, initEvent(), stopPropagation() and preventDefault().
     createEvent(type) gives the event that type (discs dispatch such events
     without initEvent). Listeners (functions, or objects with handleEvent)
     run in the order they were added, whatever their capture flag; adding
     one twice has no effect. A listener added during a dispatch waits for
     the next one, and one removed during a dispatch does not run. A
     listener that throws is recorded as an issue, with the error and where
     it was thrown when the engine records that, and the next one runs;
   - createTimer("HH:MM:SS:FF", clock, callback): frames count at 60000/1001
     per second, as in playlists (unconfirmed for timers). A timer starts
     disabled and repeats (autoReset) until autoReset is set to false;
     setting enabled to true schedules it one interval after the current
     time, and false cancels it. Every timer runs on the time given to
     hddvd_runtime_advance, whatever its clock argument.
   Player, Diagnostics and the application lifecycle are described below.
   Markup timing (cues, events and animations declared in markup) does not
   run.

   load_markup makes `xmu` (borrowed; it must outlive the runtime or the
   next load) the document scripts see, with the focus on its initial focus.
   Script state and style values of the previous document are discarded and
   element objects from it throw when used. NULL detaches the document.
   HDDVD_EPARSE when its styles do not parse. */
HDDVD_API int hddvd_runtime_load_markup(hddvd_runtime *, const hddvd_xmu *);
/* Calls the handler given to setMarkupLoadedHandler(), if any. */
HDDVD_API int hddvd_runtime_markup_loaded(hddvd_runtime *);
/* The document's computed styles with script values applied, for
   hddvd_scene_build with hddvd_runtime_focus; NULL with no document. Valid
   until the next call that runs scripts, load or destroy. */
HDDVD_API const hddvd_xmu_styles *hddvd_runtime_styles(const hddvd_runtime *);
/* The focused markup node, or SIZE_MAX. */
HDDVD_API size_t hddvd_runtime_focus(const hddvd_runtime *);
/* A state or style value as scripts read it, or NULL for an unknown state
   name or node. Borrowed until the next call that runs scripts. */
HDDVD_API const char *hddvd_runtime_get_state(const hddvd_runtime *, size_t, const char *);
HDDVD_API const char *hddvd_runtime_get_style(const hddvd_runtime *, size_t, const char *);
/* Increases whenever the focus or a state or style value changes, so the
   host knows to rebuild its scene. */
HDDVD_API uint64_t hddvd_runtime_generation(const hddvd_runtime *);
/* Dispatches controller_key_down (down nonzero) or controller_key_up, with
   the code in event.key and the focused element (or the application) as
   event.target, to application listeners. Unless a listener calls
   preventDefault(), a compass key moves the focus to the focused element's
   nav property in that direction, and Enter sets its state actioned to
   "true" (key down) and back to "false" (key up); these defaults are
   unconfirmed against a player. *prevented (when not NULL) is set when a
   listener called preventDefault(), so the host should not act on the key
   either. Returns HDDVD_ESCRIPT when a listener threw (see the issues), and
   HDDVD_ELIMIT when the operation, time or heap budget ran out; the first two
   stop the dispatch where it is, default action included. Running out of
   recursion is an exception in the listener concerned. Budgets apply to each
   call as a whole. */
HDDVD_API int hddvd_runtime_key(hddvd_runtime *, uint32_t, int, int *);
/* Moves the runtime clock forward (HDDVD_EINVAL backwards) and runs the
   timers due by then in due order (equal times in the order they were
   scheduled: enabled, or rescheduled after firing), then steps animations.
   A timer fires at most once per call: a repeating timer that fell behind
   is rescheduled one interval after the new time. Returns as
   hddvd_runtime_key. */
HDDVD_API int hddvd_runtime_advance(hddvd_runtime *, hddvd_time);
/* Dispatches an event of this type, with no other fields, to application
   listeners. Returns as hddvd_runtime_key. */
HDDVD_API int hddvd_runtime_dispatch(hddvd_runtime *, const char *);
/* Issues are kept in order until cleared; once 256 are held, later ones are
   only counted. */
HDDVD_API size_t hddvd_runtime_issue_count(const hddvd_runtime *);
HDDVD_API int hddvd_runtime_get_issue(const hddvd_runtime *, size_t, hddvd_runtime_issue *);
HDDVD_API size_t hddvd_runtime_issues_dropped(const hddvd_runtime *);
HDDVD_API void hddvd_runtime_clear_issues(hddvd_runtime *);
/* Player and application host objects (docs/hdi-host-api.md):
   - Player.playlist: titles (by index, and by playlist id as a property),
     each with attributes.id, attributes.titleDuration, chapters (each with
     jump(time)) and jump(time); currentTitle (whose elapsedTime is the
     reported title time), currentChapter.number, playState with
     PLAYSTATE_STOP, PLAYSTATE_PLAY and PLAYSTATE_PAUSE (placeholder
     values), play() and pause();
   - Player.track: selectAudioTrackNumber(), selectSubtitleTrackNumber(),
     selectAudioLanguage(), selectSubtitleLanguage(), and the current and
     selected track numbers and language codes (from the playlist's tracks);
   - Player.subtitle.visible (setting it is a command), Player.menuLanguage,
     Player.capabilities.network.connected (false: network access is denied),
     Player.generalParameters.getValue()/setValue() (kept in the runtime;
     getValue of a missing name is null, and setting undefined or null
     removes it);
   - application: attributes.id, advancedApplications (id, zOrder,
     activate(), inactivate()), thisAdvancedApplication, moveToTop() and
     moveToBottom(), over the list from hddvd_runtime_set_applications;
   - Diagnostics.trace(message) records a TRACE issue.
   Times are "HH:MM:SS:FF" strings with frames at 60000/1001 per second, as
   in playlists; jump()'s second argument is accepted and ignored. Calls that
   change playback become commands for the host; the player state scripts
   read changes only when the host reports it. Members a player has but this
   runtime cannot honor (audio mixing and effects, video layout and scaling,
   getSPRM, document.setXPathVariable() and getProperties(), which drive
   markup timing, and every member of FileIO, XMLParser, Network and
   PersistentStorageManager, which would reach files or the network) throw
   an Error naming the member; their constants exist so scripts can load.

   load_playlist makes `xpl` (borrowed; it must outlive the runtime or the
   next load) the playlist scripts see; NULL removes it. Title objects from
   an earlier playlist throw when used. */
HDDVD_API int hddvd_runtime_load_playlist(hddvd_runtime *, const hddvd_xpl *);
/* Reports the player's state. HDDVD_EINVAL for a title the playlist lacks,
   an unknown state or an unterminated menu language. */
HDDVD_API int hddvd_runtime_set_player(hddvd_runtime *, const hddvd_runtime_player *);
/* The running applications in z-order (bottom first), each by its id (NULL
   or "" when it has none), and which of them this runtime runs; ids are
   copied. Fewer than 64 applications. */
HDDVD_API int hddvd_runtime_set_applications(hddvd_runtime *, const char *const *, size_t, size_t);
/* Commands are queued in the order scripts made them; at most 64 wait to be
   taken and 64 more to be acknowledged, and a script call beyond that
   throws. take returns HDDVD_ENOTFOUND when none waits, and HDDVD_ELIMIT
   while 64 taken commands are unacknowledged. Every command taken
   must be acknowledged with its id (HDDVD_EINVAL for an id not taken);
   HDDVD_COMMAND_REJECTED or HDDVD_COMMAND_FAILED records a COMMAND issue. */
HDDVD_API size_t hddvd_runtime_command_count(const hddvd_runtime *);
HDDVD_API int hddvd_runtime_take_command(hddvd_runtime *, hddvd_runtime_command *);
HDDVD_API int hddvd_runtime_acknowledge(hddvd_runtime *, uint64_t, hddvd_command_result);
/* Dispatches an event of this type with string fields (event.newValue,
   event.lang, ...); names must be identifiers other than the event's own
   members. At most 16 fields. Returns as hddvd_runtime_key. */
HDDVD_API int hddvd_runtime_dispatch_fields(hddvd_runtime *, const char *, const char *const *,
                                            const char *const *, size_t);
/* Decoders must return premultiplied RGBA8. The cache calls release exactly
   once after the last display list using a plane is closed. All objects are
   single-threaded and callbacks are made only by cache_get/build calls. */
HDDVD_API void hddvd_display_limits_default(hddvd_display_limits *);
HDDVD_API int hddvd_image_cache_create(hddvd_image_decode_fn, void *, const hddvd_display_limits *,
                                       hddvd_image_cache **);
HDDVD_API int hddvd_image_cache_create_with_config(hddvd_image_decode_fn, void *,
                                                   const hddvd_config_limits *,
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
/* Supplies the encoded bytes of an image that markup names, given the URL as
   written in backgroundImage. The bytes must stay valid while any display
   list built from the scene exists. Return HDDVD_ENOTFOUND to draw nothing
   for that image; any other error fails the build. */
typedef int (*hddvd_scene_image_fn)(void *, const char *, const void **, size_t *);
/* Build display-list scene nodes from markup and its computed styles, for
   hddvd_display_build. Elements under <body> (or every element of a <div>
   fragment) become nodes in document order, except param and include. A
   node's id is its markup node index, so hit tests name markup nodes.
   - Geometry: x, y, width and height in pixels, or a percentage of the
     parent's size. An element without a width or height fills its parent
     (unconfirmed against a player). The parent of a top-level element is
     the 1920x1080 logical region.
   - visible is cleared by display "none" or visibility "hidden".
   - opacity follows CSS: "inherit" copies the parent's value, and values
     multiply down the tree.
   - z_index is the computed zIndex; focused is set on the node given (pass
     SIZE_MAX for none); buttons and inputs are hit-testable.
   - Images are the normal and focused background images
     (hddvd_xmu_style_image_for_state); each URL is requested once.
   Background position, crop, colors and text are not drawn. The markup and
   styles must outlive the scene, whose image identities borrow from them. */
HDDVD_API int hddvd_scene_build(const hddvd_xmu *, const hddvd_xmu_styles *, size_t,
                                hddvd_scene_image_fn, void *, hddvd_scene **);
HDDVD_API void hddvd_scene_close(hddvd_scene *);
HDDVD_API size_t hddvd_scene_node_count(const hddvd_scene *);
/* The node array, valid until hddvd_scene_close(); NULL when empty. */
HDDVD_API const hddvd_scene_node *hddvd_scene_nodes(const hddvd_scene *);
HDDVD_API int hddvd_scene_get_decoration(const hddvd_scene *, size_t, hddvd_scene_decoration *);
/* Build a display list from a scene, including its decorations: background
   fills, text runs, and placed images. The scene (and its markup) must
   outlive the list. hddvd_display_quad_count and hddvd_display_get_quad
   still report image quads only; hddvd_display_item_count and
   hddvd_display_get_item report every item. Damage and hit tests cover
   every item. */
HDDVD_API int hddvd_display_build_scene(hddvd_image_cache *, uint32_t, uint32_t,
                                        const hddvd_scene *, const hddvd_display_list *,
                                        hddvd_display_list **);
HDDVD_API size_t hddvd_display_item_count(const hddvd_display_list *);
HDDVD_API int hddvd_display_get_item(const hddvd_display_list *, size_t, hddvd_display_item *);
HDDVD_API int hddvd_scan_files(const hddvd_ctx *, hddvd_file_cb, void *);
HDDVD_API const char *hddvd_file_kind_name(hddvd_file_kind);
HDDVD_API int hddvd_xpl_find_start_ref(const char *, char **);
/* XPL objects are immutable. Strings in returned records are borrowed until
   hddvd_xpl_close(). Times are normalized to frames using TitleSet timeBase. */
HDDVD_API void hddvd_xpl_limits_default(hddvd_xpl_limits *);
HDDVD_API int hddvd_xpl_open(const void *, size_t, hddvd_xpl **);
HDDVD_API int hddvd_xpl_open_with_limits(const void *, size_t, const hddvd_xpl_limits *,
                                         hddvd_xpl **);
HDDVD_API int hddvd_xpl_open_with_config(const void *, size_t, const hddvd_config_limits *,
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
/* Resolve a title id, or the already-validated onEnd branch of a title, to an
   index in the immutable title array. */
HDDVD_API int hddvd_xpl_find_title(const hddvd_xpl *, const char *, size_t *);
HDDVD_API int hddvd_xpl_get_on_end_target(const hddvd_xpl *, size_t, size_t *);
HDDVD_API size_t hddvd_xpl_clip_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_clip(const hddvd_xpl *, size_t, size_t, hddvd_xpl_clip *);
HDDVD_API int hddvd_xpl_get_clip_kind(const hddvd_xpl *, size_t, size_t, hddvd_xpl_clip_kind *);
HDDVD_API size_t hddvd_xpl_chapter_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_chapter(const hddvd_xpl *, size_t, size_t, hddvd_xpl_chapter *);
HDDVD_API size_t hddvd_xpl_track_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_track(const hddvd_xpl *, size_t, size_t, hddvd_xpl_track *);
/* Use SIZE_MAX as the title index for playlist-wide applications. */
HDDVD_API size_t hddvd_xpl_application_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_application(const hddvd_xpl *, size_t, size_t, hddvd_xpl_application *);
/* Returns HDDVD_ENOTFOUND when the application has no loadingBegin time. */
HDDVD_API int hddvd_xpl_get_application_loading_begin(const hddvd_xpl *, size_t, size_t,
                                                      hddvd_xpl_time *);
HDDVD_API size_t hddvd_xpl_application_resource_count(const hddvd_xpl *, size_t, size_t);
HDDVD_API int hddvd_xpl_get_application_resource(const hddvd_xpl *, size_t, size_t, size_t,
                                                 hddvd_xpl_application_resource *);
/* The resource's multiplexed attribute as a number: true/false read as 1/0,
   and discs also use other non-negative integers (observed 9 to 13) whose
   meaning is not established. The record's multiplexed flag is nonzero for
   any nonzero value. */
HDDVD_API int hddvd_xpl_get_application_resource_multiplexed(const hddvd_xpl *, size_t, size_t,
                                                             size_t, uint64_t *);
HDDVD_API size_t hddvd_xpl_event_count(const hddvd_xpl *, size_t);
HDDVD_API int hddvd_xpl_get_event(const hddvd_xpl *, size_t, size_t, hddvd_xpl_event *);
/* Playlist majorVersion/minorVersion; HDDVD_ENOTFOUND when majorVersion is
   absent. A missing minorVersion reads as 0. */
HDDVD_API int hddvd_xpl_get_version(const hddvd_xpl *, uint32_t *, uint32_t *);
/* TitleSet tickBase in ticks per second, or 0 when absent. */
HDDVD_API uint32_t hddvd_xpl_tick_rate(const hddvd_xpl *);
/* TitleSet defaultLanguage, or NULL when absent. */
HDDVD_API const char *hddvd_xpl_default_language(const hddvd_xpl *);
/* The time must use the playlist frame rate (HDDVD_EINVAL otherwise). */
HDDVD_API int hddvd_xpl_get_application_state(const hddvd_xpl *, size_t, size_t, hddvd_xpl_time,
                                              hddvd_xpl_application_state *);
/* Playlist description, or NULL when absent. */
HDDVD_API const char *hddvd_xpl_description(const hddvd_xpl *);
HDDVD_API int hddvd_xpl_get_title_details(const hddvd_xpl *, size_t, hddvd_xpl_title_details *);
/* Clip description, or NULL through the out pointer when absent. */
HDDVD_API int hddvd_xpl_get_clip_description(const hddvd_xpl *, size_t, size_t, const char **);
HDDVD_API int hddvd_xpl_get_track_details(const hddvd_xpl *, size_t, size_t,
                                          hddvd_xpl_track_details *);
/* Use SIZE_MAX as the title index for playlist-wide applications. */
HDDVD_API int hddvd_xpl_get_application_details(const hddvd_xpl *, size_t, size_t,
                                                hddvd_xpl_application_details *);
HDDVD_API int hddvd_xpl_get_application_resource_details(const hddvd_xpl *, size_t, size_t, size_t,
                                                         hddvd_xpl_resource_details *);
/* Constructs that parsed but are not represented above. Attributes in the
   XML Schema instance namespace (xsi:schemaLocation and the like) describe
   the document, not the playlist, and are never reported. */
HDDVD_API size_t hddvd_xpl_unmodeled_count(const hddvd_xpl *);
HDDVD_API int hddvd_xpl_get_unmodeled(const hddvd_xpl *, size_t, hddvd_xpl_unmodeled *);
HDDVD_API int hddvd_xmf_dump(const void *, size_t, const char *, void *);
/* XMF objects are immutable. All returned strings are borrowed and remain
   valid until hddvd_xmf_close(). Inputs are copied by the parser. */
HDDVD_API void hddvd_xmf_limits_default(hddvd_xmf_limits *);
HDDVD_API int hddvd_xmf_open(const void *, size_t, hddvd_xmf **);
HDDVD_API int hddvd_xmf_open_with_limits(const void *, size_t, const hddvd_xmf_limits *,
                                         hddvd_xmf **);
HDDVD_API int hddvd_xmf_open_with_config(const void *, size_t, const hddvd_config_limits *,
                                         hddvd_xmf **);
HDDVD_API void hddvd_xmf_close(hddvd_xmf *);
/* NULL when the Application element has no id, as on most observed discs. */
HDDVD_API const char *hddvd_xmf_application_id(const hddvd_xmf *);
HDDVD_API int hddvd_xmf_get_region(const hddvd_xmf *, hddvd_xmf_region *);
HDDVD_API size_t hddvd_xmf_script_count(const hddvd_xmf *);
/* Script and markup references return (id, src); id is NULL when absent. */
HDDVD_API int hddvd_xmf_get_script(const hddvd_xmf *, size_t, const char **, const char **);
HDDVD_API size_t hddvd_xmf_markup_count(const hddvd_xmf *);
HDDVD_API int hddvd_xmf_get_markup(const hddvd_xmf *, size_t, const char **, const char **);
HDDVD_API size_t hddvd_xmf_resource_root_count(const hddvd_xmf *);
HDDVD_API const char *hddvd_xmf_resource_root(const hddvd_xmf *, size_t);
/* Application reports own the manifest and all report strings. Dependency
   failures are recorded rather than making open fail, so callers can present
   a complete preflight report before attempting execution. */
HDDVD_API int hddvd_application_open(const hddvd_ctx *, const char *, hddvd_application **);
HDDVD_API void hddvd_application_close(hddvd_application *);
HDDVD_API const hddvd_xmf *hddvd_application_manifest(const hddvd_application *);
HDDVD_API size_t hddvd_application_dependency_count(const hddvd_application *);
HDDVD_API int hddvd_application_get_dependency(const hddvd_application *, size_t,
                                               hddvd_dependency *);
HDDVD_API size_t hddvd_application_requirement_count(const hddvd_application *);
HDDVD_API int hddvd_application_get_requirement(const hddvd_application *, size_t,
                                                hddvd_requirement *);
HDDVD_API int hddvd_aca_supported(void);
HDDVD_API int hddvd_aca_open(const char *, hddvd_aca **);
HDDVD_API int hddvd_aca_open_stream(hddvd_stream *, hddvd_aca **);
HDDVD_API int hddvd_aca_open_stream_with_limits(hddvd_stream *, const hddvd_limits *, hddvd_aca **);
HDDVD_API int hddvd_aca_open_stream_with_config(hddvd_stream *, const hddvd_config_limits *,
                                                hddvd_aca **);
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
/* XMU models are immutable. All strings returned through getters are borrowed
   and remain valid until hddvd_xmu_close(). SIZE_MAX denotes no node. */
HDDVD_API void hddvd_xmu_limits_default(hddvd_xmu_limits *);
HDDVD_API int hddvd_xmu_open(const void *, size_t, hddvd_xmu **);
HDDVD_API int hddvd_xmu_open_with_limits(const void *, size_t, const hddvd_xmu_limits *,
                                         hddvd_xmu **);
HDDVD_API int hddvd_xmu_open_with_config(const void *, size_t, const hddvd_config_limits *,
                                         hddvd_xmu **);
HDDVD_API void hddvd_xmu_close(hddvd_xmu *);
HDDVD_API size_t hddvd_xmu_node_count(const hddvd_xmu *);
HDDVD_API int hddvd_xmu_get_node(const hddvd_xmu *, size_t, hddvd_xmu_node *);
HDDVD_API int hddvd_xmu_get_attribute(const hddvd_xmu *, size_t, size_t, hddvd_xmu_attribute *);
HDDVD_API size_t hddvd_xmu_find_id(const hddvd_xmu *, const char *);
/* The root is <root> for a document or <div> for a markup fragment. The
   initial focus is the one element outside <head> with state:focused="true",
   or SIZE_MAX when there is none; two such elements are rejected. */
HDDVD_API size_t hddvd_xmu_initial_focus(const hddvd_xmu *);
HDDVD_API const char *hddvd_xmu_clock(const hddvd_xmu *);
/* Style models borrow the XMU model, which must outlive them. The specified
   value contains only properties declared directly on a node; the computed
   value includes named style references. Equal zIndex values retain document
   order. Elements inside <timing> (animation keyframes) have no static style.
   A nav property of "none", or one naming an id the document lacks (a
   script may create it later), resolves to SIZE_MAX. In a <div>-rooted
   fragment, style references to ids defined elsewhere are skipped.
   Percentage contentWidth/contentHeight values are in the details record.
   position is "absolute" or "relative" (position_absolute 1 or 0).
   Malformed numbers in any property return HDDVD_EPARSE. A keyword a detail
   property does not recognize (including a word where a number is expected,
   such as fontSize="small"), or an unknown property, is reported through
   hddvd_xmu_styles_get_unmodeled instead and leaves the property unset. */
HDDVD_API int hddvd_xmu_styles_open(const hddvd_xmu *, hddvd_xmu_styles **);
HDDVD_API void hddvd_xmu_styles_close(hddvd_xmu_styles *);
HDDVD_API int hddvd_xmu_get_specified_style(const hddvd_xmu_styles *, size_t, hddvd_xmu_style *);
HDDVD_API int hddvd_xmu_get_computed_style(const hddvd_xmu_styles *, size_t, hddvd_xmu_style *);
HDDVD_API const char *hddvd_xmu_style_background_image(const hddvd_xmu_styles *, size_t, size_t,
                                                       int);
HDDVD_API const char *hddvd_xmu_style_image_for_state(const hddvd_xmu_styles *, size_t, int);
HDDVD_API size_t hddvd_xmu_style_z_order(const hddvd_xmu_styles *, size_t);
/* Pass a nonzero last argument for the computed value. */
HDDVD_API int hddvd_xmu_get_style_details(const hddvd_xmu_styles *, size_t, int,
                                          hddvd_xmu_style_details *);
/* Deduplicated in document order; elements inside <timing> are skipped. */
HDDVD_API size_t hddvd_xmu_styles_unmodeled_count(const hddvd_xmu_styles *);
HDDVD_API int hddvd_xmu_styles_get_unmodeled(const hddvd_xmu_styles *, size_t,
                                             hddvd_xmu_style_unmodeled *);
HDDVD_API int hddvd_time_from_milliseconds(uint64_t, hddvd_time *);
HDDVD_API int hddvd_time_to_milliseconds(hddvd_time, uint64_t *);
HDDVD_API int hddvd_time_from_seconds(uint64_t, hddvd_time *);
/* Navigation objects and callbacks have navigation-thread affinity. Events
   with equal timestamps are delivered in posting order. The bounded queue
   rejects its newest event with HDDVD_ELIMIT and never discards old work.
   Host command callbacks run synchronously and must acknowledge later by
   posting HDDVD_EVENT_COMMAND_ACK (including if the command is rejected).

   The scheduler clock is title time. advance() only moves it forward, so it
   stops while playback is paused and follows the title clock at any playback
   rate; timers fire once title time reaches their due time and are stamped
   with it. Events the host posts at time T before advancing to T precede
   timers that fire during that advance. discontinuity() moves the clock to
   any time (seek, stream discontinuity): queued events stamped later than the
   new time are restamped to it without reordering, an HDDVD_EVENT_DISCONTINUITY
   event is queued at the new time, and timers that are due by then fire after
   it, stamped with the new time. Timers due later stay pending, including
   after a backward seek. cancel_all_timers() discards every pending timer
   (for example when a menu application closes) and returns how many it
   removed; queued events are unaffected. */
HDDVD_API void hddvd_nav_limits_default(hddvd_nav_limits *);
HDDVD_API int hddvd_nav_create(const hddvd_nav_limits *, hddvd_event_fn, hddvd_command_fn, void *,
                               hddvd_nav **);
HDDVD_API int hddvd_nav_create_with_config(const hddvd_config_limits *, hddvd_event_fn,
                                           hddvd_command_fn, void *, hddvd_nav **);
HDDVD_API void hddvd_nav_destroy(hddvd_nav *);
HDDVD_API int hddvd_nav_post(hddvd_nav *, const hddvd_event *);
HDDVD_API int hddvd_nav_schedule_timer(hddvd_nav *, uint64_t, hddvd_time);
HDDVD_API int hddvd_nav_cancel_timer(hddvd_nav *, uint64_t);
HDDVD_API int hddvd_nav_advance(hddvd_nav *, hddvd_time);
HDDVD_API int hddvd_nav_discontinuity(hddvd_nav *, hddvd_time);
HDDVD_API size_t hddvd_nav_cancel_all_timers(hddvd_nav *);
HDDVD_API size_t hddvd_nav_pump(hddvd_nav *, size_t);
HDDVD_API int hddvd_nav_command(hddvd_nav *, const hddvd_command *, uint64_t *);
/* Runs a script runtime's work as navigation-queue tasks: the host's
   hddvd_event_fn passes every pumped event to hddvd_runtime_bridge_event,
   so scripts run in queue order with clocks, input, timers and
   acknowledgements. For each event the bridge:
   - moves the runtime clock forward by the scheduler time that passed since
     the previous event (none across an HDDVD_EVENT_DISCONTINUITY), running
     script timers and animations, and reports the event's time as the
     current title's elapsed time;
   - INPUT: hddvd_runtime_key;
   - PLAYBACK_STATE, TITLE_BEGIN, CHAPTER and STREAM_SELECTION (audio and
     subtitle types): reports the new player state, then dispatches
     play_state, title_begin, chapter, audio_track or subtitle_track;
     TITLE_END dispatches title_end;
   - SCRIPT_ACK: hddvd_runtime_acknowledge.
   Then it hands every command scripts made to the callback, in order. The
   host must answer each, accepted or not, by posting HDDVD_EVENT_SCRIPT_ACK
   with the command's id; with no callback the bridge rejects them itself.
   Commands beyond the runtime's 64 unacknowledged wait for acknowledgements.
   The runtime is borrowed and must outlive the bridge; the host may still
   set the playlist, applications and markup between events. Returns the
   first failure of the calls made (as hddvd_runtime_key); the rest of the
   event is still handled. HDDVD_EINVAL for a title the playlist lacks.
   Event names and the clock model are unconfirmed against a player. */
HDDVD_API int hddvd_runtime_bridge_create(hddvd_runtime *, hddvd_runtime_command_fn, void *,
                                          hddvd_runtime_bridge **);
HDDVD_API void hddvd_runtime_bridge_destroy(hddvd_runtime_bridge *);
HDDVD_API int hddvd_runtime_bridge_event(hddvd_runtime_bridge *, const hddvd_event *);
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
/* Validate and read IFO and MAP headers. Unrecognized identifiers return
   HDDVD_ENOTSUP; framing that contradicts the end-sector field returns
   HDDVD_EPARSE. Tables beyond these fields are not decoded yet. */
HDDVD_API int hddvd_ifo_read_header(hddvd_stream *, hddvd_ifo_header *);
HDDVD_API int hddvd_map_read_header(hddvd_stream *, hddvd_map_header *);
/* Read the EVOB table of an ADVANCED-VTS (VTI) file. Other identifiers
   return HDDVD_ENOTSUP; a table outside the file, a record outside the
   table, an empty, unprintable or path-like file name, a duplicate number,
   an end before the start, or an empty EVOB return HDDVD_EPARSE. The
   handle does not keep the stream. */
HDDVD_API int hddvd_vti_open(hddvd_stream *, hddvd_vti **);
HDDVD_API void hddvd_vti_close(hddvd_vti *);
HDDVD_API size_t hddvd_vti_evob_count(const hddvd_vti *);
HDDVD_API int hddvd_vti_get_evob(const hddvd_vti *, size_t, hddvd_vti_evob *);
/* Find the record with this EVOB number; HDDVD_ENOTFOUND if none. */
HDDVD_API int hddvd_vti_find_evob(const hddvd_vti *, uint16_t, size_t *);
/* The VTI's attribute records, which EVOBs name by attribute_number (record
   index + 1): the video attributes and the audio streams of each. A VTI
   without the table has none. Open rejects a record whose audio streams do
   not fit in it and an EVOB naming a missing record (HDDVD_EPARSE). */
HDDVD_API size_t hddvd_vti_attribute_count(const hddvd_vti *);
HDDVD_API int hddvd_vti_get_video_attributes(const hddvd_vti *, size_t, hddvd_video_attributes *);
HDDVD_API size_t hddvd_vti_audio_count(const hddvd_vti *, size_t);
HDDVD_API int hddvd_vti_get_audio_attributes(const hddvd_vti *, size_t, size_t,
                                             hddvd_stream_attributes *);
/* Read a time map (MAP file). Besides the header rules above, the entry
   table must lie inside the file and hold at least one entry, and every
   entry must have a non-zero size and playback time and a first reference
   picture that is non-empty and fits in its EVOBU; anything else returns
   HDDVD_EPARSE. The handle does not keep the stream. */
HDDVD_API int hddvd_tmap_open(hddvd_stream *, hddvd_tmap **);
HDDVD_API void hddvd_tmap_close(hddvd_tmap *);
HDDVD_API int hddvd_tmap_get_header(const hddvd_tmap *, hddvd_map_header *);
HDDVD_API uint16_t hddvd_tmap_evob_number(const hddvd_tmap *);
HDDVD_API size_t hddvd_tmap_entry_count(const hddvd_tmap *);
HDDVD_API int hddvd_tmap_get_entry(const hddvd_tmap *, size_t, hddvd_tmap_entry *);
/* Total sectors and playback time of all entries. */
HDDVD_API uint64_t hddvd_tmap_sector_count(const hddvd_tmap *);
HDDVD_API hddvd_time hddvd_tmap_duration(const hddvd_tmap *);
/* Index of the entry playing at this offset from the EVOB start time.
   Offsets at or past the end return HDDVD_ENOTFOUND. */
HDDVD_API int hddvd_tmap_find(const hddvd_tmap *, hddvd_time, size_t *);
/* Check a time map against its VTI record: same EVOB number, sectors that
   add up to the EVOB's sector count, and playback time that adds up to
   end - start. Returns HDDVD_EPARSE on any disagreement. */
HDDVD_API int hddvd_tmap_check_evob(const hddvd_tmap *, const hddvd_vti_evob *);
/* Read a Standard Content title set (STANDARD-VTS IFO) through its time
   maps, cells and VOBU map. Other identifiers return HDDVD_ENOTSUP; an IFO
   larger than the config's max_resource_bytes returns HDDVD_ELIMIT. Tables
   outside the file, records outside their tables, and references that do
   not resolve return HDDVD_EPARSE: a chapter naming a missing PGC or
   program, a program map that is not increasing from cell 1, a cell whose
   VOBUs are not in the VOBU map or not inside its address-table entry, a
   cell command beyond the PGC's count, a time-map entry outside the PGC's
   cells, or invalid BCD. Indexes are 0-based. The handle does not keep
   the stream. */
HDDVD_API int hddvd_vts_open(hddvd_stream *, hddvd_vts **);
HDDVD_API int hddvd_vts_open_with_config(hddvd_stream *, const hddvd_config_limits *, hddvd_vts **);
HDDVD_API void hddvd_vts_close(hddvd_vts *);
HDDVD_API int hddvd_vts_get_info(const hddvd_vts *, hddvd_vts_info *);
HDDVD_API size_t hddvd_vts_title_count(const hddvd_vts *);
HDDVD_API size_t hddvd_vts_chapter_count(const hddvd_vts *, size_t title);
HDDVD_API int hddvd_vts_get_chapter(const hddvd_vts *, size_t title, size_t chapter,
                                    hddvd_vts_chapter *);
HDDVD_API size_t hddvd_vts_pgc_count(const hddvd_vts *);
HDDVD_API int hddvd_vts_get_pgc(const hddvd_vts *, size_t pgc, hddvd_vts_pgc *);
/* The 1-based number of the cell where a program starts. */
HDDVD_API int hddvd_vts_get_program(const hddvd_vts *, size_t pgc, size_t program,
                                    uint16_t *first_cell);
HDDVD_API int hddvd_vts_get_cell(const hddvd_vts *, size_t pgc, size_t cell, hddvd_vts_cell *);
/* A raw 8-byte navigation command; its semantics are not decoded. */
HDDVD_API int hddvd_vts_get_command(const hddvd_vts *, size_t pgc, hddvd_vts_command_kind,
                                    size_t index, uint8_t command[8]);
/* A PGC's time map: its unit in seconds (0 for an empty map) and entry
   count. HDDVD_ENOTFOUND when the title set has no time map table. */
HDDVD_API int hddvd_vts_get_time_map(const hddvd_vts *, size_t pgc, uint8_t *unit_seconds,
                                     size_t *count);
HDDVD_API int hddvd_vts_get_time_map_entry(const hddvd_vts *, size_t pgc, size_t index,
                                           hddvd_vts_map_entry *);
HDDVD_API size_t hddvd_vts_evob_cell_count(const hddvd_vts *);
HDDVD_API int hddvd_vts_get_evob_cell(const hddvd_vts *, size_t, hddvd_vts_evob_cell *);
HDDVD_API size_t hddvd_vts_vobu_count(const hddvd_vts *);
HDDVD_API int hddvd_vts_get_vobu(const hddvd_vts *, size_t, uint32_t *sector);
/* Convert a stored time to the 90 kHz clock: 30 frames per second at
   30000/1001 for rate code 2. Other codes return HDDVD_ENOTSUP. */
HDDVD_API int hddvd_vts_time_to_ticks(const hddvd_vts_time *, hddvd_time *);
/* A title set's video attributes and its audio and subpicture streams, in
   stream number order. Open rejects more than 8 audio or 32 subpicture
   streams (HDDVD_EPARSE). */
HDDVD_API int hddvd_vts_get_video_attributes(const hddvd_vts *, hddvd_video_attributes *);
HDDVD_API size_t hddvd_vts_stream_count(const hddvd_vts *, hddvd_stream_kind);
HDDVD_API int hddvd_vts_get_stream_attributes(const hddvd_vts *, hddvd_stream_kind, size_t,
                                              hddvd_stream_attributes *);
/* Read the disc's title table (HVDVD-VMG100 IFO). Rules as for
   hddvd_vts_open; titles must have at least one angle and chapter, name a
   title set within the title-set count when the IFO gives one, and start
   after the VMG's own sectors. */
HDDVD_API int hddvd_vmg_open(hddvd_stream *, hddvd_vmg **);
HDDVD_API int hddvd_vmg_open_with_config(hddvd_stream *, const hddvd_config_limits *, hddvd_vmg **);
HDDVD_API void hddvd_vmg_close(hddvd_vmg *);
/* Last sector of the VMG, and the number of title sets (0 when the IFO has
   no title-set attribute table). */
HDDVD_API uint32_t hddvd_vmg_last_sector(const hddvd_vmg *);
HDDVD_API size_t hddvd_vmg_title_set_count(const hddvd_vmg *);
HDDVD_API size_t hddvd_vmg_title_count(const hddvd_vmg *);
HDDVD_API int hddvd_vmg_get_title(const hddvd_vmg *, size_t, hddvd_vmg_title *);
/* Convert a playlist time to title time on the 90 kHz clock. A "60fps"
   timeBase counts at 60000/1001 frames per second (docs/formats/ifo-map.md);
   other rates are unconfirmed and return HDDVD_ENOTSUP. Results that do not
   fit return HDDVD_ELIMIT. */
HDDVD_API int hddvd_xpl_time_to_ticks(hddvd_xpl_time, hddvd_time *);
/* Open the time maps of a title's primary audio/video clips, and the VTI
   each one names in its own directory, through the context's disc root.
   A clip needs a src and a titleTimeEnd after its titleTimeBegin (default
   0); its time map must name an EVOB in the VTI and cover clipTimeBegin
   (default 0) plus the clip's length. Failures are recorded on the context.
   A title without primary clips opens, and locates nothing. */
HDDVD_API int hddvd_title_map_open(hddvd_ctx *, const hddvd_xpl *, size_t, hddvd_title_map **);
HDDVD_API void hddvd_title_map_close(hddvd_title_map *);
/* Resolve a title time to the EVO and sector that play it. The first clip
   whose [titleTimeBegin, titleTimeEnd) contains the time is used; times
   that no clip covers return HDDVD_ENOTFOUND. */
HDDVD_API int hddvd_title_map_locate(const hddvd_title_map *, hddvd_time, hddvd_title_position *);
HDDVD_API void hddvd_free(void *);
#ifdef __cplusplus
}
#endif
#endif
