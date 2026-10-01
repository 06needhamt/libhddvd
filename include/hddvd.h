#ifndef LIBHDDVD_H
#define LIBHDDVD_H
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
# ifdef HDDVD_BUILD_SHARED
#  define HDDVD_API __declspec(dllexport)
# elif defined(HDDVD_USE_SHARED)
#  define HDDVD_API __declspec(dllimport)
# else
#  define HDDVD_API
# endif
#else
# define HDDVD_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C"
{
    #endif
    #define HDDVD_VERSION_MAJOR 0
    #define HDDVD_VERSION_MINOR 4
    #define HDDVD_VERSION_PATCH 0
    #define HDDVD_PATH_MAX 1024

    typedef enum hddvd_status { HDDVD_OK=0,HDDVD_EINVAL=-1,HDDVD_ENOMEM=-2,HDDVD_EIO=-3,HDDVD_EPARSE=-4,HDDVD_ENOTFOUND=-5,HDDVD_ENOTSUP=-6,HDDVD_ELIMIT=-7,HDDVD_EPROTECTED=-8 } hddvd_status;
    typedef struct hddvd_ctx hddvd_ctx;
    typedef struct hddvd_stream hddvd_stream;
    typedef struct hddvd_aca hddvd_aca;
    typedef struct hddvd_uri hddvd_uri;
    typedef struct hddvd_xmf hddvd_xmf;
    typedef struct hddvd_object hddvd_object;
    typedef struct hddvd_script hddvd_script;
    typedef struct hddvd_image_cache hddvd_image_cache;
    typedef struct hddvd_display_list hddvd_display_list;
    typedef void (*hddvd_log_fn)(void*,int,const char*);
    typedef int (*hddvd_xmf_cb)(void*,const char*,const void*,size_t);
    typedef struct hddvd_aca_entry { uint32_t offset; uint32_t size; uint32_t crc32; uint8_t flags; char name[256]; } hddvd_aca_entry;
    typedef struct hddvd_limits { size_t max_aca_entries; uint64_t max_aca_member_size; } hddvd_limits;
    typedef struct hddvd_xmf_limits { size_t max_nodes; size_t max_references; } hddvd_xmf_limits;
    typedef struct hddvd_xmf_region { uint32_t x; uint32_t y; uint32_t width; uint32_t height; } hddvd_xmf_region;
    typedef struct hddvd_script_limits { size_t max_strings; size_t max_functions; size_t max_instructions; uint64_t max_bytes; } hddvd_script_limits;
    typedef struct hddvd_script_function { uint32_t name_index; uint32_t first_instruction; uint32_t instruction_count; } hddvd_script_function;
    typedef struct hddvd_script_instruction { uint8_t opcode; uint8_t flags; int32_t operand; } hddvd_script_instruction;
    typedef struct hddvd_rect { int32_t x; int32_t y; uint32_t width; uint32_t height; } hddvd_rect;
    typedef struct hddvd_image_plane { const uint8_t *pixels; uint32_t width; uint32_t height; size_t stride; void (*release)(void*,const uint8_t*); void *release_opaque; } hddvd_image_plane;
    typedef int (*hddvd_image_decode_fn)(void*,const char*,const void*,size_t,hddvd_image_plane*);
    typedef struct hddvd_display_limits { size_t max_images; uint64_t max_encoded_bytes; uint64_t max_decoded_pixels; uint32_t max_dimension; size_t max_quads; } hddvd_display_limits;
    typedef struct hddvd_image_source { const char *identity; const void *encoded; size_t encoded_size; } hddvd_image_source;
    /* Parents must precede children. Geometry is parent-relative; clip, when
       non-empty, is node-relative. Equal z values retain authored order. */
    typedef struct hddvd_scene_node { uint64_t id; size_t parent; hddvd_rect bounds; hddvd_rect clip; int32_t z_index; uint8_t opacity; uint8_t visible; uint8_t focused; uint8_t hit_test; hddvd_image_source normal_image; hddvd_image_source focused_image; } hddvd_scene_node;
    typedef struct hddvd_display_quad { uint64_t node_id; hddvd_image_plane image; hddvd_rect source; hddvd_rect destination; hddvd_rect clip; int32_t z_index; uint8_t opacity; } hddvd_display_quad;
    typedef struct hddvd_video_transform { int32_t x; int32_t y; uint32_t width; uint32_t height; uint32_t logical_width; uint32_t logical_height; } hddvd_video_transform;
    typedef int (*hddvd_aca_entry_cb)(void*,const hddvd_aca_entry*);

    typedef enum hddvd_object_type { HDDVD_OBJECT_UNKNOWN=0,HDDVD_OBJECT_MANIFEST=1,HDDVD_OBJECT_MARKUP=2,HDDVD_OBJECT_SCRIPT=3,HDDVD_OBJECT_IMAGE=4 } hddvd_object_type;
    typedef struct hddvd_object_info { uint8_t version; uint8_t flags; uint16_t raw_type; uint32_t unknown_1; uint64_t declared_length; uint64_t payload_offset; uint64_t payload_length; uint64_t trailer_offset; uint32_t trailer_length; } hddvd_object_info;
    /* A provider returns a newly allocated clear payload in *output.  The
       library releases it with free().  Providers must not call back into the
       object being opened. */
    typedef int (*hddvd_decrypt_fn)(void*,const hddvd_object_info*,const void*,size_t,void**,size_t*);

    typedef enum hddvd_file_kind { HDDVD_FILE_UNKNOWN=0,HDDVD_FILE_IFO,HDDVD_FILE_BUP,HDDVD_FILE_VTI,HDDVD_FILE_MAP,HDDVD_FILE_EVO,HDDVD_FILE_XPL,HDDVD_FILE_ACA,HDDVD_FILE_DISCID } hddvd_file_kind;
    typedef struct hddvd_file_info { hddvd_file_kind kind; uint64_t size; char path[HDDVD_PATH_MAX]; } hddvd_file_info;
    typedef int (*hddvd_file_cb)(void*,const hddvd_file_info*);

    typedef enum hddvd_stream_codec { HDDVD_CODEC_UNKNOWN=0,HDDVD_CODEC_MPEG2_VIDEO,HDDVD_CODEC_H264,HDDVD_CODEC_VC1,HDDVD_CODEC_MPEG_AUDIO,HDDVD_CODEC_AC3_EAC3,HDDVD_CODEC_DTS_HD,HDDVD_CODEC_LPCM,HDDVD_CODEC_TRUEHD_MLP,HDDVD_CODEC_SUBPICTURE } hddvd_stream_codec;
    typedef struct hddvd_stream_info { uint8_t stream_id; uint8_t substream_id; hddvd_stream_codec codec; uint64_t packet_count; } hddvd_stream_info;

    typedef struct hddvd_map_info { uint64_t size; uint32_t sector_count; int structure_validated; } hddvd_map_info;

    typedef struct hddvd_ifo_info { uint64_t size; char identifier[17]; int recognized_identifier; } hddvd_ifo_info;

    HDDVD_API const char *hddvd_version(void);
    HDDVD_API const char *hddvd_strerror(int);
    HDDVD_API hddvd_ctx *hddvd_open(const char*);
    HDDVD_API void hddvd_close(hddvd_ctx*);
    HDDVD_API const char *hddvd_root(const hddvd_ctx*);
    HDDVD_API int hddvd_has_advanced_content(const hddvd_ctx*);
    HDDVD_API void hddvd_set_log_callback(hddvd_ctx*,hddvd_log_fn,void*);
    /* Streams are single-threaded. Reads may be short only at EOF. Memory input
       is copied. Closing an ACA does not invalidate an open member stream. */
    HDDVD_API int hddvd_stream_open_file(const char*,hddvd_stream**);
    HDDVD_API int hddvd_stream_open_memory(const void*,size_t,hddvd_stream**);
    HDDVD_API uint64_t hddvd_stream_size(const hddvd_stream*);
    HDDVD_API int hddvd_stream_read_at(hddvd_stream*,uint64_t,void*,size_t,size_t*);
    HDDVD_API void hddvd_stream_close(hddvd_stream*);
    /* Object input is copied/retained by the returned immutable handle. */
    HDDVD_API int hddvd_object_open(const void*,size_t,hddvd_object**);
    HDDVD_API int hddvd_object_open_with_provider(const void*,size_t,hddvd_decrypt_fn,void*,hddvd_object**);
    HDDVD_API int hddvd_object_open_stream(hddvd_stream*,hddvd_object**);
    HDDVD_API int hddvd_object_open_unwrapped(hddvd_object_type,const void*,size_t,hddvd_object**);
    HDDVD_API void hddvd_object_close(hddvd_object*);
    HDDVD_API int hddvd_object_get_info(const hddvd_object*,hddvd_object_info*);
    HDDVD_API hddvd_object_type hddvd_object_get_type(const hddvd_object*);
    HDDVD_API const char *hddvd_object_name(const hddvd_object*);
    HDDVD_API int hddvd_object_open_payload(const hddvd_object*,hddvd_stream**);
    HDDVD_API int hddvd_object_get_trailer(const hddvd_object*,const void**,size_t*);
    /* The decoder exposes validated, engine-neutral script structure. Strings
       are borrowed and all indexes remain valid until hddvd_script_close(). */
    HDDVD_API void hddvd_script_limits_default(hddvd_script_limits*);
    HDDVD_API int hddvd_script_open(const void*,size_t,hddvd_script**);
    HDDVD_API int hddvd_script_open_with_limits(const void*,size_t,const hddvd_script_limits*,hddvd_script**);
    HDDVD_API void hddvd_script_close(hddvd_script*);
    HDDVD_API uint16_t hddvd_script_version(const hddvd_script*);
    HDDVD_API size_t hddvd_script_string_count(const hddvd_script*);
    HDDVD_API const char *hddvd_script_string(const hddvd_script*,size_t);
    HDDVD_API size_t hddvd_script_function_count(const hddvd_script*);
    HDDVD_API int hddvd_script_get_function(const hddvd_script*,size_t,hddvd_script_function*);
    HDDVD_API size_t hddvd_script_instruction_count(const hddvd_script*);
    HDDVD_API int hddvd_script_get_instruction(const hddvd_script*,size_t,hddvd_script_instruction*);
    /* Decoders must return premultiplied RGBA8. The cache calls release exactly
       once after the last display list using a plane is closed. All objects are
       single-threaded and callbacks are made only by cache_get/build calls. */
    HDDVD_API void hddvd_display_limits_default(hddvd_display_limits*);
    HDDVD_API int hddvd_image_cache_create(hddvd_image_decode_fn,void*,const hddvd_display_limits*,hddvd_image_cache**);
    HDDVD_API void hddvd_image_cache_destroy(hddvd_image_cache*);
    HDDVD_API int hddvd_display_build(hddvd_image_cache*,uint32_t,uint32_t,const hddvd_scene_node*,size_t,const hddvd_display_list*,hddvd_display_list**);
    HDDVD_API void hddvd_display_list_close(hddvd_display_list*);
    HDDVD_API size_t hddvd_display_quad_count(const hddvd_display_list*);
    HDDVD_API int hddvd_display_get_quad(const hddvd_display_list*,size_t,hddvd_display_quad*);
    HDDVD_API size_t hddvd_display_damage_count(const hddvd_display_list*);
    HDDVD_API int hddvd_display_get_damage(const hddvd_display_list*,size_t,hddvd_rect*);
    HDDVD_API void hddvd_video_transform_fit(uint32_t,uint32_t,uint32_t,uint32_t,hddvd_video_transform*);
    HDDVD_API int hddvd_video_to_logical(const hddvd_video_transform*,int32_t,int32_t,int32_t*,int32_t*);
    HDDVD_API int hddvd_display_hit_test(const hddvd_display_list*,int32_t,int32_t,uint64_t*);
    HDDVD_API int hddvd_scan_files(const hddvd_ctx*,hddvd_file_cb,void*);
    HDDVD_API const char *hddvd_file_kind_name(hddvd_file_kind);
    HDDVD_API int hddvd_xpl_find_start_ref(const char*,char**);
    HDDVD_API int hddvd_xmf_dump(const void*,size_t,const char*,void*);
    /* XMF objects are immutable. All returned strings are borrowed and remain
       valid until hddvd_xmf_close(). Inputs are copied by the parser. */
    HDDVD_API void hddvd_xmf_limits_default(hddvd_xmf_limits*);
    HDDVD_API int hddvd_xmf_open(const void*,size_t,hddvd_xmf**);
    HDDVD_API int hddvd_xmf_open_with_limits(const void*,size_t,const hddvd_xmf_limits*,hddvd_xmf**);
    HDDVD_API void hddvd_xmf_close(hddvd_xmf*);
    HDDVD_API const char *hddvd_xmf_application_id(const hddvd_xmf*);
    HDDVD_API int hddvd_xmf_get_region(const hddvd_xmf*,hddvd_xmf_region*);
    HDDVD_API size_t hddvd_xmf_script_count(const hddvd_xmf*);
    HDDVD_API int hddvd_xmf_get_script(const hddvd_xmf*,size_t,const char**,const char**);
    HDDVD_API size_t hddvd_xmf_markup_count(const hddvd_xmf*);
    HDDVD_API int hddvd_xmf_get_markup(const hddvd_xmf*,size_t,const char**,const char**);
    HDDVD_API size_t hddvd_xmf_resource_root_count(const hddvd_xmf*);
    HDDVD_API const char *hddvd_xmf_resource_root(const hddvd_xmf*,size_t);
    HDDVD_API int hddvd_aca_supported(void);
    HDDVD_API int hddvd_aca_open(const char*,hddvd_aca**);
    HDDVD_API int hddvd_aca_open_stream(hddvd_stream*,hddvd_aca**);
    HDDVD_API int hddvd_aca_open_stream_with_limits(hddvd_stream*,const hddvd_limits*,hddvd_aca**);
    HDDVD_API void hddvd_limits_default(hddvd_limits*);
    HDDVD_API void hddvd_aca_close(hddvd_aca*);
    HDDVD_API size_t hddvd_aca_count(const hddvd_aca*);
    HDDVD_API int hddvd_aca_get_entry(const hddvd_aca*,size_t,hddvd_aca_entry*);
    HDDVD_API int hddvd_aca_open_member(const hddvd_aca*,const char*,hddvd_stream**);
    HDDVD_API int hddvd_aca_list(const char*,hddvd_aca_entry_cb,void*);
    HDDVD_API int hddvd_aca_verify(const char*,size_t*);
    HDDVD_API int hddvd_aca_read_member(const char*,const char*,void**,size_t*);
    HDDVD_API int hddvd_aca_extract(const char*,const char*);
    HDDVD_API int hddvd_aca_visit_xmf(const char*,hddvd_xmf_cb,void*);
    /* URI getters return borrowed strings valid until hddvd_uri_free(). */
    HDDVD_API int hddvd_uri_parse(const char*,hddvd_uri**);
    HDDVD_API void hddvd_uri_free(hddvd_uri*);
    HDDVD_API const char *hddvd_uri_disc_path(const hddvd_uri*);
    HDDVD_API const char *hddvd_uri_archive_name(const hddvd_uri*);
    HDDVD_API const char *hddvd_uri_member_name(const hddvd_uri*);
    HDDVD_API int hddvd_open_resource(const hddvd_ctx*,const char*,hddvd_stream**);
    HDDVD_API int hddvd_xmu_dump(const void*,size_t,const char*,void*);
    HDDVD_API int hddvd_evo_probe(const char*,hddvd_stream_info*,size_t,size_t*);
    HDDVD_API const char *hddvd_codec_name(hddvd_stream_codec);
    HDDVD_API int hddvd_map_probe(const char*,hddvd_map_info*);
    HDDVD_API int hddvd_ifo_probe(const char*,hddvd_ifo_info*);
    HDDVD_API void hddvd_free(void*);
    #ifdef __cplusplus
}
#endif
#endif
