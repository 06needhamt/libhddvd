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
    return 0;
}
