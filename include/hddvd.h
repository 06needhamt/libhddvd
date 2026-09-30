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
    typedef struct hddvd_object hddvd_object;
    typedef void (*hddvd_log_fn)(void*,int,const char*);
    typedef int (*hddvd_xmf_cb)(void*,const char*,const void*,size_t);
    typedef struct hddvd_aca_entry { uint32_t offset; uint32_t size; uint32_t crc32; uint8_t flags; char name[256]; } hddvd_aca_entry;
    typedef struct hddvd_limits { size_t max_aca_entries; uint64_t max_aca_member_size; } hddvd_limits;
    typedef int (*hddvd_aca_entry_cb)(void*,const hddvd_aca_entry*);
    typedef int (*hddvd_decrypt_fn)(void*,uint8_t,const void*,size_t,void**,size_t*);

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
    HDDVD_API int hddvd_scan_files(const hddvd_ctx*,hddvd_file_cb,void*);
    HDDVD_API const char *hddvd_file_kind_name(hddvd_file_kind);
    HDDVD_API int hddvd_xpl_find_start_ref(const char*,char**);
    HDDVD_API int hddvd_xmf_dump(const void*,size_t,const char*,void*);
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
    HDDVD_API int hddvd_object_parse(const void*,size_t,hddvd_object**);
    HDDVD_API void hddvd_object_free(hddvd_object*);
    HDDVD_API uint8_t hddvd_object_type(const hddvd_object*);
    HDDVD_API uint32_t hddvd_object_flags(const hddvd_object*);
    HDDVD_API const char *hddvd_object_name(const hddvd_object*);
    HDDVD_API size_t hddvd_object_payload_size(const hddvd_object*);
    HDDVD_API size_t hddvd_object_trailer_size(const hddvd_object*);
    HDDVD_API int hddvd_object_is_protected(const hddvd_object*);
    /* Returns a newly allocated clear payload. Protected objects require a host
       callback; libhddvd never supplies keys or implements AACS decryption. */
    HDDVD_API int hddvd_object_read_payload(const hddvd_object*,hddvd_decrypt_fn,void*,void**,size_t*);
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
