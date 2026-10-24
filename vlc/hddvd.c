/*****************************************************************************
 * hddvd.c: HD DVD input through libhddvd
 *****************************************************************************
 * The project licence (LICENSE) applies; see vlc/README.md about the notice
 * an upstream submission needs.
 *****************************************************************************/

/* A stream access for HD DVD Standard Content titles. libhddvd lists the
   disc's titles and chapters and reads a title's EVO sectors as one byte
   stream; VLC's MPEG-PS demuxer (asked for through the content type) reads
   that stream and passes title and chapter controls back here, as it does
   for VCDs. Encrypted (AACS) titles stop with an error dialog: libhddvd
   does not decrypt them. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_access.h>
#include <vlc_arrays.h>
#include <vlc_dialog.h>
#include <vlc_input.h>

#include <hddvd.h>

/* VLC's own build defines these in config.h (vlc_fixups.h); an
   out-of-tree build does not. */
#ifndef N_
#define N_(str) (str)
#endif
#ifndef _
#define _(str) vlc_gettext(str)
#endif

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

vlc_module_begin()
    set_shortname(N_("HD DVD"))
    set_description(N_("HD DVD input (libhddvd, no menus)"))
    set_subcategory(SUBCAT_INPUT_ACCESS)
    /* Tried for file:// before the file system access, as dvdnav and bluray
       are, so that opening a disc directory finds its titles. Open accepts
       only directories with an HD DVD title table; a file fails at the
       first directory lookup. */
    set_capability("access", 300)
    add_shortcut("hddvd", "file")
    set_callbacks(Open, Close)
vlc_module_end()

typedef struct
{
    hddvd_ctx *ctx;
    hddvd_titles *titles;
    unsigned title;
    unsigned seekpoint;
    uint64_t offset;
    bool failed;
} access_sys_t;

static vlc_tick_t ToTick(hddvd_time time)
{
    return vlc_tick_from_samples(time.ticks, 90000);
}

/* Tells the user why reading stopped, once. */
static void ReportError(stream_t *access, int status)
{
    access_sys_t *sys = access->p_sys;
    hddvd_error_detail detail;

    if (sys->failed)
        return;
    sys->failed = true;
    if (hddvd_get_last_error(sys->ctx, &detail) == HDDVD_OK)
        msg_Err(access, "%s: %s (%s)", detail.resource, detail.message, hddvd_strerror(status));
    if (status == HDDVD_EPROTECTED)
        vlc_dialog_display_error(access, _("Encrypted HD DVD"), "%s",
                                 _("This HD DVD title is protected by AACS. libhddvd does "
                                   "not decrypt protected content, so it cannot be played."));
    else
        vlc_dialog_display_error(access, _("HD DVD read error"),
                                 _("This HD DVD title could not be read: %s."),
                                 hddvd_strerror(status));
}

static void UpdateSeekpoint(stream_t *access)
{
    access_sys_t *sys = access->p_sys;
    size_t chapter;

    if (hddvd_titles_chapter_at(sys->titles, sys->title, sys->offset, &chapter) == HDDVD_OK)
        sys->seekpoint = (unsigned)chapter;
}

static ssize_t Read(stream_t *access, void *buffer, size_t size)
{
    access_sys_t *sys = access->p_sys;
    size_t got = 0;
    int status;

    if (sys->failed)
        return 0;
    status = hddvd_titles_read(sys->titles, sys->title, sys->offset, buffer, size, &got);
    sys->offset += got;
    UpdateSeekpoint(access);
    /* Hand over what was read; the failure comes back on the next call. */
    if (status != HDDVD_OK && got == 0)
        ReportError(access, status);
    return (ssize_t)got;
}

static int Seek(stream_t *access, uint64_t offset)
{
    access_sys_t *sys = access->p_sys;

    sys->offset = offset;
    UpdateSeekpoint(access);
    return VLC_SUCCESS;
}

static input_title_t *NewTitle(const hddvd_titles *titles, size_t index)
{
    hddvd_title_info info;
    input_title_t *title;

    if (hddvd_titles_get_info(titles, index, &info) != HDDVD_OK)
        return NULL;
    title = vlc_input_title_New();
    if (title == NULL)
        return NULL;
    title->i_length = ToTick(info.duration);
    for (size_t i = 0; i < info.chapter_count; i++)
    {
        hddvd_title_chapter chapter;
        seekpoint_t *seekpoint = vlc_seekpoint_New();

        if (seekpoint == NULL || hddvd_titles_get_chapter(titles, index, i, &chapter) != HDDVD_OK)
        {
            if (seekpoint != NULL)
                vlc_seekpoint_Delete(seekpoint);
            vlc_input_title_Delete(title);
            return NULL;
        }
        seekpoint->i_time_offset = ToTick(chapter.start);
        TAB_APPEND(title->i_seekpoint, title->seekpoint, seekpoint);
    }
    return title;
}

static int Control(stream_t *access, int query, va_list args)
{
    access_sys_t *sys = access->p_sys;
    size_t count = hddvd_titles_count(sys->titles);

    switch (query)
    {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg(args, bool *) = true;
            return VLC_SUCCESS;

        case STREAM_GET_SIZE:
        {
            hddvd_title_info info;

            if (hddvd_titles_get_info(sys->titles, sys->title, &info) != HDDVD_OK)
                return VLC_EGENERIC;
            *va_arg(args, uint64_t *) = info.size;
            return VLC_SUCCESS;
        }

        case STREAM_GET_PTS_DELAY:
            *va_arg(args, vlc_tick_t *) =
                VLC_TICK_FROM_MS(var_InheritInteger(access, "disc-caching"));
            return VLC_SUCCESS;

        case STREAM_SET_PAUSE_STATE:
            return VLC_SUCCESS;

        case STREAM_GET_CONTENT_TYPE:
        {
            char *type = strdup("video/MP2P");

            if (type == NULL)
                return VLC_ENOMEM;
            *va_arg(args, char **) = type;
            return VLC_SUCCESS;
        }

        case STREAM_GET_TITLE_INFO:
        {
            input_title_t ***out = va_arg(args, input_title_t ***);
            int *out_count = va_arg(args, int *);
            input_title_t **list = vlc_alloc(count, sizeof(*list));

            if (list == NULL)
                return VLC_ENOMEM;
            for (size_t i = 0; i < count; i++)
            {
                list[i] = NewTitle(sys->titles, i);
                if (list[i] == NULL)
                {
                    while (i > 0)
                        vlc_input_title_Delete(list[--i]);
                    free(list);
                    return VLC_ENOMEM;
                }
            }
            *out = list;
            *out_count = (int)count;
            return VLC_SUCCESS;
        }

        case STREAM_GET_TITLE:
            *va_arg(args, unsigned *) = sys->title;
            return VLC_SUCCESS;

        case STREAM_GET_SEEKPOINT:
            *va_arg(args, unsigned *) = sys->seekpoint;
            return VLC_SUCCESS;

        case STREAM_SET_TITLE:
        {
            int title = va_arg(args, int);

            if (title < 0 || (size_t)title >= count)
                return VLC_EGENERIC;
            sys->title = (unsigned)title;
            sys->seekpoint = 0;
            sys->offset = 0;
            sys->failed = false;
            msg_Dbg(access, "title %u chapter 0 at offset 0", sys->title);
            return VLC_SUCCESS;
        }

        case STREAM_SET_SEEKPOINT:
        {
            int seekpoint = va_arg(args, int);
            hddvd_title_chapter chapter;

            if (seekpoint < 0 || hddvd_titles_get_chapter(sys->titles, sys->title,
                                                          (size_t)seekpoint, &chapter) != HDDVD_OK)
                return VLC_EGENERIC;
            sys->seekpoint = (unsigned)seekpoint;
            sys->offset = chapter.offset;
            msg_Dbg(access, "title %u chapter %u at offset %" PRIu64, sys->title, sys->seekpoint,
                    sys->offset);
            return VLC_SUCCESS;
        }

        default:
            return VLC_EGENERIC;
    }
}

static int Open(vlc_object_t *obj)
{
    stream_t *access = (stream_t *)obj;
    bool forced = access->psz_name != NULL && !strcmp(access->psz_name, "hddvd");
    access_sys_t *sys;
    int status;

    if (access->psz_filepath == NULL || access->psz_filepath[0] == '\0')
        return VLC_EGENERIC;

    sys = vlc_obj_calloc(obj, 1, sizeof(*sys));
    if (sys == NULL)
        return VLC_ENOMEM;
    sys->ctx = hddvd_open(access->psz_filepath);
    if (sys->ctx == NULL)
        return VLC_ENOMEM;
    status = hddvd_titles_open(sys->ctx, &sys->titles);
    if (status == HDDVD_OK && hddvd_titles_count(sys->titles) == 0)
    {
        hddvd_titles_close(sys->titles);
        sys->titles = NULL;
        status = HDDVD_ENOTFOUND;
    }
    if (status != HDDVD_OK)
    {
        hddvd_error_detail detail;

        /* Without hddvd://, anything but an HD DVD disc is someone else's. */
        if (forced && hddvd_get_last_error(sys->ctx, &detail) == HDDVD_OK)
            msg_Err(access, "%s: %s (%s)", detail.resource, detail.message, hddvd_strerror(status));
        hddvd_close(sys->ctx);
        return VLC_EGENERIC;
    }

    msg_Dbg(access, "libhddvd %s: %zu titles", hddvd_version(), hddvd_titles_count(sys->titles));
    access->p_sys = sys;
    access->pf_read = Read;
    access->pf_block = NULL;
    access->pf_seek = Seek;
    access->pf_control = Control;
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *obj)
{
    stream_t *access = (stream_t *)obj;
    access_sys_t *sys = access->p_sys;

    hddvd_titles_close(sys->titles);
    hddvd_close(sys->ctx);
}
