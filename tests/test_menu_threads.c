/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A mock player host around a menu session, as a player would arrange it:
   an input thread, a clock thread and a video-output thread post work to a
   queue, and one worker thread owns the session and does everything with
   it. The CI tsan job runs this under ThreadSanitizer; the session itself
   takes no locks, so any access from another thread would be a race. The
   application is the fixture one (tests/fixtures/app): three overlapping
   buttons, where Enter on "three" jumps to the Feature title. */
#define ROOT "menu-threads-disc"
#define KEYS 150
#define TICKS 300
#define RESIZES 60

typedef enum work_kind
{
    WORK_KEY,
    WORK_POINTER,
    WORK_TICK,
    WORK_RESIZE,
    WORK_REOPEN,
    WORK_STOP
} work_kind;

typedef struct work
{
    work_kind kind;
    uint32_t a;
    uint32_t b;
    int pressed;
} work;

#define QUEUE 64
static struct
{
    pthread_mutex_t lock;
    pthread_cond_t ready;
    pthread_cond_t space;
    work items[QUEUE];
    size_t first;
    size_t count;
    /* The clock the clock thread reads and advances, in 90 kHz ticks. */
    uint64_t now;
} queue = {PTHREAD_MUTEX_INITIALIZER,
           PTHREAD_COND_INITIALIZER,
           PTHREAD_COND_INITIALIZER,
           {{WORK_STOP, 0, 0, 0}},
           0,
           0,
           0};

static void push(work item)
{
    pthread_mutex_lock(&queue.lock);
    while (queue.count == QUEUE)
        pthread_cond_wait(&queue.space, &queue.lock);
    /* Times are stamped under the lock, so they reach the worker in order. */
    if (item.kind == WORK_TICK)
    {
        queue.now += item.a;
        item.a = (uint32_t)queue.now;
    }
    queue.items[(queue.first + queue.count++) % QUEUE] = item;
    pthread_cond_signal(&queue.ready);
    pthread_mutex_unlock(&queue.lock);
}

static work pop(void)
{
    work item;
    pthread_mutex_lock(&queue.lock);
    while (!queue.count)
        pthread_cond_wait(&queue.ready, &queue.lock);
    item = queue.items[queue.first];
    queue.first = (queue.first + 1) % QUEUE;
    queue.count--;
    pthread_cond_signal(&queue.space);
    pthread_mutex_unlock(&queue.lock);
    return item;
}

static void *input_thread(void *opaque)
{
    static const uint32_t keys[] = {HDDVD_KEY_RIGHT, HDDVD_KEY_ENTER, HDDVD_KEY_LEFT};
    work item = {WORK_KEY, 0, 0, 0};
    int i;
    (void)opaque;
    for (i = 0; i < KEYS; i++)
    {
        item.kind = WORK_KEY;
        item.a = keys[i % 3];
        item.pressed = 1;
        push(item);
        item.pressed = 0;
        push(item);
        /* The pointer wanders across the picture and its borders. */
        item.kind = WORK_POINTER;
        item.a = (uint32_t)(i * 37 % 2000);
        item.b = (uint32_t)(i * 53 % 1200);
        item.pressed = i & 1;
        push(item);
        if (i == KEYS / 2)
        {
            item.kind = WORK_REOPEN;
            push(item);
        }
    }
    return NULL;
}

static void *clock_thread(void *opaque)
{
    work item = {WORK_TICK, 1500, 0, 0};
    int i;
    (void)opaque;
    for (i = 0; i < TICKS; i++)
        push(item);
    return NULL;
}

static void *video_thread(void *opaque)
{
    work item = {WORK_RESIZE, 0, 0, 0};
    int i;
    (void)opaque;
    for (i = 0; i < RESIZES; i++)
    {
        item.a = 640u + (uint32_t)(i % 7) * 213u;
        item.b = 360u + (uint32_t)(i % 5) * 180u;
        push(item);
    }
    return NULL;
}

struct outcome
{
    size_t done;
    size_t rebuilt;
    size_t commands;
    size_t opened;
};

static hddvd_menu *open_menu(hddvd_ctx *ctx, const hddvd_xpl *xpl)
{
    hddvd_menu *menu = NULL;
    assert(hddvd_menu_open(ctx, xpl, "file:///dvddisc/ADV_OBJ/app.aca/app.xmf", NULL, NULL,
                           &menu) == HDDVD_OK);
    return menu;
}

/* After each piece of work: answer the commands and take the new list. */
static void settle(hddvd_menu *menu, struct outcome *out)
{
    hddvd_runtime_command command;
    const hddvd_display_list *list;
    int changed;
    while (hddvd_menu_take_command(menu, &command) == HDDVD_OK)
    {
        assert(command.type == HDDVD_RUNTIME_COMMAND_JUMP);
        assert(hddvd_menu_acknowledge(menu, command.id, HDDVD_COMMAND_ACCEPTED) == HDDVD_OK);
        out->commands++;
    }
    assert(hddvd_menu_update(menu, &list, &changed) == HDDVD_OK && list);
    out->rebuilt += (size_t)changed;
}

static unsigned char playlist_bytes[1 << 14];
static size_t playlist_size;

static void *worker_thread(void *opaque)
{
    struct outcome *out = opaque;
    hddvd_ctx *ctx = hddvd_open(ROOT);
    hddvd_xpl *xpl = NULL;
    hddvd_menu *menu;
    hddvd_time base = {0}, t;
    uint64_t latest = 0;
    assert(ctx && hddvd_xpl_open(playlist_bytes, playlist_size, &xpl) == HDDVD_OK);
    menu = open_menu(ctx, xpl);
    out->opened = 1;
    for (;;)
    {
        work item = pop();
        if (item.kind == WORK_STOP)
            break;
        switch (item.kind)
        {
            case WORK_KEY:
                assert(hddvd_menu_key(menu, item.a, item.pressed) == HDDVD_OK);
                break;
            case WORK_POINTER:
                assert(hddvd_menu_pointer(menu, (int32_t)item.a, (int32_t)item.b, item.pressed) ==
                       HDDVD_OK);
                break;
            case WORK_TICK:
                /* A reopened session starts its own clock at the reopen. */
                latest = item.a;
                t.ticks = latest - base.ticks;
                assert(hddvd_menu_advance(menu, t) == HDDVD_OK);
                break;
            case WORK_RESIZE:
                assert(hddvd_menu_set_viewport(menu, item.a, item.b) == HDDVD_OK);
                break;
            case WORK_REOPEN:
                hddvd_menu_close(menu);
                menu = open_menu(ctx, xpl);
                base.ticks = latest;
                out->opened++;
                break;
            default:
                break;
        }
        settle(menu, out);
        out->done++;
    }
    /* Whatever order the threads' work arrived in, the pointer over the
       picture puts the focus on the top button, where Enter jumps. */
    assert(hddvd_menu_set_viewport(menu, 1920, 1080) == HDDVD_OK);
    assert(hddvd_menu_pointer(menu, 960, 540, 0) == HDDVD_OK);
    assert(hddvd_menu_key(menu, HDDVD_KEY_ENTER, 1) == HDDVD_OK);
    settle(menu, out);
    hddvd_menu_close(menu);
    hddvd_xpl_close(xpl);
    hddvd_close(ctx);
    return NULL;
}

static size_t copy_fixture(const char *name, const char *to, unsigned char *keep, size_t room)
{
    static unsigned char bytes[1 << 16];
    char path[512];
    FILE *f;
    size_t size;
    snprintf(path, sizeof(path), "%s/app/%s", HDDVD_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    assert(f);
    size = fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
    assert(size > 0 && size < sizeof(bytes));
    if (to)
    {
        f = fopen(to, "wb");
        assert(f && fwrite(bytes, 1, size, f) == size);
        fclose(f);
    }
    if (keep)
    {
        assert(size <= room);
        memcpy(keep, bytes, size);
    }
    return size;
}

int main(void)
{
    pthread_t worker, producers[3];
    struct outcome out;
    work stop = {WORK_STOP, 0, 0, 0};
    int i;
    if (!hddvd_runtime_engine())
        return 0;
    mkdir(ROOT, 0755);
    mkdir(ROOT "/ADV_OBJ", 0755);
    copy_fixture("app.aca", ROOT "/ADV_OBJ/app.aca", NULL, 0);
    playlist_size = copy_fixture("app.xpl", NULL, playlist_bytes, sizeof(playlist_bytes));

    memset(&out, 0, sizeof(out));
    assert(!pthread_create(&worker, NULL, worker_thread, &out));
    assert(!pthread_create(&producers[0], NULL, input_thread, NULL));
    assert(!pthread_create(&producers[1], NULL, clock_thread, NULL));
    assert(!pthread_create(&producers[2], NULL, video_thread, NULL));
    for (i = 0; i < 3; i++)
        assert(!pthread_join(producers[i], NULL));
    push(stop);
    assert(!pthread_join(worker, NULL));

    assert(out.done == (size_t)KEYS * 3 + 1 + TICKS + RESIZES);
    assert(out.opened == 2 && out.rebuilt > 0 && out.commands > 0);
    remove(ROOT "/ADV_OBJ/app.aca");
    rmdir(ROOT "/ADV_OBJ");
    rmdir(ROOT);
    return 0;
}
