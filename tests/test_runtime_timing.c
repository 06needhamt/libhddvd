/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Markup timing: par and cue intervals from offsets and conditions, fill,
   set and animate values and their precedence against script values,
   events from markup, use= of definitions, XPath variables, and the issues
   for timing the runtime cannot run. */

#define NS "http://www.dvdforum.org/2005/ihd"
#define OPEN                                                                                       \
    "<root xmlns='" NS "' xmlns:s='" NS "#style' xmlns:st='" NS "#state'><head><timing "           \
    "clock='page'>"
#define BODY                                                                                       \
    "</timing></head><body><div id='page'>"                                                        \
    "<button id='a' st:focused='true' s:x='10px' s:navRight='b'/>"                                 \
    "<button id='b' class='big round' s:x='20px' s:navLeft='a' s:backgroundFrame='0'/>"            \
    "<input id='field' st:value='start'/></div></body></root>"

#define SECOND 90000u

static void ok(hddvd_runtime *runtime, const char *source)
{
    int rc = hddvd_runtime_evaluate(runtime, "test.js", source, strlen(source));
    if (rc)
    {
        hddvd_runtime_diagnostic d;
        hddvd_runtime_get_diagnostic(runtime, &d);
        fprintf(stderr, "script failed (%d): %s\n%s\n", rc, d.message ? d.message : "", source);
    }
    assert(rc == HDDVD_OK);
}

struct page
{
    hddvd_runtime *runtime;
    hddvd_xmu *xmu;
};

static struct page open_page(const char *timing)
{
    struct page page;
    char xml[8192];
    int n = snprintf(xml, sizeof(xml), "%s%s%s", OPEN, timing, BODY);
    assert(n > 0 && (size_t)n < sizeof(xml));
    page.runtime = NULL;
    page.xmu = NULL;
    assert(hddvd_runtime_create(&page.runtime) == HDDVD_OK);
    ok(page.runtime, "function expect(c, what) { if (!c) throw new Error('expected ' + what); }"
                     "var log = [];");
    assert(hddvd_xmu_open(xml, (size_t)n, &page.xmu) == HDDVD_OK);
    assert(hddvd_runtime_load_markup(page.runtime, page.xmu) == HDDVD_OK);
    return page;
}

static void close_page(struct page *page)
{
    hddvd_runtime_destroy(page->runtime);
    hddvd_xmu_close(page->xmu);
}

static const char *style(const struct page *page, const char *id, const char *name)
{
    return hddvd_runtime_get_style(page->runtime, hddvd_xmu_find_id(page->xmu, id), name);
}

static const char *state(const struct page *page, const char *id, const char *name)
{
    return hddvd_runtime_get_state(page->runtime, hddvd_xmu_find_id(page->xmu, id), name);
}

static void at(struct page *page, uint64_t ticks)
{
    hddvd_time t;
    t.ticks = ticks;
    assert(hddvd_runtime_advance(page->runtime, t) == HDDVD_OK);
}

static int is(const char *value, const char *expected)
{
    if (!value || strcmp(value, expected))
    {
        fprintf(stderr, "got \"%s\", expected \"%s\"\n", value ? value : "(null)", expected);
        return 0;
    }
    return 1;
}

/* Offsets from the parent's begin, dur, fill and end offsets. */
static void offsets(void)
{
    struct page page = open_page(
        "<par><cue begin='1s' dur='1s' select=\"id('a')\"><set s:x='50px'/></cue>"
        "<cue begin='1s' dur='1s' fill='hold' select=\"id('b')\"><set s:x='60px'/></cue>"
        "<cue begin='500ms' end='2s' select=\"id('field')\"><set st:value='mid'/></cue></par>"
        "<par begin='3s' end='4s'><cue begin='30f' fill='hold' select=\"id('a')\">"
        "<set s:y='7px'/></cue></par>");
    uint64_t generation = hddvd_runtime_generation(page.runtime);
    assert(is(style(&page, "a", "x"), "10px"));
    at(&page, SECOND / 2);
    assert(is(state(&page, "field", "value"), "mid"));
    assert(is(style(&page, "a", "x"), "10px"));
    at(&page, SECOND);
    assert(is(style(&page, "a", "x"), "50px"));
    assert(is(style(&page, "b", "x"), "60px"));
    assert(hddvd_runtime_generation(page.runtime) > generation);
    {
        hddvd_xmu_style computed;
        const hddvd_xmu_styles *styles = hddvd_runtime_styles(page.runtime);
        assert(hddvd_xmu_get_computed_style(styles, hddvd_xmu_find_id(page.xmu, "a"), &computed) ==
               HDDVD_OK);
        assert(computed.x == 50);
    }
    at(&page, 2 * SECOND);
    assert(is(style(&page, "a", "x"), "10px"));
    assert(is(style(&page, "b", "x"), "60px"));
    assert(is(state(&page, "field", "value"), "start"));
    /* The second par starts at 3s; its cue 30 frames later, and the held
       value goes with the par at 4s. */
    at(&page, 3 * SECOND + SECOND / 4);
    assert(is(style(&page, "a", "y"), ""));
    at(&page, 3 * SECOND + SECOND * 3 / 4);
    assert(is(style(&page, "a", "y"), "7px"));
    at(&page, 4 * SECOND);
    assert(is(style(&page, "a", "y"), ""));
    assert(hddvd_runtime_issue_count(page.runtime) == 0);
    close_page(&page);
}

/* Conditions on focus and actioned state, defaultNode(), markup events with
   params, and focus set by timing. */
static void conditions(void)
{
    struct page page = open_page(
        "<par>"
        "<cue begin=\"id('b')[state:focused()=true()]\" "
        "end=\"defaultNode()[state:focused()=false()]\""
        " select=\"id('b')\"><set s:backgroundFrame='1'/></cue>"
        "<cue begin=\"class('round')[state:actioned()]\" select=\"id('b')\" dur='1f'>"
        "<event name='pressed'><param name='bt_id' value='Name'/><param name='dir_id' "
        "value='2'/></event></cue>"
        "<cue begin=\"//input[@id='field' and state:value()='go']\" dur='1f' "
        "select=\"id('a')\"><set st:focused='true'/></cue>"
        "<cue begin=\"class('big')[state:focused()]\" end=\"defaultNode()[not(state:focused())]\">"
        "<event name='got'/><set s:y='9px'/></cue>"
        "</par>");
    ok(page.runtime, "addEventListener('got', function (e) { log.push('got:' + e.target.id); });"
                     "addEventListener('pressed', function (e) {"
                     "  log.push(e.type + ':' + e.bt_id + ':' + e.dir_id + ':' + e.target.core.id);"
                     "}, false);");
    assert(is(style(&page, "b", "backgroundFrame"), "0"));
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_RIGHT, 1, NULL) == HDDVD_OK);
    assert(is(style(&page, "b", "backgroundFrame"), "1"));
    /* Without select, a cue acts on what its begin condition matched. */
    assert(is(style(&page, "b", "y"), "9px"));
    ok(page.runtime, "expect(log.join() == 'got:b', 'got ' + log.join()); log = [];");
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_ENTER, 1, NULL) == HDDVD_OK);
    ok(page.runtime, "expect(log.join() == 'pressed:Name:2:b', 'event ' + log.join()); log = [];");
    /* Holding Enter does not raise it again; a new press does. */
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_ENTER, 1, NULL) == HDDVD_OK);
    ok(page.runtime, "expect(log.length == 0, 'no repeat');");
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_ENTER, 0, NULL) == HDDVD_OK);
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_ENTER, 1, NULL) == HDDVD_OK);
    ok(page.runtime, "expect(log.length == 1, 'second press'); log = [];");
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_LEFT, 1, NULL) == HDDVD_OK);
    assert(is(style(&page, "b", "backgroundFrame"), "0"));
    assert(is(style(&page, "b", "y"), ""));
    /* A script value makes the third cue move the focus back to a. */
    assert(hddvd_runtime_key(page.runtime, HDDVD_KEY_RIGHT, 1, NULL) == HDDVD_OK);
    assert(hddvd_runtime_focus(page.runtime) == hddvd_xmu_find_id(page.xmu, "b"));
    ok(page.runtime, "log = []; document.field.state.value = 'go';");
    assert(hddvd_runtime_dispatch(page.runtime, "anything") == HDDVD_OK);
    assert(hddvd_runtime_focus(page.runtime) == hddvd_xmu_find_id(page.xmu, "a"));
    assert(hddvd_runtime_issue_count(page.runtime) == 0);
    close_page(&page);
}

/* Variables, use= of definitions, animate and the latest cue winning. */
static void variables_and_animation(void)
{
    struct page page =
        open_page("<defs><g id='glow'><set s:opacity='0.5'/><set st:value='glowing'/></g>"
                  "<animate id='slide' s:x='0px;100px'/>"
                  "<animate id='frames' calcMode='discrete' s:backgroundFrame='0;1;2;3'/></defs>"
                  "<par begin=\"id('page')[$Mode='on']\" end=\"id('page')[$Mode!='on']\">"
                  "<cue select=\"id('field')\" use='glow'/>"
                  "<cue begin='0s' dur='1s' select=\"id('a')\" use='slide' fill='hold'/>"
                  "<cue begin='0s' dur='1s' select=\"id('b')\" use='frames'/>"
                  "<cue begin='2s' select=\"id('a') | id('b')\"><set s:x='5px'/></cue>"
                  "</par>");
    assert(is(style(&page, "field", "opacity"), "1"));
    ok(page.runtime, "document.setXPathVariable('Mode', 'on');");
    assert(hddvd_runtime_dispatch(page.runtime, "x") == HDDVD_OK);
    assert(is(style(&page, "field", "opacity"), "0.5"));
    assert(is(state(&page, "field", "value"), "glowing"));
    assert(is(style(&page, "a", "x"), "0px"));
    at(&page, SECOND / 2);
    assert(is(style(&page, "a", "x"), "50px"));
    assert(is(style(&page, "b", "backgroundFrame"), "2"));
    at(&page, SECOND);
    assert(is(style(&page, "a", "x"), "100px"));
    assert(is(style(&page, "b", "backgroundFrame"), "0"));
    /* The cue at 2s began last, so its x wins over the held animation. */
    at(&page, 2 * SECOND);
    assert(is(style(&page, "a", "x"), "5px"));
    assert(is(style(&page, "b", "x"), "5px"));
    ok(page.runtime, "document.setXPathVariable('Mode', 'off');");
    at(&page, 3 * SECOND);
    assert(is(style(&page, "a", "x"), "10px"));
    assert(is(style(&page, "field", "opacity"), "1"));
    assert(is(state(&page, "field", "value"), "start"));
    /* On again: the par restarts and its children with it. Timing sees the
       variable at the next call that runs scripts. */
    ok(page.runtime, "document.setXPathVariable('Mode', 'on');");
    assert(hddvd_runtime_dispatch(page.runtime, "x") == HDDVD_OK);
    at(&page, 3 * SECOND + SECOND / 2);
    assert(is(style(&page, "a", "x"), "50px"));
    assert(hddvd_runtime_issue_count(page.runtime) == 0);
    close_page(&page);
}

/* Script values over timing values: style values until unset, state values
   until unsetProperty, which keeps the value under timing. */
static void precedence(void)
{
    struct page page = open_page("<par><cue begin='1s' end='2s' select=\"id('a')\">"
                                 "<set s:x='50px' st:value='timed'/></cue></par>");
    ok(page.runtime, "document.a.style.x = '30px'; document.a.state.value = 'script';");
    at(&page, SECOND);
    assert(is(style(&page, "a", "x"), "30px"));
    assert(is(state(&page, "a", "value"), "script"));
    ok(page.runtime, "document.a.style.unsetProperty('x'); document.a.state.unsetProperty('value');"
                     "expect(document.a.style.x == '50px', 'timing x');"
                     "expect(document.a.state.value == 'timed', 'timing value');");
    at(&page, 2 * SECOND);
    assert(is(style(&page, "a", "x"), "10px"));
    assert(is(state(&page, "a", "value"), "script"));
    close_page(&page);
}

static size_t timing_issues(const hddvd_runtime *runtime, const char *part)
{
    hddvd_runtime_issue issue;
    size_t i, count = 0;
    for (i = 0; i < hddvd_runtime_issue_count(runtime); i++)
    {
        assert(hddvd_runtime_get_issue(runtime, i, &issue) == HDDVD_OK);
        if (issue.kind == HDDVD_RUNTIME_ISSUE_TIMING && strstr(issue.message, part))
            count++;
    }
    return count;
}

/* What cannot run is reported once each, and the rest still runs. */
static void issues(void)
{
    struct page page =
        open_page("<par><cue begin=\"id('a')[\" select=\"id('a')\"><set s:x='1px'/></cue>"
                  "<cue begin=\"id('a')[lang('en')]\" select=\"id('a')\"><set s:x='2px'/></cue>"
                  "<cue select=\"id('a')\" use='missing'><set s:y='3px'/><blink/></cue>"
                  "<cue dur='soon' select=\"id('b')\"><set s:y='4px'/></cue>"
                  "<switch/><seq/></par>");
    at(&page, SECOND);
    assert(is(style(&page, "a", "x"), "10px"));
    assert(is(style(&page, "a", "y"), "3px"));
    assert(is(style(&page, "b", "y"), "4px"));
    assert(timing_issues(page.runtime, "id('a')[\"") == 1);
    assert(timing_issues(page.runtime, "unsupported function lang()") == 1);
    assert(timing_issues(page.runtime, "use=\"missing\"") == 1);
    assert(timing_issues(page.runtime, "<blink>") == 1);
    assert(timing_issues(page.runtime, "dur \"soon\"") == 1);
    assert(timing_issues(page.runtime, "<switch>") == 1);
    assert(timing_issues(page.runtime, "<seq> runs as <par>") == 1);
    close_page(&page);
}

/* A document without timing, and a new document replacing timing. */
static void documents(void)
{
    struct page page = open_page("<par><cue select=\"id('a')\"><set s:x='50px'/></cue></par>");
    static const char plain[] = "<root xmlns='" NS "' xmlns:s='" NS "#style'><body>"
                                "<div id='a' s:x='1px'/></body></root>";
    hddvd_xmu *other = NULL;
    assert(is(style(&page, "a", "x"), "50px"));
    assert(hddvd_xmu_open(plain, strlen(plain), &other) == HDDVD_OK);
    assert(hddvd_runtime_load_markup(page.runtime, other) == HDDVD_OK);
    at(&page, SECOND);
    assert(is(hddvd_runtime_get_style(page.runtime, hddvd_xmu_find_id(other, "a"), "x"), "1px"));
    assert(hddvd_runtime_load_markup(page.runtime, NULL) == HDDVD_OK);
    at(&page, 2 * SECOND);
    hddvd_xmu_close(other);
    close_page(&page);
}

int main(void)
{
    if (!hddvd_runtime_engine())
    {
        puts("no script engine; markup timing not tested");
        return 0;
    }
    offsets();
    conditions();
    variables_and_animation();
    precedence();
    issues();
    documents();
    return 0;
}
