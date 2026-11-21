/* A window for the headless checks: like the dummy window, but it hands
   itself to the check program (check_window, looked up in the process), so
   that the check can report mouse events through VLC's own video output
   path, as a real window does. Its priority puts it before every other
   window; it is only on the plugin path (VLC_PLUGIN_PATH) for the check. */
#define MODULE_NAME testwin
#define MODULE_STRING "testwin"
#define VLC_DYNAMIC_PLUGIN
#include <dlfcn.h>
#include <stdlib.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_window.h>

typedef void (*check_fn)(void *window, unsigned width, unsigned height,
                         void (*move)(void *, int, int), void (*press)(void *, int),
                         void (*release)(void *, int));

static void Move(void *window, int x, int y)
{
    vlc_window_ReportMouseMoved(window, x, y);
}

static void Press(void *window, int button)
{
    vlc_window_ReportMousePressed(window, button);
}

static void Release(void *window, int button)
{
    vlc_window_ReportMouseReleased(window, button);
}

static int Enable(vlc_window_t *wnd, const vlc_window_cfg_t *cfg)
{
    check_fn check = (check_fn)dlsym(RTLD_DEFAULT, "check_window");

    vlc_window_ReportSize(wnd, cfg->width, cfg->height);
    if (check != NULL)
        check(wnd, cfg->width, cfg->height, Move, Press, Release);
    return VLC_SUCCESS;
}

static void Disable(vlc_window_t *wnd)
{
    check_fn check = (check_fn)dlsym(RTLD_DEFAULT, "check_window");

    if (check != NULL)
        check(NULL, 0, 0, NULL, NULL, NULL);
    (void)wnd;
}

static const struct vlc_window_operations ops = {
    .enable = Enable,
    .disable = Disable,
    .resize = vlc_window_ReportSize,
};

static int Open(vlc_window_t *wnd)
{
    wnd->type = VLC_WINDOW_TYPE_DUMMY;
    wnd->ops = &ops;
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("testwin")
    set_description("Window for headless checks")
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout window", 1000)
    set_callback(Open)
    add_shortcut("testwin")
vlc_module_end()
