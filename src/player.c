#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct timer_entry
{
    uint64_t id;
    hddvd_time due;
    uint64_t order;
};
struct hddvd_nav
{
    hddvd_event *events;
    size_t event_count, event_cap;
    struct timer_entry *timers;
    size_t timer_count, timer_cap;
    uint64_t next_sequence, next_command, next_timer_order;
    hddvd_time now;
    /* Time of the latest discontinuity; no timer is stamped earlier. */
    hddvd_time floor;
    hddvd_event_fn event_fn;
    hddvd_command_fn command_fn;
    void *opaque;
};

int hddvd_time_from_milliseconds(uint64_t value, hddvd_time *out)
{
    if (!out || value > UINT64_MAX / 90u)
        return HDDVD_EINVAL;
    out->ticks = value * 90u;
    return HDDVD_OK;
}
int hddvd_time_to_milliseconds(hddvd_time value, uint64_t *out)
{
    if (!out)
        return HDDVD_EINVAL;
    *out = value.ticks / 90u;
    return HDDVD_OK;
}
int hddvd_time_from_seconds(uint64_t value, hddvd_time *out)
{
    if (!out || value > UINT64_MAX / 90000u)
        return HDDVD_EINVAL;
    out->ticks = value * 90000u;
    return HDDVD_OK;
}
void hddvd_nav_limits_default(hddvd_nav_limits *l)
{
    if (l)
    {
        l->max_events = 1024;
        l->max_timers = 256;
    }
}
int hddvd_nav_create(const hddvd_nav_limits *given, hddvd_event_fn ef, hddvd_command_fn cf,
                     void *opaque, hddvd_nav **out)
{
    hddvd_nav_limits d;
    hddvd_nav *n;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    hddvd_nav_limits_default(&d);
    if (given)
        d = *given;
    if (!d.max_events || !d.max_timers)
        return HDDVD_EINVAL;
    n = (hddvd_nav *)calloc(1, sizeof(*n));
    if (!n)
        return HDDVD_ENOMEM;
    n->events = (hddvd_event *)calloc(d.max_events, sizeof(*n->events));
    n->timers = (struct timer_entry *)calloc(d.max_timers, sizeof(*n->timers));
    if (!n->events || !n->timers)
    {
        hddvd_nav_destroy(n);
        return HDDVD_ENOMEM;
    }
    n->event_cap = d.max_events;
    n->timer_cap = d.max_timers;
    n->event_fn = ef;
    n->command_fn = cf;
    n->opaque = opaque;
    n->next_sequence = 1;
    n->next_command = 1;
    n->next_timer_order = 1;
    *out = n;
    return HDDVD_OK;
}
int hddvd_nav_create_with_config(const hddvd_config_limits *config, hddvd_event_fn ef,
                                 hddvd_command_fn cf, void *opaque, hddvd_nav **out)
{
    hddvd_nav_limits limits;
    if (!hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    limits.max_events = config->max_queued_events;
    limits.max_timers = config->max_timers;
    return hddvd_nav_create(&limits, ef, cf, opaque, out);
}
void hddvd_nav_destroy(hddvd_nav *n)
{
    if (n)
    {
        free(n->events);
        free(n->timers);
        free(n);
    }
}
int hddvd_nav_post(hddvd_nav *n, const hddvd_event *event)
{
    size_t at;
    if (!n || !event)
        return HDDVD_EINVAL;
    if (n->event_count == n->event_cap)
        return HDDVD_ELIMIT;
    at = n->event_count;
    while (at && n->events[at - 1].when.ticks > event->when.ticks)
    {
        n->events[at] = n->events[at - 1];
        at--;
    }
    n->events[at] = *event;
    n->events[at].sequence = n->next_sequence++;
    n->event_count++;
    return HDDVD_OK;
}
int hddvd_nav_schedule_timer(hddvd_nav *n, uint64_t id, hddvd_time due)
{
    size_t i;
    if (!n || !id)
        return HDDVD_EINVAL;
    for (i = 0; i < n->timer_count; i++)
        if (n->timers[i].id == id)
        {
            n->timers[i].due = due;
            return HDDVD_OK;
        }
    if (n->timer_count == n->timer_cap)
        return HDDVD_ELIMIT;
    n->timers[n->timer_count].id = id;
    n->timers[n->timer_count].due = due;
    n->timers[n->timer_count].order = n->next_timer_order++;
    n->timer_count++;
    return HDDVD_OK;
}
int hddvd_nav_cancel_timer(hddvd_nav *n, uint64_t id)
{
    size_t i;
    if (!n || !id)
        return HDDVD_EINVAL;
    for (i = 0; i < n->timer_count; i++)
        if (n->timers[i].id == id)
        {
            memmove(n->timers + i, n->timers + i + 1,
                    (n->timer_count - i - 1) * sizeof(*n->timers));
            n->timer_count--;
            return HDDVD_OK;
        }
    return HDDVD_ENOTFOUND;
}
static int fire_due_timers(hddvd_nav *n)
{
    for (;;)
    {
        size_t i, best = (size_t)-1;
        hddvd_event e;
        for (i = 0; i < n->timer_count; i++)
            if (n->timers[i].due.ticks <= n->now.ticks &&
                (best == (size_t)-1 || n->timers[i].due.ticks < n->timers[best].due.ticks ||
                 (n->timers[i].due.ticks == n->timers[best].due.ticks &&
                  n->timers[i].order < n->timers[best].order)))
                best = i;
        if (best == (size_t)-1)
            return HDDVD_OK;
        if (n->event_count == n->event_cap)
            return HDDVD_ELIMIT;
        memset(&e, 0, sizeof(e));
        e.type = HDDVD_EVENT_TIMER;
        e.when = n->timers[best].due;
        if (e.when.ticks < n->floor.ticks)
            e.when = n->floor;
        e.data.timer.timer_id = n->timers[best].id;
        if (hddvd_nav_post(n, &e) != HDDVD_OK)
            return HDDVD_ELIMIT;
        memmove(n->timers + best, n->timers + best + 1,
                (n->timer_count - best - 1) * sizeof(*n->timers));
        n->timer_count--;
    }
}
int hddvd_nav_advance(hddvd_nav *n, hddvd_time now)
{
    if (!n || now.ticks < n->now.ticks)
        return HDDVD_EINVAL;
    n->now = now;
    return fire_due_timers(n);
}
int hddvd_nav_discontinuity(hddvd_nav *n, hddvd_time now)
{
    size_t i;
    hddvd_event e;
    if (!n)
        return HDDVD_EINVAL;
    if (n->event_count == n->event_cap)
        return HDDVD_ELIMIT;
    /* The queue is sorted, so restamping its later suffix keeps the order. */
    for (i = 0; i < n->event_count; i++)
        if (n->events[i].when.ticks > now.ticks)
            n->events[i].when = now;
    n->now = now;
    n->floor = now;
    memset(&e, 0, sizeof(e));
    e.type = HDDVD_EVENT_DISCONTINUITY;
    e.when = now;
    if (hddvd_nav_post(n, &e) != HDDVD_OK)
        return HDDVD_ELIMIT;
    return fire_due_timers(n);
}
size_t hddvd_nav_cancel_all_timers(hddvd_nav *n)
{
    size_t count;
    if (!n)
        return 0;
    count = n->timer_count;
    n->timer_count = 0;
    return count;
}
size_t hddvd_nav_pump(hddvd_nav *n, size_t maximum)
{
    size_t done = 0;
    if (!n)
        return 0;
    while (n->event_count && (!maximum || done < maximum) &&
           n->events[0].when.ticks <= n->now.ticks)
    {
        hddvd_event e = n->events[0];
        memmove(n->events, n->events + 1, (n->event_count - 1) * sizeof(*n->events));
        n->event_count--;
        if (n->event_fn)
            n->event_fn(n->opaque, &e);
        done++;
    }
    return done;
}
int hddvd_nav_command(hddvd_nav *n, const hddvd_command *command, uint64_t *id)
{
    hddvd_command c;
    if (!n || !command || !id)
        return HDDVD_EINVAL;
    c = *command;
    c.id = n->next_command++;
    *id = c.id;
    if (n->command_fn)
        n->command_fn(n->opaque, &c);
    return HDDVD_OK;
}
