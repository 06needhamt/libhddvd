/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

/* A menu session driven as a player's overlay code would: an original
   application of loose files on a disc built here (two image buttons and a
   panel a key shows), then the fixture application from its ACA archive. */
#define ROOT "menu-disc"
#define ADV ROOT "/ADV_OBJ/"
#define MANIFEST "file:///dvddisc/ADV_OBJ/menu.xmf"
#define FRAMES(n) ((uint64_t)(n) * 3003u / 2u)

static const char manifest[] =
    "<Application xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Manifest'>"
    "<Region x='0' y='0' width='1920' height='1080'/>"
    "<Script src='lib.js'/><Script src='file:///dvddisc/ADV_OBJ/menu.js'/>"
    "<Markup src='menu.xmu'/></Application>";

static const char lib_js[] = "function byId(id) { return document.getElementById(id); }\n";

/* Enter on "left" shows the panel, on "right" jumps to the Feature title;
   one second after loading the focus moves to "right"; a title ending hides
   the panel. */
static const char menu_js[] =
    "function onKey(evt) {\n"
    "  if (evt.key != 0x0D) return;\n"
    "  if (evt.target.core.id == 'left') byId('panel').style.display = 'auto';\n"
    "  if (evt.target.core.id == 'right')\n"
    "    Player.playlist.titles['Feature'].jump('00:00:01:00', false);\n"
    "}\n"
    "setMarkupLoadedHandler(function () {\n"
    "  var timer;\n"
    "  byId('left').state.focused = true;\n"
    "  addEventListener('controller_key_down', onKey, false);\n"
    "  addEventListener('title_end', function () { byId('panel').style.display = 'none'; },\n"
    "                   false);\n"
    "  timer = createTimer('00:00:01:00', TIMER_APPLICATION,\n"
    "                      function () { byId('right').state.focused = true; });\n"
    "  timer.autoReset = false;\n"
    "  timer.enabled = true;\n"
    "});\n";

static const char markup[] =
    "<root xmlns='http://www.dvdforum.org/2005/ihd' "
    "xmlns:s='http://www.dvdforum.org/2005/ihd#style'><body>"
    "<button id='left' s:x='100px' s:y='100px' s:width='200px' s:height='100px' "
    "s:backgroundImage=\"url('a.png') url('b.png')\" s:navRight='right'/>"
    "<button id='right' s:x='400px' s:y='100px' s:width='200px' s:height='100px' "
    "s:backgroundImage=\"url('a.png') url('b.png')\" s:navLeft='left'/>"
    /* An image the disc lacks is not drawn. */
    "<div id='ghost' s:x='900px' s:y='900px' s:width='10px' s:height='10px' "
    "s:backgroundImage=\"url('missing.png')\"/>"
    "<div id='panel' s:x='0px' s:y='500px' s:width='300px' s:height='50px' "
    "s:backgroundColor='#102030' s:display='none'/>"
    "</body></root>";

static const char playlist[] =
    "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
    "<TitleSet timeBase='60fps'>"
    "<Title id='Menu' titleDuration='00:01:00:00'>"
    "<ApplicationSegment src='" MANIFEST "' autorun='true' titleTimeBegin='00:00:00:00' "
    "titleTimeEnd='00:01:00:00'/></Title>"
    "<Title id='Feature' titleDuration='00:10:00:00'/>"
    "</TitleSet></Playlist>";

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(data, 1, size, f) == size);
    fclose(f);
}

static void write_text(const char *path, const char *text)
{
    write_file(path, text, strlen(text));
}

static unsigned decoded, released;

static void release(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    released++;
    free((void *)pixels);
}

/* Every image is 4x4; the bytes are the file's. */
static int decode(void *opaque, const char *id, const void *data, size_t size,
                  hddvd_image_plane *plane)
{
    (void)opaque;
    (void)id;
    assert(size == 3 && (!memcmp(data, "AAA", 3) || !memcmp(data, "BBB", 3)));
    decoded++;
    memset(plane, 0, sizeof(*plane));
    plane->width = 4;
    plane->height = 4;
    plane->stride = 16;
    plane->pixels = calloc(4, 16);
    plane->release = release;
    return plane->pixels ? HDDVD_OK : HDDVD_ENOMEM;
}

static const char *state(const hddvd_menu *menu, size_t index, const char *name)
{
    const char *value = hddvd_runtime_get_state(hddvd_menu_runtime(menu), index, name);
    return value ? value : "";
}

static int overlaps(const hddvd_rect *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    return r->x < x + (int32_t)w && x < r->x + (int32_t)r->width && r->y < y + (int32_t)h &&
           y < r->y + (int32_t)r->height;
}

/* Whether the list's damage touches a rectangle. */
static int damaged(const hddvd_display_list *list, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    size_t i;
    for (i = 0; i < hddvd_display_damage_count(list); i++)
    {
        hddvd_rect r;
        assert(hddvd_display_get_damage(list, i, &r) == HDDVD_OK);
        if (overlaps(&r, x, y, w, h))
            return 1;
    }
    return 0;
}

static void advance(hddvd_menu *menu, uint64_t ticks)
{
    hddvd_time t;
    t.ticks = ticks;
    assert(hddvd_menu_advance(menu, t) == HDDVD_OK);
}

static void press(hddvd_menu *menu, uint32_t key)
{
    assert(hddvd_menu_key(menu, key, 1) == HDDVD_OK);
    assert(hddvd_menu_key(menu, key, 0) == HDDVD_OK);
}

static void session(hddvd_ctx *ctx, const hddvd_xpl *xpl)
{
    hddvd_menu *menu = NULL;
    const hddvd_display_list *list = NULL, *again = NULL;
    hddvd_runtime_command command;
    hddvd_video_transform transform;
    hddvd_event event;
    hddvd_time t;
    size_t left, right, focus;
    int changed = 0;

    assert(hddvd_menu_open(ctx, xpl, MANIFEST, decode, NULL, &menu) == HDDVD_OK && menu);
    assert(hddvd_runtime_issue_count(hddvd_menu_runtime(menu)) == 0);
    left = hddvd_runtime_focus(hddvd_menu_runtime(menu));
    assert(left != SIZE_MAX && !strcmp(state(menu, left, "focused"), "true"));

    /* The first list: both buttons' images; the panel is hidden. The
       focused button draws its focus image, so both files are decoded. */
    assert(hddvd_menu_update(menu, &list, &changed) == HDDVD_OK && list && changed);
    assert(hddvd_display_quad_count(list) == 2 && hddvd_display_item_count(list) == 2);
    assert(decoded == 2);
    assert(hddvd_menu_update(menu, &again, &changed) == HDDVD_OK && again == list && !changed);

    /* A key that moves the focus damages the old and the new button. */
    press(menu, HDDVD_KEY_RIGHT);
    right = hddvd_runtime_focus(hddvd_menu_runtime(menu));
    assert(right != left && right != SIZE_MAX);
    assert(hddvd_menu_update(menu, &list, &changed) == HDDVD_OK && changed);
    assert(damaged(list, 100, 100, 200, 100) && damaged(list, 400, 100, 200, 100));
    assert(!damaged(list, 0, 500, 300, 50));

    /* Enter on "right": a jump command, answered on the queue. */
    assert(hddvd_menu_take_command(menu, &command) == HDDVD_ENOTFOUND);
    assert(hddvd_menu_key(menu, HDDVD_KEY_ENTER, 1) == HDDVD_OK);
    assert(hddvd_menu_take_command(menu, &command) == HDDVD_OK);
    assert(command.type == HDDVD_RUNTIME_COMMAND_JUMP && command.title == 1 &&
           command.time.ticks == FRAMES(60));
    assert(hddvd_menu_take_command(menu, &command) == HDDVD_ENOTFOUND);
    assert(hddvd_menu_key(menu, HDDVD_KEY_ENTER, 0) == HDDVD_OK);
    assert(hddvd_menu_acknowledge(menu, command.id, HDDVD_COMMAND_ACCEPTED) == HDDVD_OK);
    assert(hddvd_menu_acknowledge(menu, command.id + 77, HDDVD_COMMAND_ACCEPTED) == HDDVD_EINVAL);
    assert(hddvd_runtime_issue_count(hddvd_menu_runtime(menu)) == 0);

    /* A 5:4 video area letterboxes the menu: 1280x720 from line 152. */
    assert(hddvd_menu_set_viewport(menu, 1280, 1024) == HDDVD_OK);
    assert(hddvd_menu_get_transform(menu, &transform) == HDDVD_OK);
    assert(transform.x == 0 && transform.y == 152 && transform.width == 1280 &&
           transform.height == 720);
    /* Logical (150, 150), inside "left": the pointer and the focus go there. */
    assert(hddvd_menu_pointer(menu, 100, 152 + 100, 0) == HDDVD_OK);
    assert(hddvd_runtime_focus(hddvd_menu_runtime(menu)) == left);
    assert(!strcmp(state(menu, left, "pointer"), "true"));
    assert(strcmp(state(menu, left, "actioned"), "true"));
    assert(hddvd_menu_pointer(menu, 100, 152 + 100, 1) == HDDVD_OK);
    assert(!strcmp(state(menu, left, "actioned"), "true"));
    assert(hddvd_menu_pointer(menu, 100, 152 + 100, 0) == HDDVD_OK);
    assert(!strcmp(state(menu, left, "actioned"), "false"));
    /* In the bar above the picture, and over nothing inside it: no element
       has the pointer; the focus stays. */
    assert(hddvd_menu_pointer(menu, 100, 10, 1) == HDDVD_OK);
    assert(!strcmp(state(menu, left, "pointer"), "false"));
    assert(hddvd_menu_pointer(menu, 100, 152 + 100, 1) == HDDVD_OK);
    assert(!strcmp(state(menu, left, "pointer"), "true"));
    assert(hddvd_menu_pointer(menu, 1200, 152 + 700, 0) == HDDVD_OK);
    assert(!strcmp(state(menu, left, "pointer"), "false") &&
           !strcmp(state(menu, left, "actioned"), "false"));
    assert(hddvd_runtime_focus(hddvd_menu_runtime(menu)) == left);
    assert(hddvd_runtime_pointer(hddvd_menu_runtime(menu), 100000, 0) == HDDVD_EINVAL);
    assert(hddvd_runtime_pointer(NULL, 0, 0) == HDDVD_EINVAL);
    /* A resize changes where the pointer lands, not the list. */
    assert(hddvd_menu_set_viewport(menu, 1920, 1080) == HDDVD_OK);
    assert(hddvd_menu_pointer(menu, 450, 150, 0) == HDDVD_OK);
    assert(hddvd_runtime_focus(hddvd_menu_runtime(menu)) == right);
    assert(hddvd_menu_set_viewport(menu, 0, 1080) == HDDVD_EINVAL);

    /* Enter on "left" shows the panel: a fill item, damaged where it is. */
    assert(hddvd_menu_pointer(menu, 150, 150, 0) == HDDVD_OK);
    assert(hddvd_menu_update(menu, &list, &changed) == HDDVD_OK && changed);
    press(menu, HDDVD_KEY_ENTER);
    assert(hddvd_menu_update(menu, &list, &changed) == HDDVD_OK && changed);
    assert(hddvd_display_item_count(list) == 3 && damaged(list, 0, 500, 300, 50));

    /* The script's timer, on the session's clock; time does not go back. */
    advance(menu, 45000);
    assert(hddvd_runtime_focus(hddvd_menu_runtime(menu)) == left);
    advance(menu, 90000 + 3003);
    focus = hddvd_runtime_focus(hddvd_menu_runtime(menu));
    assert(focus == right);
    t.ticks = 90000;
    assert(hddvd_menu_advance(menu, t) == HDDVD_EINVAL);
    /* A seek may go back; time then moves on from there. */
    assert(hddvd_menu_seek(menu, t) == HDDVD_OK);
    advance(menu, 90001);
    assert(hddvd_menu_seek(NULL, t) == HDDVD_EINVAL);

    /* A player event: the title ending hides the panel. */
    memset(&event, 0, sizeof(event));
    event.type = HDDVD_EVENT_TITLE_END;
    assert(hddvd_menu_post(menu, &event) == HDDVD_OK);
    assert(hddvd_menu_update(menu, &list, &changed) == HDDVD_OK && changed);
    assert(hddvd_display_item_count(list) == 2 && damaged(list, 0, 500, 300, 50));

    assert(hddvd_menu_update(menu, NULL, &changed) == HDDVD_EINVAL);
    assert(hddvd_menu_update(NULL, &list, &changed) == HDDVD_EINVAL);
    assert(hddvd_menu_advance(NULL, t) == HDDVD_EINVAL);
    assert(hddvd_menu_key(NULL, 1, 1) == HDDVD_EINVAL);
    assert(hddvd_menu_post(menu, NULL) == HDDVD_EINVAL);
    assert(hddvd_menu_pointer(NULL, 0, 0, 0) == HDDVD_EINVAL);
    assert(hddvd_menu_take_command(menu, NULL) == HDDVD_EINVAL);
    assert(hddvd_menu_acknowledge(NULL, 1, HDDVD_COMMAND_ACCEPTED) == HDDVD_EINVAL);
    assert(hddvd_menu_set_viewport(NULL, 1, 1) == HDDVD_EINVAL);
    assert(hddvd_menu_get_transform(menu, NULL) == HDDVD_EINVAL);
    assert(!hddvd_menu_runtime(NULL));
    /* Closing releases every decoded image. */
    hddvd_menu_close(menu);
    assert(decoded == released && decoded >= 2);
    hddvd_menu_close(NULL);
}

/* Reopening starts again from the authored state; without a decoder no
   image is drawn, and without a playlist the jump has no title to name. */
static void reopen(hddvd_ctx *ctx)
{
    hddvd_menu *menu = NULL;
    const hddvd_display_list *list;
    hddvd_runtime_command command;
    size_t left;
    assert(hddvd_menu_open(ctx, NULL, MANIFEST, NULL, NULL, &menu) == HDDVD_OK);
    left = hddvd_runtime_focus(hddvd_menu_runtime(menu));
    assert(hddvd_menu_update(menu, &list, NULL) == HDDVD_OK && list);
    assert(hddvd_display_quad_count(list) == 0);
    press(menu, HDDVD_KEY_RIGHT);
    assert(hddvd_runtime_focus(hddvd_menu_runtime(menu)) != left);
    /* The listener throws for lack of a playlist: reported, not fatal. */
    assert(hddvd_menu_key(menu, HDDVD_KEY_ENTER, 1) == HDDVD_ESCRIPT);
    assert(hddvd_runtime_issue_count(hddvd_menu_runtime(menu)) > 0);
    assert(hddvd_menu_take_command(menu, &command) == HDDVD_ENOTFOUND);
    hddvd_menu_close(menu);
}

static void failures(hddvd_ctx *ctx, const hddvd_xpl *xpl)
{
    hddvd_menu *menu = (hddvd_menu *)ctx;
    hddvd_error_detail detail;
    const hddvd_display_list *list;

    assert(hddvd_menu_open(ctx, xpl, "file:///dvddisc/ADV_OBJ/none.xmf", NULL, NULL, &menu) ==
               HDDVD_ENOTFOUND &&
           !menu);
    assert(hddvd_menu_open(NULL, xpl, MANIFEST, NULL, NULL, &menu) == HDDVD_EINVAL);
    assert(hddvd_menu_open(ctx, xpl, NULL, NULL, NULL, &menu) == HDDVD_EINVAL);
    assert(hddvd_menu_open(ctx, xpl, MANIFEST, NULL, NULL, NULL) == HDDVD_EINVAL);

    write_text(ADV "bad.xmf", "<Application");
    assert(hddvd_menu_open(ctx, xpl, "file:///dvddisc/ADV_OBJ/bad.xmf", NULL, NULL, &menu) ==
           HDDVD_EPARSE);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_EPARSE);

    /* Markup that is missing fails; a script that is missing, or throws,
       does not. */
    write_text(ADV "nomarkup.xmf",
               "<Application xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Manifest'>"
               "<Region x='0' y='0' width='1920' height='1080'/>"
               "<Markup src='none.xmu'/></Application>");
    assert(hddvd_menu_open(ctx, xpl, "file:///dvddisc/ADV_OBJ/nomarkup.xmf", NULL, NULL, &menu) ==
           HDDVD_ENOTFOUND);
    write_text(ADV "throws.js", "throw new Error('original');");
    write_text(ADV "scripts.xmf",
               "<Application xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Manifest'>"
               "<Region x='0' y='0' width='1920' height='1080'/>"
               "<Script src='none.js'/><Script src='throws.js'/><Script src='lib.js'/>"
               "</Application>");
    assert(hddvd_menu_open(ctx, xpl, "file:///dvddisc/ADV_OBJ/scripts.xmf", NULL, NULL, &menu) ==
           HDDVD_OK);
    /* The last script still ran; an application without markup has no list. */
    assert(hddvd_runtime_call(hddvd_menu_runtime(menu), "byId") != HDDVD_ENOTFOUND);
    assert(hddvd_menu_update(menu, &list, NULL) == HDDVD_OK && !list);
    assert(hddvd_menu_pointer(menu, 5, 5, 1) == HDDVD_OK);
    hddvd_menu_close(menu);
    remove(ADV "bad.xmf");
    remove(ADV "nomarkup.xmf");
    remove(ADV "throws.js");
    remove(ADV "scripts.xmf");
}

/* The fixture application, read from its ACA archive: Enter on "two" arms
   a timer that focuses "three", where Enter jumps to the Feature title. */
static void from_archive(hddvd_ctx *ctx)
{
    char path[512];
    FILE *in;
    hddvd_xpl *xpl = NULL;
    hddvd_menu *menu = NULL;
    hddvd_runtime_command command;
    static unsigned char bytes[1 << 16];
    size_t size;

    snprintf(path, sizeof(path), "%s/app/app.aca", HDDVD_FIXTURE_DIR);
    in = fopen(path, "rb");
    assert(in);
    size = fread(bytes, 1, sizeof(bytes), in);
    fclose(in);
    assert(size > 0 && size < sizeof(bytes));
    write_file(ADV "app.aca", bytes, size);
    snprintf(path, sizeof(path), "%s/app/app.xpl", HDDVD_FIXTURE_DIR);
    in = fopen(path, "rb");
    assert(in);
    size = fread(bytes, 1, sizeof(bytes), in);
    fclose(in);
    assert(hddvd_xpl_open(bytes, size, &xpl) == HDDVD_OK);

    assert(hddvd_menu_open(ctx, xpl, "file:///dvddisc/ADV_OBJ/app.aca/app.xmf", NULL, NULL,
                           &menu) == HDDVD_OK);
    assert(hddvd_runtime_issue_count(hddvd_menu_runtime(menu)) == 0);
    press(menu, HDDVD_KEY_RIGHT);
    press(menu, HDDVD_KEY_ENTER);
    advance(menu, 99000);
    press(menu, HDDVD_KEY_ENTER);
    assert(hddvd_menu_take_command(menu, &command) == HDDVD_OK);
    assert(command.type == HDDVD_RUNTIME_COMMAND_JUMP && command.title == 1);
    assert(hddvd_menu_acknowledge(menu, command.id, HDDVD_COMMAND_REJECTED) == HDDVD_OK);
    /* A rejected command is an issue for the host to show. */
    assert(hddvd_runtime_issue_count(hddvd_menu_runtime(menu)) == 1);
    hddvd_menu_close(menu);
    hddvd_xpl_close(xpl);
    remove(ADV "app.aca");
}

int main(void)
{
    hddvd_ctx *ctx;
    hddvd_xpl *xpl = NULL;
    hddvd_menu *menu = NULL;

    MKDIR(ROOT);
    MKDIR(ROOT "/ADV_OBJ");
    write_text(ADV "menu.xmf", manifest);
    write_text(ADV "lib.js", lib_js);
    write_text(ADV "menu.js", menu_js);
    write_text(ADV "menu.xmu", markup);
    write_text(ADV "a.png", "AAA");
    write_text(ADV "b.png", "BBB");
    ctx = hddvd_open(ROOT);
    assert(ctx && hddvd_xpl_open(playlist, sizeof(playlist) - 1, &xpl) == HDDVD_OK);

    if (!hddvd_runtime_engine())
    {
        /* Without an engine there is nothing to run a menu with. */
        assert(hddvd_menu_open(ctx, xpl, MANIFEST, NULL, NULL, &menu) == HDDVD_ENOTSUP && !menu);
    }
    else
    {
        session(ctx, xpl);
        reopen(ctx);
        failures(ctx, xpl);
        from_archive(ctx);
    }
    hddvd_xpl_close(xpl);
    hddvd_close(ctx);
    return 0;
}
