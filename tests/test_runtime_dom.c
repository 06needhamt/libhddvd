/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* HDi host objects: element lookup, state, style, listeners, focus and
   navigation keys, timers, animations, document changes and budgets. */

#define NS "http://www.dvdforum.org/2005/ihd"
#define MARKUP_OPEN "<root xmlns='" NS "' xmlns:s='" NS "#style' xmlns:st='" NS "#state'>"

/* Four buttons in a ring (a, b, c, d to the right), with a down/up pair
   a-c, a diagonal from d to b, two layered styles and an input. */
static const char markup[] =
    MARKUP_OPEN "<head><styling>"
                "<style id='base' s:x='10px' s:opacity='0.5'/><style id='late' s:x='20px'/>"
                "</styling></head><body>"
                "<div id='page' s:width='1920px' s:height='1080px'>"
                "<button id='a' style='base late' st:focused='true' s:navRight='b' s:navDown='c' "
                "s:y='5px'/>"
                "<button id='b' s:navRight='c' s:navLeft='a' s:display='none'/>"
                "<button id='c' s:navRight='d' s:navUp='a' st:enabled='false'/>"
                "<button id='d' s:navRight='a' s:navLeftUp='b'/>"
                "<input id='field' st:value='hello'/>"
                "</div></body></root>";

/* A second document that shares only the id 'b'. */
static const char other_markup[] =
    MARKUP_OPEN "<body><div id='b' s:x='7px'/><div id='e'/></body></root>";

/* 90 kHz ticks of n frames at 60000/1001 per second. */
#define FRAMES(n) ((uint64_t)(n) * 3003u / 2u)
#define SECOND 90000u

static int run(hddvd_runtime *runtime, const char *source)
{
    return hddvd_runtime_evaluate(runtime, "test.js", source, strlen(source));
}

static void ok(hddvd_runtime *runtime, const char *source)
{
    int rc = run(runtime, source);
    if (rc)
    {
        hddvd_runtime_diagnostic d;
        hddvd_runtime_get_diagnostic(runtime, &d);
        fprintf(stderr, "script failed (%d): %s\n%s\n", rc, d.message ? d.message : "", source);
    }
    assert(rc == HDDVD_OK);
}

static int has(const char *text, const char *part)
{
    return text && strstr(text, part) != NULL;
}

static hddvd_runtime *create(uint64_t operations)
{
    hddvd_runtime_limits limits;
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_limits_default(&limits);
    if (operations)
        limits.max_operations = operations;
    assert(hddvd_runtime_create_with_limits(&limits, &runtime) == HDDVD_OK);
    ok(runtime, "function expect(c, what) { if (!c) throw new Error('expected ' + what); }");
    return runtime;
}

static hddvd_xmu *open_xmu(const char *xml)
{
    hddvd_xmu *xmu = NULL;
    assert(hddvd_xmu_open(xml, strlen(xml), &xmu) == HDDVD_OK);
    return xmu;
}

static size_t node(const hddvd_xmu *xmu, const char *id)
{
    size_t index = hddvd_xmu_find_id(xmu, id);
    assert(index != SIZE_MAX);
    return index;
}

static int32_t computed_x(const hddvd_runtime *runtime, size_t index)
{
    hddvd_xmu_style style;
    assert(hddvd_xmu_get_computed_style(hddvd_runtime_styles(runtime), index, &style) == HDDVD_OK);
    return style.x;
}

static void key(hddvd_runtime *runtime, uint32_t code)
{
    assert(hddvd_runtime_key(runtime, code, 1, NULL) == HDDVD_OK);
    assert(hddvd_runtime_key(runtime, code, 0, NULL) == HDDVD_OK);
}

static void advance(hddvd_runtime *runtime, uint64_t ticks)
{
    hddvd_time t;
    t.ticks = ticks;
    assert(hddvd_runtime_advance(runtime, t) == HDDVD_OK);
}

static void arguments(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_runtime_issue issue;
    hddvd_time t;
    int prevented = 5;
    t.ticks = 0;
    assert(hddvd_runtime_load_markup(NULL, NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_markup_loaded(NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_styles(NULL) == NULL && hddvd_runtime_styles(runtime) == NULL);
    assert(hddvd_runtime_focus(NULL) == SIZE_MAX && hddvd_runtime_focus(runtime) == SIZE_MAX);
    assert(hddvd_runtime_get_state(runtime, 0, "focused") == NULL);
    assert(hddvd_runtime_get_style(runtime, 0, "x") == NULL);
    assert(hddvd_runtime_generation(NULL) == 0);
    assert(hddvd_runtime_key(NULL, HDDVD_KEY_UP, 1, &prevented) == HDDVD_EINVAL && !prevented);
    assert(hddvd_runtime_advance(NULL, t) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch(NULL, "x") == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch(runtime, NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_dispatch(runtime, "") == HDDVD_EINVAL);
    assert(hddvd_runtime_issue_count(NULL) == 0 && hddvd_runtime_issues_dropped(NULL) == 0);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_EINVAL);
    assert(hddvd_runtime_get_issue(NULL, 0, &issue) == HDDVD_EINVAL);
    hddvd_runtime_clear_issues(NULL);
    /* The host objects exist before any document; lookups find nothing. */
    ok(runtime, "expect(document.getElementById('a') === null, 'no element');"
                "expect(document.body === null, 'no body');"
                "expect(typeof application.createTimer === 'function', 'createTimer');"
                "expect(typeof TIMER_APPLICATION === 'number', 'TIMER_APPLICATION');"
                "expect(typeof Player === 'object', 'Player');");
    assert(hddvd_runtime_markup_loaded(runtime) == HDDVD_OK);
    assert(hddvd_runtime_load_markup(runtime, NULL) == HDDVD_OK);
    hddvd_runtime_destroy(runtime);
    {
        /* Installing the host objects does not use the operation or time
           budgets, so even the smallest ones leave a working runtime. */
        hddvd_runtime_limits limits;
        hddvd_runtime_limits_default(&limits);
        limits.max_operations = 1;
        limits.max_milliseconds = 1;
        assert(hddvd_runtime_create_with_limits(&limits, &runtime) == HDDVD_OK);
        assert(run(runtime, "var x = typeof document;") == HDDVD_OK);
        hddvd_runtime_destroy(runtime);
    }
}

static void lookup(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_xmu *xmu = open_xmu(markup);
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    ok(runtime,
       "var a = document.getElementById('a');"
       "expect(a.id === 'a' && a.localName === 'button', 'a');"
       "expect(document.getElementById('a') === a && document.a === a, 'one object per element');"
       "expect(document.getElementById('nope') === null, 'null for unknown ids');"
       "expect(document.body.localName === 'body', 'body');"
       "expect(a.parentNode.id === 'page' && document.page.firstChild === a, 'parent, child');"
       "expect(a.nextSibling.id === 'b' && document.field.nextSibling === null, 'siblings');"
       "expect(a.getAttribute('style') === 'base late', 'attribute');"
       "expect(a.getAttribute('missing') === null, 'missing attribute');"
       "expect(a.core.id === 'a' && document.page.core.id === 'page', 'core.id');"
       "expect(a.attributes.getNamedItem('style').value === 'base late', 'named item');"
       "expect(a.attributes.getNamedItem('missing') === null, 'no named item');"
       "expect(!document.field.hasChildNodes() && document.page.hasChildNodes(), 'children');"
       "expect(typeof document.getElementById === 'function', 'members win over ids');");
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
}

static void state(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_xmu *xmu = open_xmu(markup);
    uint64_t generation;
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    assert(hddvd_runtime_focus(runtime) == node(xmu, "a"));
    ok(runtime, "expect(document.a.state.focused === 'true', 'initial focus');"
                "expect(document.b.state.focused === 'false', 'b unfocused');"
                "expect(document.c.state.enabled === 'false', 'markup state');"
                "expect(document.b.state.enabled === 'true', 'enabled by default');"
                "expect(document.field.state.value === 'hello', 'markup value');"
                "expect(document.b.state.actioned === 'false', 'not actioned');");
    generation = hddvd_runtime_generation(runtime);
    ok(runtime, "document.c.state.focused = true;"
                "expect(document.a.state.focused === 'false', 'focus left a');");
    assert(hddvd_runtime_focus(runtime) == node(xmu, "c"));
    assert(hddvd_runtime_generation(runtime) > generation);
    ok(runtime, "document.c.state.setProperty('focused', 'false');");
    assert(hddvd_runtime_focus(runtime) == SIZE_MAX);
    ok(runtime, "document.field.state.value = 42;"
                "expect(document.field.state.getProperty('value') === '42', 'converted');"
                "document.c.state.enabled = 'yes';"
                "expect(document.c.state.enabled === 'false', 'only true is true');"
                "document.c.state.enabled = true;"
                "document.field.state.unsetProperty('value');"
                "expect(document.field.state.value === '42', 'unset keeps the value');"
                "var threw = 0;"
                "try { document.a.state.getProperty('bogus'); } catch (e) { threw++; }"
                "try { document.a.state.unsetProperty('bogus'); } catch (e) { threw++; }"
                "try { document.a.state.setProperty('bogus', 1); } catch (e) { threw++; }"
                "expect(threw === 3, 'unknown state names throw');");
    assert(!strcmp(hddvd_runtime_get_state(runtime, node(xmu, "field"), "value"), "42"));
    assert(!strcmp(hddvd_runtime_get_state(runtime, node(xmu, "c"), "enabled"), "true"));
    assert(hddvd_runtime_get_state(runtime, node(xmu, "c"), "bogus") == NULL);
    assert(hddvd_runtime_get_state(runtime, 9999, "value") == NULL);
    assert(hddvd_runtime_get_state(runtime, 0, NULL) == NULL);
    /* Setting a value it already has changes nothing. */
    generation = hddvd_runtime_generation(runtime);
    ok(runtime, "document.field.state.value = '42'; document.b.state.enabled = 'true';");
    assert(hddvd_runtime_generation(runtime) == generation);
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
}

static void style(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_xmu *xmu = open_xmu(markup);
    hddvd_runtime_issue issue;
    hddvd_xmu_style computed;
    size_t a = node(xmu, "a"), b = node(xmu, "b"), d = node(xmu, "d");
    uint64_t generation;
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    ok(runtime, "expect(document.a.style.x === '20px', 'the later style reference wins');"
                "expect(document.a.style.y === '5px', 'own declaration');"
                "expect(document.a.style.opacity === '0.5', 'referenced');"
                "expect(document.b.style.display === 'none', 'display');"
                "expect(document.c.style.display === 'auto', 'initial display');"
                "expect(document.c.style.visibility === 'visible', 'initial visibility');"
                "expect(document.c.style.opacity === '1', 'initial opacity');"
                "expect(document.c.style.width === '', 'undeclared');");
    assert(computed_x(runtime, a) == 20);
    generation = hddvd_runtime_generation(runtime);
    ok(runtime, "document.a.style.x = '100px';"
                "expect(document.a.style.getProperty('x') === '100px', 'script value');");
    assert(computed_x(runtime, a) == 100);
    assert(!strcmp(hddvd_runtime_get_style(runtime, a, "x"), "100px"));
    assert(hddvd_runtime_generation(runtime) > generation);
    assert(hddvd_runtime_issue_count(runtime) == 0);
    /* A malformed value reads back but leaves the model as markup had it. */
    ok(runtime, "document.a.style.setProperty('x', 'wide');"
                "expect(document.a.style.x === 'wide', 'kept');");
    assert(computed_x(runtime, a) == 20);
    assert(hddvd_runtime_issue_count(runtime) == 1);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_OK);
    assert(issue.kind == HDDVD_RUNTIME_ISSUE_STYLE && issue.node == a);
    assert(has(issue.message, "malformed") && has(issue.message, "wide"));
    ok(runtime, "document.a.style.setProperty('glow', 'on');"
                "expect(document.a.style.getProperty('glow') === 'on', 'unmodeled kept');");
    assert(hddvd_runtime_get_issue(runtime, 1, &issue) == HDDVD_OK);
    assert(has(issue.message, "unmodeled") && has(issue.message, "glow"));
    hddvd_runtime_clear_issues(runtime);
    assert(hddvd_runtime_issue_count(runtime) == 0);
    ok(runtime, "document.a.style.unsetProperty('x');"
                "expect(document.a.style.x === '20px', 'back to markup');"
                "document.a.style.x = '30px'; document.a.style.x = '';"
                "expect(document.a.style.x === '20px', 'an empty value unsets');");
    assert(computed_x(runtime, a) == 20);
    assert(hddvd_runtime_issue_count(runtime) == 0);
    /* Display, z-order and navigation follow script values. */
    ok(runtime, "document.b.style.display = 'auto';"
                "document.d.style.zIndex = '-1';"
                "document.a.style.navRight = 'd';");
    assert(hddvd_xmu_get_computed_style(hddvd_runtime_styles(runtime), b, &computed) == 0);
    assert(computed.display == HDDVD_XMU_DISPLAY_AUTO);
    assert(hddvd_xmu_style_z_order(hddvd_runtime_styles(runtime), 0) == d);
    assert(hddvd_xmu_get_computed_style(hddvd_runtime_styles(runtime), a, &computed) == 0);
    assert(computed.nav_right == d);
    key(runtime, HDDVD_KEY_RIGHT);
    assert(hddvd_runtime_focus(runtime) == d);
    ok(runtime, "document.d.style.unsetProperty('zIndex');");
    assert(hddvd_xmu_style_z_order(hddvd_runtime_styles(runtime), 0) != d);
    ok(runtime, "var threw = false;"
                "try { document.a.style.setProperty('', 'x'); } catch (e) { threw = true; }"
                "expect(threw, 'an empty name throws');");
    assert(hddvd_runtime_get_style(runtime, a, "") == NULL);
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
}

static void listeners(void)
{
    hddvd_runtime *runtime = create(0);
    ok(runtime, "var log = [];"
                "function first(e) { log.push('first:' + e.type); }"
                "function second(e) { log.push('second'); }"
                "var handler = { handleEvent: function (e) { log.push('object'); } };"
                "addEventListener('ping', first, true);"
                "application.addEventListener('ping', second, false);"
                "application.addEventListener('ping', first, true);"
                "application.addEventListener('ping', handler, false);"
                "application.addEventListener('ping', first, false);");
    assert(hddvd_runtime_dispatch(runtime, "ping") == HDDVD_OK);
    assert(hddvd_runtime_dispatch(runtime, "other") == HDDVD_OK);
    ok(runtime, "expect(log.join() === 'first:ping,second,object,first:ping', log.join());");
    /* Removed during a dispatch: skipped; added during one: next time. */
    ok(runtime, "log = [];"
                "removeEventListener('ping', first, true);"
                "removeEventListener('ping', second, false);"
                "application.removeEventListener('ping', handler, false);"
                "removeEventListener('ping', first, false);"
                "function victim() { log.push('victim'); }"
                "function late() { log.push('late'); }"
                "addEventListener('ping', function () {"
                "  application.removeEventListener('ping', victim, false);"
                "  application.addEventListener('ping', late, false); }, false);"
                "addEventListener('ping', victim, false);");
    assert(hddvd_runtime_dispatch(runtime, "ping") == HDDVD_OK);
    ok(runtime, "expect(log.join() === '', log.join());");
    assert(hddvd_runtime_dispatch(runtime, "ping") == HDDVD_OK);
    ok(runtime, "expect(log.join() === 'late', log.join());");
    /* Script events carry their own fields and report preventDefault. */
    ok(runtime, "var seen;"
                "application.addEventListener('custom', function (e) {"
                "  seen = e; e.preventDefault(); e.stopPropagation(); }, false);"
                "var e = document.createEvent('Events');"
                "e.initEvent('custom', true, true); e.payload = 7;"
                "expect(dispatchEvent(e) === false, 'prevented');"
                "expect(seen === e && seen.payload === 7 && e.defaultPrevented, 'same event');"
                "expect(e.target === application && e.currentTarget === null, 'targets');"
                "var plain = createEvent('x'); plain.initEvent('custom', false, false);"
                "expect(application.dispatchEvent(plain) === true, 'not cancelable');"
                "var threw = 0;"
                "try { dispatchEvent(createEvent()); } catch (x) { threw++; }"
                "try { dispatchEvent(null); } catch (x) { threw++; }"
                "try { addEventListener('x', 5); } catch (x) { threw++; }"
                "removeEventListener('never', first, false);"
                "expect(threw === 3, 'bad dispatches and listeners throw');"
                "var typed = 0;"
                "addEventListener('dismiss', function (e) { typed++; }, false);"
                "dispatchEvent(document.createEvent('dismiss'));"
                "expect(typed === 1, 'createEvent(type) sets the type');");
    hddvd_runtime_destroy(runtime);
}

static void exceptions(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_runtime_diagnostic d;
    hddvd_runtime_issue issue;
    size_t i;
    ok(runtime, "var log = [];"
                "addEventListener('boom', function () { log.push(1); }, false);"
                "addEventListener('boom', function () { throw new Error('bad'); }, false);"
                "addEventListener('boom', function () {"
                "  throw { toString: function () { throw 1; } }; }, false);"
                "addEventListener('boom', function () { log.push(2); }, false);");
    assert(hddvd_runtime_dispatch(runtime, "boom") == HDDVD_ESCRIPT);
    assert(hddvd_runtime_get_diagnostic(runtime, &d) == HDDVD_OK);
    assert(d.status == HDDVD_ESCRIPT && has(d.message, "Error: bad"));
    ok(runtime, "expect(log.join() === '1,2', 'the rest still ran');");
    assert(hddvd_runtime_issue_count(runtime) == 2);
    assert(hddvd_runtime_get_issue(runtime, 0, &issue) == HDDVD_OK);
    assert(issue.kind == HDDVD_RUNTIME_ISSUE_EXCEPTION && issue.node == SIZE_MAX);
    assert(has(issue.message, "\"boom\" listener threw Error: bad (at "));
    assert(has(issue.message, "test.js:"));
    assert(hddvd_runtime_get_issue(runtime, 1, &issue) == HDDVD_OK);
    assert(has(issue.message, "cannot be converted"));
    /* Timers and the markup loaded handler report the same way. */
    ok(runtime, "var fired = [];"
                "var t1 = createTimer('00:00:00:01', 1, function () { throw 'timer'; });"
                "var t2 = createTimer('00:00:00:01', 1, function () { fired.push(2); });"
                "t1.enabled = t2.enabled = true;"
                "setMarkupLoadedHandler(function () { throw new TypeError('loaded'); });");
    advance(runtime, 0);
    {
        hddvd_time t;
        t.ticks = FRAMES(1);
        assert(hddvd_runtime_advance(runtime, t) == HDDVD_ESCRIPT);
    }
    ok(runtime, "expect(fired.join() === '2', 'the other timer fired');");
    assert(hddvd_runtime_markup_loaded(runtime) == HDDVD_ESCRIPT);
    assert(hddvd_runtime_get_diagnostic(runtime, &d) == HDDVD_OK && has(d.message, "loaded"));
    assert(hddvd_runtime_issue_count(runtime) == 4);
    hddvd_runtime_clear_issues(runtime);
    /* 256 issues are kept and the rest counted. */
    ok(runtime, "for (var i = 0; i < 300; i++)"
                "  addEventListener('many', function () { throw 0; }, false);");
    assert(hddvd_runtime_dispatch(runtime, "many") == HDDVD_ESCRIPT);
    assert(hddvd_runtime_issue_count(runtime) == 256);
    assert(hddvd_runtime_issues_dropped(runtime) == 44);
    for (i = 0; i < 256; i++)
        assert(hddvd_runtime_get_issue(runtime, i, &issue) == HDDVD_OK && issue.message);
    hddvd_runtime_clear_issues(runtime);
    assert(hddvd_runtime_issues_dropped(runtime) == 0);
    ok(runtime, "setMarkupLoadedHandler(null);"
                "var threw = false;"
                "try { setMarkupLoadedHandler(3); } catch (e) { threw = true; }"
                "expect(threw, 'a handler must be a function');");
    assert(hddvd_runtime_markup_loaded(runtime) == HDDVD_OK);
    hddvd_runtime_destroy(runtime);
}

static void focus_and_keys(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_xmu *xmu = open_xmu(markup);
    size_t a = node(xmu, "a"), b = node(xmu, "b"), c = node(xmu, "c"), d = node(xmu, "d");
    int prevented = 5;
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    /* Around the ring and back. */
    key(runtime, HDDVD_KEY_RIGHT);
    assert(hddvd_runtime_focus(runtime) == b);
    key(runtime, HDDVD_KEY_RIGHT);
    key(runtime, HDDVD_KEY_RIGHT);
    assert(hddvd_runtime_focus(runtime) == d);
    key(runtime, HDDVD_KEY_UP_LEFT);
    assert(hddvd_runtime_focus(runtime) == b);
    key(runtime, HDDVD_KEY_LEFT);
    assert(hddvd_runtime_focus(runtime) == a);
    key(runtime, HDDVD_KEY_LEFT);
    assert(hddvd_runtime_focus(runtime) == a);
    key(runtime, HDDVD_KEY_DOWN);
    assert(hddvd_runtime_focus(runtime) == c);
    key(runtime, HDDVD_KEY_UP);
    assert(hddvd_runtime_focus(runtime) == a);
    /* Listeners see every key; preventDefault stops the default, and
       stopPropagation alone does not. */
    ok(runtime, "var keys = [];"
                "function block(e) {"
                "  keys.push(e.type + ':' + e.key + ':' + e.target.core.id);"
                "  if (e.key === 0x27) e.preventDefault(); else e.stopPropagation(); }"
                "addEventListener('controller_key_down', block, true);"
                "addEventListener('controller_key_up', block, true);");
    assert(hddvd_runtime_key(runtime, HDDVD_KEY_RIGHT, 1, &prevented) == HDDVD_OK && prevented);
    assert(hddvd_runtime_focus(runtime) == a);
    assert(hddvd_runtime_key(runtime, HDDVD_KEY_DOWN, 1, &prevented) == HDDVD_OK && !prevented);
    assert(hddvd_runtime_focus(runtime) == c);
    assert(hddvd_runtime_key(runtime, 0xfa, 0, NULL) == HDDVD_OK);
    /* The focused element is each event's target. */
    ok(runtime, "expect(keys.join() === 'controller_key_down:39:a,controller_key_down:40:a,"
                "controller_key_up:250:c', keys.join());"
                "removeEventListener('controller_key_down', block, true);");
    /* Enter actions the focused element while held. */
    assert(hddvd_runtime_key(runtime, HDDVD_KEY_ENTER, 1, NULL) == HDDVD_OK);
    assert(!strcmp(hddvd_runtime_get_state(runtime, c, "actioned"), "true"));
    assert(hddvd_runtime_key(runtime, HDDVD_KEY_ENTER, 0, NULL) == HDDVD_OK);
    assert(!strcmp(hddvd_runtime_get_state(runtime, c, "actioned"), "false"));
    /* Without a focus, keys do nothing by default and target the
       application. */
    ok(runtime, "document.c.state.focused = false;"
                "removeEventListener('controller_key_up', block, true);"
                "var target;"
                "addEventListener('controller_key_down', function (e) { target = e.target; },"
                "  false);");
    key(runtime, HDDVD_KEY_RIGHT);
    key(runtime, HDDVD_KEY_ENTER);
    assert(hddvd_runtime_focus(runtime) == SIZE_MAX);
    ok(runtime, "expect(target === application, 'application target');");
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
}

static void timers(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_time t;
    uint64_t start = 2u * SECOND;
    ok(runtime, "var log = [];"
                "function mark(name) { return function () { log.push(name); }; }"
                "var every = createTimer('00:00:01:00', TIMER_APPLICATION, mark('every'));"
                "expect(every.enabled === false && every.autoReset === true, 'defaults');");
    advance(runtime, start);
    ok(runtime, "expect(log.length === 0, 'disabled timers wait');"
                "every.enabled = true; every.enabled = true;");
    advance(runtime, start + FRAMES(60) - 1);
    ok(runtime, "expect(log.length === 0, 'not yet');");
    advance(runtime, start + FRAMES(60));
    ok(runtime, "expect(log.join() === 'every', 'fired once');");
    /* A repeating timer that fell behind fires once. */
    advance(runtime, start + 10u * FRAMES(60));
    ok(runtime, "expect(log.join() === 'every,every', log.join());"
                "every.enabled = false; log = [];");
    start += 10u * FRAMES(60);
    /* Equal due times fire in the order the timers were enabled; a one-shot
       timer disables itself; a timer enabled by a callback waits. */
    ok(runtime, "var c = createTimer('00:00:00:30', 1, mark('c'));"
                "var a = createTimer('00:00:00:30', 1, function () {"
                "  log.push('a'); var n = createTimer('00:00:00:01', 1, mark('new'));"
                "  n.autoReset = false; n.enabled = true; });"
                "var b = createTimer('00:00:00:30', 1, mark('b'));"
                "var early = createTimer('00:00:00:10', 1, mark('early'));"
                "var gone = createTimer('00:00:00:20', 1, mark('gone'));"
                "c.autoReset = a.autoReset = b.autoReset = early.autoReset = false;"
                "c.enabled = true; a.enabled = true; b.enabled = true; early.enabled = true;"
                "gone.enabled = true; gone.enabled = false;");
    advance(runtime, start + FRAMES(30));
    ok(runtime, "expect(log.join() === 'early,c,a,b', log.join());"
                "expect(!a.enabled && !early.enabled, 'one-shot timers disable');");
    advance(runtime, start + FRAMES(31));
    ok(runtime, "expect(log.join() === 'early,c,a,b,new', log.join());");
    /* Time only moves forward and must stay exact as a number. */
    t.ticks = start;
    assert(hddvd_runtime_advance(runtime, t) == HDDVD_EINVAL);
    t.ticks = (UINT64_C(1) << 53) + 1u;
    assert(hddvd_runtime_advance(runtime, t) == HDDVD_EINVAL);
    ok(runtime, "var threw = 0, bad = ['1:2', '00:00:00:00', '00:61:00:00', '00:00:00:60', 5];"
                "for (var i = 0; i < bad.length; i++)"
                "  try { createTimer(bad[i], 1, mark('x')); } catch (e) {"
                "    if (e instanceof RangeError) threw++; }"
                "try { createTimer('00:00:01:00', 1, 'notAFunction'); } catch (e) {"
                "  if (e instanceof TypeError) threw++; }"
                "expect(threw === 6, 'invalid timers throw');");
    hddvd_runtime_destroy(runtime);
}

static void animations(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_xmu *xmu = open_xmu(markup);
    size_t a = node(xmu, "a");
    uint64_t start = SECOND;
    hddvd_xmu_style computed;
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    advance(runtime, start);
    ok(runtime, "document.a.style.animateProperty('x', '0px;100px;0px', 2);"
                "document.b.style.animateProperty('display', 'none;auto', 1);"
                "document.c.style.animateProperty('opacity', '0;1', 1);"
                "document.d.style.animateProperty('y', '0px;50px', 1);"
                "document.d.style.y = '5px';"
                "expect(document.a.style.x === '0px', 'first value at once');");
    advance(runtime, start + SECOND / 4u);
    ok(runtime, "expect(document.c.style.opacity === '0.25', document.c.style.opacity);"
                "expect(document.a.style.x === '25px', document.a.style.x);"
                "expect(document.b.style.display === 'none', 'held until the next key');");
    advance(runtime, start + SECOND);
    ok(runtime, "expect(document.a.style.x === '100px', document.a.style.x);"
                "expect(document.b.style.display === 'auto', 'last value at the end');"
                "expect(document.d.style.y === '5px', 'a script value stops an animation');");
    assert(computed_x(runtime, a) == 100);
    advance(runtime, start + SECOND + SECOND / 2u);
    assert(computed_x(runtime, a) == 50);
    advance(runtime, start + 5u * SECOND);
    assert(computed_x(runtime, a) == 0);
    assert(hddvd_xmu_get_computed_style(hddvd_runtime_styles(runtime), node(xmu, "b"), &computed) ==
           0);
    assert(computed.display == HDDVD_XMU_DISPLAY_AUTO);
    ok(runtime, "var threw = 0;"
                "try { document.a.style.animateProperty('x', '1px;2px', 0); } catch (e) {"
                "  if (e instanceof RangeError) threw++; }"
                "try { document.a.style.animateProperty('x', '1px;2px', 'soon'); } catch (e) {"
                "  if (e instanceof RangeError) threw++; }"
                "expect(threw === 2, 'durations must be positive seconds');");
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
}

static void documents(void)
{
    hddvd_runtime *runtime = create(0);
    hddvd_xmu *xmu = open_xmu(markup), *other = open_xmu(other_markup), *bad;
    static const char broken[] = MARKUP_OPEN "<body><div s:x='wide'/></body></root>";
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    ok(runtime, "var old = document.a, oldB = document.b, heard = 0;"
                "old.style.animateProperty('x', '0px;100px', 1);"
                "addEventListener('still', function () { heard++; }, false);");
    assert(hddvd_runtime_load_markup(runtime, other) == HDDVD_OK);
    assert(hddvd_runtime_focus(runtime) == SIZE_MAX);
    advance(runtime, SECOND);
    assert(hddvd_runtime_issue_count(runtime) == 0);
    assert(hddvd_runtime_dispatch(runtime, "still") == HDDVD_OK);
    ok(runtime, "var threw = 0;"
                "try { old.style.x; } catch (e) { if (/no longer/.test(e.message)) threw++; }"
                "try { old.state.focused = true; } catch (e) { threw++; }"
                "try { old.parentNode; } catch (e) { threw++; }"
                "expect(threw === 3, 'elements of the old document throw');"
                "expect(document.a === undefined && document.e.id === 'e', 'new ids');"
                "expect(document.b !== oldB && document.b.style.x === '7px', 'new b');"
                "expect(heard === 1, 'application listeners stay');");
    /* A document whose styles do not parse is refused; the current stays. */
    bad = open_xmu(broken);
    assert(hddvd_runtime_load_markup(runtime, bad) == HDDVD_EPARSE);
    ok(runtime, "expect(document.e.id === 'e', 'still the second document');");
    assert(hddvd_runtime_load_markup(runtime, NULL) == HDDVD_OK);
    assert(hddvd_runtime_styles(runtime) == NULL);
    ok(runtime, "expect(document.getElementById('e') === null && document.e === undefined,"
                "  'detached');");
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(bad);
    hddvd_xmu_close(other);
    hddvd_xmu_close(xmu);
}

static void budgets(void)
{
    hddvd_runtime *runtime = create(2000000);
    hddvd_xmu *xmu = open_xmu(markup);
    hddvd_runtime_diagnostic d;
    hddvd_time t;
    assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_OK);
    ok(runtime, "var spin = true, runs = 0;"
                "addEventListener('controller_key_down', function () {"
                "  while (spin) {} }, false);"
                "var once = createTimer('00:00:00:01', 1, function () { runs++; while (true) {} });"
                "once.autoReset = false; once.enabled = true;"
                "function deep() { return deep() + 1; }"
                "addEventListener('deep', function () { deep(); }, false);");
    /* A budget stops the whole dispatch, default action included. */
    assert(hddvd_runtime_key(runtime, HDDVD_KEY_RIGHT, 1, NULL) == HDDVD_ELIMIT);
    assert(hddvd_runtime_get_diagnostic(runtime, &d) == HDDVD_OK);
    assert(d.limit == HDDVD_RUNTIME_LIMIT_OPERATIONS);
    assert(hddvd_runtime_focus(runtime) == node(xmu, "a"));
    ok(runtime, "spin = false;");
    key(runtime, HDDVD_KEY_RIGHT);
    assert(hddvd_runtime_focus(runtime) == node(xmu, "b"));
    /* A timer that ran out of budget is not run again. */
    t.ticks = FRAMES(1);
    assert(hddvd_runtime_advance(runtime, t) == HDDVD_ELIMIT);
    advance(runtime, SECOND);
    ok(runtime, "expect(runs === 1, 'the timer ran once');");
    /* Running out of recursion is an exception in that listener. */
    assert(hddvd_runtime_dispatch(runtime, "deep") == HDDVD_ESCRIPT);
    assert(hddvd_runtime_get_diagnostic(runtime, &d) == HDDVD_OK);
    assert(has(d.message, "stack overflow"));
    assert(hddvd_runtime_dispatch(runtime, "nothing") == HDDVD_OK);
    hddvd_runtime_destroy(runtime);
    hddvd_xmu_close(xmu);
}

/* The same inputs give the same results in two runtimes, read back
   through a state value. */
static void replay(void)
{
    static const char script[] =
        "var trace = [];"
        "addEventListener('controller_key_down', function (e) {"
        "  trace.push('k' + e.key + (document.a.state.focused === 'true' ? 'A' : ''));"
        "  var t = createTimer('00:00:00:05', 1, function () {"
        "    trace.push('t' + Math.floor(Math.random() * 1000));"
        "    document.field.state.value = trace.join(); });"
        "  t.autoReset = false; t.enabled = true; }, false);";
    static const uint32_t keys[] = {HDDVD_KEY_RIGHT, HDDVD_KEY_DOWN, HDDVD_KEY_LEFT, HDDVD_KEY_UP,
                                    HDDVD_KEY_RIGHT};
    hddvd_xmu *xmu = open_xmu(markup);
    hddvd_runtime *runtimes[2];
    size_t focus[2][5], r, i, field = node(xmu, "field");
    uint64_t generation[2];
    const char *trace[2];
    for (r = 0; r < 2; r++)
    {
        runtimes[r] = create(0);
        assert(hddvd_runtime_load_markup(runtimes[r], xmu) == HDDVD_OK);
        ok(runtimes[r], script);
        for (i = 0; i < 5; i++)
        {
            key(runtimes[r], keys[i]);
            advance(runtimes[r], (i + 1) * FRAMES(10));
            focus[r][i] = hddvd_runtime_focus(runtimes[r]);
        }
        generation[r] = hddvd_runtime_generation(runtimes[r]);
        trace[r] = hddvd_runtime_get_state(runtimes[r], field, "value");
    }
    assert(!memcmp(focus[0], focus[1], sizeof(focus[0])) && generation[0] == generation[1]);
    assert(trace[0] && trace[1] && !strcmp(trace[0], trace[1]));
    /* Keys and timers interleave: k, t, k, t ... with a's focus marked. */
    assert(!strncmp(trace[0], "k39A,t", 6));
    for (r = 0; r < 2; r++)
        hddvd_runtime_destroy(runtimes[r]);
    hddvd_xmu_close(xmu);
}

int main(void)
{
    if (!hddvd_runtime_engine())
    {
        hddvd_runtime *runtime = NULL;
        hddvd_xmu *xmu = open_xmu(markup);
        assert(hddvd_runtime_create(&runtime) == HDDVD_ENOTSUP);
        assert(hddvd_runtime_load_markup(runtime, xmu) == HDDVD_EINVAL);
        hddvd_xmu_close(xmu);
        puts("no script engine; host objects not tested");
        return 0;
    }
    arguments();
    lookup();
    state();
    style();
    listeners();
    exceptions();
    focus_and_keys();
    timers();
    animations();
    documents();
    budgets();
    replay();
    return 0;
}
