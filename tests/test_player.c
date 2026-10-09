#include "hddvd.h"
#include <assert.h>
#include <string.h>
struct state
{
    uint64_t timers[4];
    size_t n;
    uint64_t command;
    hddvd_command_result result;
};
static void event(void *p, const hddvd_event *e)
{
    struct state *s = p;
    if (e->type == HDDVD_EVENT_TIMER)
        s->timers[s->n++] = e->data.timer.timer_id;
    if (e->type == HDDVD_EVENT_COMMAND_ACK)
        s->result = e->data.acknowledgement.result;
}
static void command(void *p, const hddvd_command *c)
{
    ((struct state *)p)->command = c->id;
}

/* Records every delivered event so scenarios can compare exact traces. */
struct trace_entry
{
    hddvd_event_type type;
    uint64_t when;
    uint64_t id;
};
struct trace
{
    struct trace_entry entries[32];
    size_t count;
    hddvd_command commands[8];
    size_t command_count;
};
static void record_event(void *p, const hddvd_event *e)
{
    struct trace *t = p;
    struct trace_entry *entry;
    assert(t->count < sizeof(t->entries) / sizeof(t->entries[0]));
    entry = &t->entries[t->count++];
    entry->type = e->type;
    entry->when = e->when.ticks;
    entry->id = 0;
    if (e->type == HDDVD_EVENT_TIMER)
        entry->id = e->data.timer.timer_id;
    else if (e->type == HDDVD_EVENT_COMMAND_ACK)
        entry->id = e->data.acknowledgement.command_id;
}
static void record_command(void *p, const hddvd_command *c)
{
    struct trace *t = p;
    assert(t->command_count < sizeof(t->commands) / sizeof(t->commands[0]));
    t->commands[t->command_count++] = *c;
}
static void expect(const struct trace *t, size_t index, hddvd_event_type type, uint64_t when,
                   uint64_t id)
{
    assert(index < t->count);
    assert(t->entries[index].type == type);
    assert(t->entries[index].when == when);
    assert(t->entries[index].id == id);
}
static hddvd_nav *new_nav(struct trace *t)
{
    hddvd_nav *n = NULL;
    memset(t, 0, sizeof(*t));
    assert(hddvd_nav_create(NULL, record_event, record_command, t, &n) == HDDVD_OK);
    return n;
}
static void post(hddvd_nav *n, hddvd_event_type type, uint64_t when)
{
    hddvd_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.when.ticks = when;
    assert(hddvd_nav_post(n, &e) == HDDVD_OK);
}
static void advance(hddvd_nav *n, uint64_t when)
{
    assert(hddvd_nav_advance(n, (hddvd_time){when}) == HDDVD_OK);
}

/* Title-clock timers stop while paused: the host keeps title time still. */
static void test_pause(void)
{
    struct trace t;
    hddvd_nav *n = new_nav(&t);
    hddvd_event e;
    assert(hddvd_nav_schedule_timer(n, 7, (hddvd_time){100}) == HDDVD_OK);
    advance(n, 40);
    memset(&e, 0, sizeof(e));
    e.type = HDDVD_EVENT_PLAYBACK_STATE;
    e.when.ticks = 40;
    e.data.playback_state = HDDVD_PLAYBACK_PAUSED;
    assert(hddvd_nav_post(n, &e) == HDDVD_OK);
    advance(n, 40);
    assert(hddvd_nav_pump(n, 0) == 1);
    advance(n, 40);
    assert(hddvd_nav_pump(n, 0) == 0);
    e.data.playback_state = HDDVD_PLAYBACK_PLAYING;
    assert(hddvd_nav_post(n, &e) == HDDVD_OK);
    advance(n, 100);
    assert(hddvd_nav_pump(n, 0) == 2);
    expect(&t, 0, HDDVD_EVENT_PLAYBACK_STATE, 40, 0);
    expect(&t, 1, HDDVD_EVENT_PLAYBACK_STATE, 40, 0);
    expect(&t, 2, HDDVD_EVENT_TIMER, 100, 7);
    hddvd_nav_destroy(n);
}

/* Rates only change how fast the host moves title time, so timers fire at
   the same title time and in the same order at 2x and at 1/2 speed. */
static void test_rate_change(void)
{
    static const uint64_t steps[2][4] = {{60, 120, 180, 240}, {15, 30, 45, 240}};
    size_t run, i;
    for (run = 0; run < 2; run++)
    {
        struct trace t;
        hddvd_nav *n = new_nav(&t);
        hddvd_event e;
        assert(hddvd_nav_schedule_timer(n, 1, (hddvd_time){100}) == HDDVD_OK);
        assert(hddvd_nav_schedule_timer(n, 2, (hddvd_time){200}) == HDDVD_OK);
        memset(&e, 0, sizeof(e));
        e.type = HDDVD_EVENT_RATE_CHANGE;
        e.data.rate.numerator = run ? 1 : 2;
        e.data.rate.denominator = run ? 2 : 1;
        assert(hddvd_nav_post(n, &e) == HDDVD_OK);
        for (i = 0; i < 4; i++)
            advance(n, steps[run][i]);
        assert(hddvd_nav_pump(n, 0) == 3);
        expect(&t, 0, HDDVD_EVENT_RATE_CHANGE, 0, 0);
        expect(&t, 1, HDDVD_EVENT_TIMER, 100, 1);
        expect(&t, 2, HDDVD_EVENT_TIMER, 200, 2);
        hddvd_nav_destroy(n);
    }
}

/* A forward seek fires skipped timers after the discontinuity, stamped with
   the new time; later timers stay pending. */
static void test_seek_forward(void)
{
    struct trace t;
    hddvd_nav *n = new_nav(&t);
    assert(hddvd_nav_schedule_timer(n, 1, (hddvd_time){300}) == HDDVD_OK);
    assert(hddvd_nav_schedule_timer(n, 2, (hddvd_time){200}) == HDDVD_OK);
    assert(hddvd_nav_schedule_timer(n, 3, (hddvd_time){900}) == HDDVD_OK);
    advance(n, 100);
    post(n, HDDVD_EVENT_INPUT, 100);
    assert(hddvd_nav_discontinuity(n, (hddvd_time){500}) == HDDVD_OK);
    assert(hddvd_nav_pump(n, 0) == 4);
    expect(&t, 0, HDDVD_EVENT_INPUT, 100, 0);
    expect(&t, 1, HDDVD_EVENT_DISCONTINUITY, 500, 0);
    expect(&t, 2, HDDVD_EVENT_TIMER, 500, 2);
    expect(&t, 3, HDDVD_EVENT_TIMER, 500, 1);
    advance(n, 900);
    assert(hddvd_nav_pump(n, 0) == 1);
    expect(&t, 4, HDDVD_EVENT_TIMER, 900, 3);
    hddvd_nav_destroy(n);
}

/* A backward seek is allowed through discontinuity() only. Queued events
   stamped after the new time are restamped without reordering, and pending
   timers fire once title time reaches them again. */
static void test_seek_backward(void)
{
    struct trace t;
    hddvd_nav *n = new_nav(&t);
    assert(hddvd_nav_schedule_timer(n, 4, (hddvd_time){800}) == HDDVD_OK);
    advance(n, 600);
    post(n, HDDVD_EVENT_STREAM_SELECTION, 550);
    post(n, HDDVD_EVENT_INPUT, 600);
    post(n, HDDVD_EVENT_COMMAND_ACK, 700);
    assert(hddvd_nav_advance(n, (hddvd_time){200}) == HDDVD_EINVAL);
    assert(hddvd_nav_discontinuity(n, (hddvd_time){200}) == HDDVD_OK);
    advance(n, 200);
    assert(hddvd_nav_pump(n, 0) == 4);
    expect(&t, 0, HDDVD_EVENT_STREAM_SELECTION, 200, 0);
    expect(&t, 1, HDDVD_EVENT_INPUT, 200, 0);
    expect(&t, 2, HDDVD_EVENT_COMMAND_ACK, 200, 0);
    expect(&t, 3, HDDVD_EVENT_DISCONTINUITY, 200, 0);
    advance(n, 799);
    assert(hddvd_nav_pump(n, 0) == 0);
    advance(n, 800);
    assert(hddvd_nav_pump(n, 0) == 1);
    expect(&t, 4, HDDVD_EVENT_TIMER, 800, 4);
    hddvd_nav_destroy(n);
}

/* A stream discontinuity that keeps the same title time still reports the
   discontinuity before timers that are due at that time. */
static void test_discontinuity_in_place(void)
{
    struct trace t;
    hddvd_nav *n = new_nav(&t);
    assert(hddvd_nav_schedule_timer(n, 9, (hddvd_time){300}) == HDDVD_OK);
    advance(n, 299);
    assert(hddvd_nav_discontinuity(n, (hddvd_time){300}) == HDDVD_OK);
    assert(hddvd_nav_pump(n, 0) == 2);
    expect(&t, 0, HDDVD_EVENT_DISCONTINUITY, 300, 0);
    expect(&t, 1, HDDVD_EVENT_TIMER, 300, 9);
    hddvd_nav_destroy(n);
}

/* Title end posted at T precedes timers fired by the advance to T, and the
   title clock stops afterwards. */
static void test_title_end(void)
{
    struct trace t;
    hddvd_nav *n = new_nav(&t);
    assert(hddvd_nav_schedule_timer(n, 5, (hddvd_time){1000}) == HDDVD_OK);
    assert(hddvd_nav_schedule_timer(n, 6, (hddvd_time){1500}) == HDDVD_OK);
    post(n, HDDVD_EVENT_TITLE_END, 1000);
    advance(n, 1000);
    assert(hddvd_nav_pump(n, 0) == 2);
    expect(&t, 0, HDDVD_EVENT_TITLE_END, 1000, 0);
    expect(&t, 1, HDDVD_EVENT_TIMER, 1000, 5);
    advance(n, 1000);
    assert(hddvd_nav_pump(n, 0) == 0);
    hddvd_nav_destroy(n);
}

/* Closing a menu cancels its timers; reopening it may reuse timer ids and
   only the new timers fire. Commands get distinct ids across the cycle. */
static void test_menu_close_reopen(void)
{
    struct trace t;
    hddvd_nav *n = new_nav(&t);
    hddvd_command c;
    hddvd_event ack;
    uint64_t close_id, open_id;
    assert(hddvd_nav_schedule_timer(n, 1, (hddvd_time){400}) == HDDVD_OK);
    assert(hddvd_nav_schedule_timer(n, 2, (hddvd_time){450}) == HDDVD_OK);
    advance(n, 100);
    memset(&c, 0, sizeof(c));
    c.type = HDDVD_COMMAND_SET_MENU_VISIBLE;
    c.data.visible = 0;
    assert(hddvd_nav_command(n, &c, &close_id) == HDDVD_OK);
    assert(hddvd_nav_cancel_all_timers(n) == 2);
    assert(hddvd_nav_cancel_all_timers(n) == 0);
    assert(hddvd_nav_cancel_timer(n, 1) == HDDVD_ENOTFOUND);
    memset(&ack, 0, sizeof(ack));
    ack.type = HDDVD_EVENT_COMMAND_ACK;
    ack.when.ticks = 100;
    ack.data.acknowledgement.command_id = close_id;
    assert(hddvd_nav_post(n, &ack) == HDDVD_OK);
    advance(n, 450);
    assert(hddvd_nav_pump(n, 0) == 1);
    expect(&t, 0, HDDVD_EVENT_COMMAND_ACK, 100, close_id);

    c.data.visible = 1;
    assert(hddvd_nav_command(n, &c, &open_id) == HDDVD_OK && open_id != close_id);
    assert(t.command_count == 2 && t.commands[1].data.visible == 1);
    assert(hddvd_nav_schedule_timer(n, 1, (hddvd_time){600}) == HDDVD_OK);
    ack.when.ticks = 450;
    ack.data.acknowledgement.command_id = open_id;
    assert(hddvd_nav_post(n, &ack) == HDDVD_OK);
    advance(n, 600);
    assert(hddvd_nav_pump(n, 0) == 2);
    expect(&t, 1, HDDVD_EVENT_COMMAND_ACK, 450, open_id);
    expect(&t, 2, HDDVD_EVENT_TIMER, 600, 1);
    assert(hddvd_nav_cancel_all_timers(NULL) == 0);
    hddvd_nav_destroy(n);
}

/* A full queue rejects a discontinuity without changing any state. If timers
   cannot all be queued, the remainder fire on the next advance and are still
   never stamped before the discontinuity. */
static void test_discontinuity_limits(void)
{
    struct trace t;
    hddvd_nav *n = NULL;
    hddvd_nav_limits limits = {2, 4};
    memset(&t, 0, sizeof(t));
    assert(hddvd_nav_create(&limits, record_event, record_command, &t, &n) == HDDVD_OK);
    assert(hddvd_nav_discontinuity(NULL, (hddvd_time){0}) == HDDVD_EINVAL);
    post(n, HDDVD_EVENT_INPUT, 0);
    post(n, HDDVD_EVENT_INPUT, 0);
    assert(hddvd_nav_discontinuity(n, (hddvd_time){50}) == HDDVD_ELIMIT);
    assert(hddvd_nav_pump(n, 0) == 2);
    expect(&t, 1, HDDVD_EVENT_INPUT, 0, 0);

    assert(hddvd_nav_schedule_timer(n, 1, (hddvd_time){10}) == HDDVD_OK);
    assert(hddvd_nav_schedule_timer(n, 2, (hddvd_time){20}) == HDDVD_OK);
    assert(hddvd_nav_discontinuity(n, (hddvd_time){50}) == HDDVD_ELIMIT);
    assert(hddvd_nav_pump(n, 0) == 2);
    expect(&t, 2, HDDVD_EVENT_DISCONTINUITY, 50, 0);
    expect(&t, 3, HDDVD_EVENT_TIMER, 50, 1);
    advance(n, 50);
    assert(hddvd_nav_pump(n, 0) == 1);
    expect(&t, 4, HDDVD_EVENT_TIMER, 50, 2);
    hddvd_nav_destroy(n);
}

int main(void)
{
    hddvd_nav *n;
    hddvd_nav_limits l = {2, 2};
    struct state s;
    hddvd_time now = {20};
    hddvd_command c;
    hddvd_event e;
    uint64_t id;
    memset(&s, 0, sizeof(s));
    assert(hddvd_nav_create(&l, event, command, &s, &n) == 0);
    assert(hddvd_nav_schedule_timer(n, 2, (hddvd_time){10}) == 0);
    assert(hddvd_nav_schedule_timer(n, 1, (hddvd_time){10}) == 0);
    assert(hddvd_nav_advance(n, now) == 0);
    assert(hddvd_nav_pump(n, 0) == 2);
    assert(s.n == 2 && s.timers[0] == 2 && s.timers[1] == 1);
    memset(&c, 0, sizeof(c));
    c.type = HDDVD_COMMAND_PLAY;
    assert(hddvd_nav_command(n, &c, &id) == 0 && id == s.command && id != 0);
    memset(&e, 0, sizeof(e));
    e.type = HDDVD_EVENT_COMMAND_ACK;
    e.when.ticks = 30;
    e.data.acknowledgement.command_id = id;
    e.data.acknowledgement.result = HDDVD_COMMAND_REJECTED;
    assert(hddvd_nav_post(n, &e) == 0);
    assert(hddvd_nav_pump(n, 0) == 0);
    assert(hddvd_nav_advance(n, (hddvd_time){30}) == 0);
    assert(hddvd_nav_pump(n, 0) == 1 && s.result == HDDVD_COMMAND_REJECTED);
    assert(hddvd_nav_advance(n, (hddvd_time){19}) == HDDVD_EINVAL);
    hddvd_nav_destroy(n);
    test_pause();
    test_rate_change();
    test_seek_forward();
    test_seek_backward();
    test_discontinuity_in_place();
    test_title_end();
    test_menu_close_reopen();
    test_discontinuity_limits();
    return 0;
}
