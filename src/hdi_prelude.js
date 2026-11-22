/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
/* HDi host objects for libhddvd's script runtime (runtime_dom.c).

   Evaluated once per runtime before any disc script. Its value is a function
   that the engine calls with the runtime's native functions and the global
   object; it installs the host globals and returns the entry functions the
   runtime calls (key, advance, dispatch, markupChanged, markupLoaded). The
   natives and entries stay in this closure, out of the scripts' reach.

   Model choices that a conforming player has not confirmed are marked
   "unconfirmed"; docs/hdi-host-api.md lists them. */
(function (native, global) {
    'use strict';

    var TICKS_PER_SECOND = 90000;
    var ISSUE_EXCEPTION = 1;
    var STATE_NAMES = ['focused', 'enabled', 'actioned', 'pointer', 'value'];
    /* Every property the style model parses (style.c). */
    var STYLE_NAMES = [
        'anchor', 'backgroundColor', 'backgroundFrame', 'backgroundImage',
        'backgroundPositionHorizontal', 'backgroundPositionVertical', 'backgroundRepeat',
        'border', 'color', 'contentHeight', 'contentWidth', 'crop', 'display', 'displayAlign',
        'font', 'fontSize', 'fontStyle', 'height', 'navDown', 'navIndex', 'navLeft',
        'navLeftDown', 'navLeftUp', 'navRight', 'navRightDown', 'navRightUp', 'navUp', 'opacity',
        'padding', 'paddingAfter', 'paddingBefore', 'paddingEnd', 'paddingStart', 'position',
        'textAlign', 'textAltitude', 'visibility', 'width', 'wrapOption', 'x', 'y', 'zIndex'
    ];

    var listeners = [];
    var timers = [];
    var animations = [];
    var sequence = 0;
    var now = 0;
    var markupHandler = null;
    /* The element under the pointer (-1 for none), its document and
       whether the button is down on it. */
    var pointerNode = -1, pointerEpoch = 0, pointerDown = false;
    var wrappers = [];
    var idProperties = [];

    function hidden(object, name, value) {
        Object.defineProperty(object, name, {value: value});
    }

    /* The error, and where it was thrown when the engine records a stack. */
    function describe(e) {
        var text, frame;
        try {
            if (!(e instanceof Error))
                return String(e);
            text = String(e.name) + ': ' + String(e.message);
            frame = /^\s*(at .*)$/m.exec(typeof e.stack === 'string' ? e.stack : '');
            return frame ? text + ' (' + frame[1] + ')' : text;
        } catch (x) {
            return 'a value that cannot be converted to a string';
        }
    }

    function fault(where, e) {
        native.issue(ISSUE_EXCEPTION, where + ' threw ' + describe(e));
    }

    /* Events */

    function Event() {
        this.type = '';
        this.bubbles = false;
        this.cancelable = false;
        this.target = null;
        this.currentTarget = null;
        this.eventPhase = 0;
        this.defaultPrevented = false;
    }
    Event.prototype.initEvent = function (type, bubbles, cancelable) {
        this.type = String(type);
        this.bubbles = !!bubbles;
        this.cancelable = !!cancelable;
    };
    /* Every listener is on the one target, so there is nowhere further to
       propagate to. */
    Event.prototype.stopPropagation = function () {
        this.cancelBubble = true;
    };
    Event.prototype.preventDefault = function () {
        if (this.cancelable)
            this.defaultPrevented = true;
    };

    /* Discs pass the event type, and some dispatch the result without
       initEvent(). */
    function createEvent(type) {
        var event = new Event();
        if (type !== undefined)
            event.type = String(type);
        return event;
    }

    function checkListener(listener) {
        if (typeof listener !== 'function' && (typeof listener !== 'object' || listener === null))
            throw new TypeError('the listener is not a function or object');
    }

    function addEventListener(type, listener, capture) {
        var i;
        checkListener(listener);
        type = String(type);
        capture = !!capture;
        for (i = 0; i < listeners.length; i++)
            if (listeners[i].type === type && listeners[i].listener === listener &&
                listeners[i].capture === capture)
                return;
        listeners[listeners.length] = {type: type, listener: listener, capture: capture,
                                       removed: false};
    }

    function removeEventListener(type, listener, capture) {
        var i;
        type = String(type);
        capture = !!capture;
        for (i = 0; i < listeners.length; i++)
            if (listeners[i].type === type && listeners[i].listener === listener &&
                listeners[i].capture === capture) {
                listeners[i].removed = true;
                listeners.splice(i, 1);
                return;
            }
    }

    /* Runs the listeners registered when the dispatch starts, skipping any
       removed meanwhile. */
    function deliver(event, target) {
        var snapshot = listeners.slice(), i, entry;
        event.target = target || application;
        event.currentTarget = application;
        event.eventPhase = 2;
        for (i = 0; i < snapshot.length; i++) {
            entry = snapshot[i];
            if (entry.removed || entry.type !== event.type)
                continue;
            try {
                if (typeof entry.listener === 'function')
                    entry.listener.call(application, event);
                else
                    entry.listener.handleEvent(event);
            } catch (e) {
                fault('a "' + event.type + '" listener', e);
            }
        }
        event.currentTarget = null;
        event.eventPhase = 0;
        return !event.defaultPrevented;
    }

    function dispatchEvent(event) {
        if (typeof event !== 'object' || event === null || typeof event.type !== 'string' ||
            event.type === '')
            throw new TypeError('dispatchEvent needs an event with a type');
        return deliver(event);
    }

    /* Timers */

    /* "HH:MM:SS:FF", with frames at 60000/1001 per second as in playlists
       (unconfirmed for timers). */
    function timecodeTicks(text, who) {
        var m = /^(\d+):(\d\d):(\d\d):(\d\d)$/.exec(String(text)), frames, ticks;
        if (!m || +m[2] > 59 || +m[3] > 59 || +m[4] > 59)
            throw new RangeError(who + ' needs a time as HH:MM:SS:FF');
        frames = ((+m[1] * 60 + +m[2]) * 60 + +m[3]) * 60 + +m[4];
        ticks = Math.floor(frames * 3003 / 2);
        if (ticks > 9007199254740991)
            throw new RangeError(who + ' needs a smaller time');
        return ticks;
    }

    function parseTimecode(text) {
        var ticks = timecodeTicks(text, 'createTimer');
        if (!(ticks > 0))
            throw new RangeError('createTimer needs a time above zero');
        return ticks;
    }

    function formatTimecode(ticks) {
        var frames = Math.floor(ticks * 2 / 3003), parts, i, out = '';
        parts = [Math.floor(frames / 216000), Math.floor(frames / 3600) % 60,
                 Math.floor(frames / 60) % 60, frames % 60];
        for (i = 0; i < 4; i++)
            out += (i ? ':' : '') + (parts[i] < 10 ? '0' : '') + parts[i];
        return out;
    }

    function setEnabled(record, enabled) {
        var i;
        if (enabled === record.enabled)
            return;
        record.enabled = enabled;
        if (enabled) {
            record.due = now + record.ticks;
            record.order = ++sequence;
            timers[timers.length] = record;
            return;
        }
        for (i = 0; i < timers.length; i++)
            if (timers[i] === record) {
                timers.splice(i, 1);
                return;
            }
    }

    /* The clock argument is accepted and ignored: every timer runs on the
       time the host gives. */
    function createTimer(timecode, clock, callback) {
        var record = {ticks: parseTimecode(timecode), callback: callback, enabled: false,
                      autoReset: true, due: 0, order: 0};
        var timer = {};
        if (typeof callback !== 'function')
            throw new TypeError('createTimer needs a callback function');
        Object.defineProperty(timer, 'enabled', {
            enumerable: true,
            get: function () { return record.enabled; },
            set: function (value) { setEnabled(record, !!value); }
        });
        Object.defineProperty(timer, 'autoReset', {
            enumerable: true,
            get: function () { return record.autoReset; },
            set: function (value) { record.autoReset = !!value; }
        });
        return timer;
    }

    /* Each pass takes the earliest due timer and moves it past `now`, so a
       timer fires at most once per advance and the loop ends. */
    function runTimers() {
        var next, i, r;
        for (;;) {
            next = null;
            for (i = 0; i < timers.length; i++) {
                r = timers[i];
                if (r.due <= now && (next === null || r.due < next.due ||
                                     (r.due === next.due && r.order < next.order)))
                    next = r;
            }
            if (next === null)
                return;
            if (next.autoReset) {
                next.due = now + next.ticks;
                next.order = ++sequence;
            } else {
                setEnabled(next, false);
            }
            try {
                next.callback.call(global);
            } catch (e) {
                fault('a timer callback', e);
            }
        }
    }

    /* Elements */

    function Element(epoch, node) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_node', node);
        hidden(this, 'state', new State(epoch, node));
        hidden(this, 'style', new Style(epoch, node));
        hidden(this, 'core', new Core(epoch, node));
        hidden(this, 'attributes', new Attributes(epoch, node));
    }

    function element(epoch, node) {
        if (node < 0)
            return null;
        if (epoch !== native.epoch())
            throw new Error('the element is no longer in the document');
        if (!wrappers[node])
            wrappers[node] = new Element(epoch, node);
        return wrappers[node];
    }

    function relative(which) {
        return function () {
            return element(this._epoch, native.relative(this._epoch, this._node, which));
        };
    }

    Object.defineProperties(Element.prototype, {
        id: {get: function () { return native.id(this._epoch, this._node); }},
        localName: {get: function () { return native.name(this._epoch, this._node); }},
        nodeName: {get: function () { return native.name(this._epoch, this._node); }},
        parentNode: {get: relative(0)},
        firstChild: {get: relative(1)},
        nextSibling: {get: relative(2)}
    });
    Element.prototype.getAttribute = function (name) {
        return native.attribute(this._epoch, this._node, String(name));
    };

    /* element.core.id: discs read the id through the core attribute group;
       element.attributes.getNamedItem(name).value for any attribute. */
    function Attributes(epoch, node) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_node', node);
    }
    Attributes.prototype.getNamedItem = function (name) {
        var value = native.attribute(this._epoch, this._node, String(name));
        return value === null ? null : {name: String(name), value: value};
    };
    function Core(epoch, node) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_node', node);
    }
    Object.defineProperty(Core.prototype, 'id', {
        get: function () { return native.id(this._epoch, this._node); }
    });
    Element.prototype.hasChildNodes = function () {
        return native.relative(this._epoch, this._node, 1) >= 0;
    };

    function State(epoch, node) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_node', node);
    }
    State.prototype.getProperty = function (name) {
        return native.getState(this._epoch, this._node, String(name));
    };
    State.prototype.setProperty = function (name, value) {
        native.setState(this._epoch, this._node, String(name), String(value));
    };
    /* The script's value stays, but markup timing may change it again. */
    State.prototype.unsetProperty = function (name) {
        native.unsetState(this._epoch, this._node, String(name));
    };

    function Style(epoch, node) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_node', node);
    }

    function cancelAnimation(node, name) {
        var i;
        for (i = animations.length - 1; i >= 0; i--)
            if (animations[i].node === node && animations[i].name === name)
                animations.splice(i, 1);
    }

    Style.prototype.getProperty = function (name) {
        return native.getStyle(this._epoch, this._node, String(name));
    };
    Style.prototype.setProperty = function (name, value) {
        name = String(name);
        native.setStyle(this._epoch, this._node, name, String(value));
        cancelAnimation(this._node, name);
    };
    Style.prototype.unsetProperty = function (name) {
        name = String(name);
        native.unsetStyle(this._epoch, this._node, name);
        cancelAnimation(this._node, name);
    };
    /* Values step through "v1;v2;..." over the duration in seconds, evenly
       spaced (unconfirmed: markup may define other timing). */
    Style.prototype.animateProperty = function (name, values, duration) {
        var keys = String(values).split(';'), seconds = Number(duration);
        name = String(name);
        if (!(seconds > 0) || seconds > 86400)
            throw new RangeError('animateProperty needs a duration in seconds');
        native.setStyle(this._epoch, this._node, name, keys[0]);
        cancelAnimation(this._node, name);
        if (keys.length > 1)
            animations[animations.length] = {
                epoch: this._epoch, node: this._node, name: name, keys: keys, start: now,
                ticks: Math.max(1, Math.round(seconds * TICKS_PER_SECOND))
            };
    };

    function accessor(kind, name) {
        if (kind === 'state')
            return {
                enumerable: true,
                get: function () { return native.getState(this._epoch, this._node, name); },
                set: function (value) {
                    native.setState(this._epoch, this._node, name, String(value));
                }
            };
        return {
            enumerable: true,
            get: function () { return native.getStyle(this._epoch, this._node, name); },
            set: function (value) {
                native.setStyle(this._epoch, this._node, name, String(value));
                cancelAnimation(this._node, name);
            }
        };
    }
    (function () {
        var i;
        for (i = 0; i < STATE_NAMES.length; i++)
            Object.defineProperty(State.prototype, STATE_NAMES[i],
                                  accessor('state', STATE_NAMES[i]));
        for (i = 0; i < STYLE_NAMES.length; i++)
            Object.defineProperty(Style.prototype, STYLE_NAMES[i],
                                  accessor('style', STYLE_NAMES[i]));
    })();

    /* Numbers with the same unit interpolate; anything else holds the
       earlier value until the next keyframe. */
    var NUMBER = /^\s*(-?\d+(?:\.\d+)?)(px|%)?\s*$/;
    function interpolate(from, to, t) {
        var a = NUMBER.exec(from), b = NUMBER.exec(to), unit, value;
        if (!a || !b || (a[2] || '') !== (b[2] || ''))
            return from;
        unit = a[2] || '';
        value = +a[1] + (+b[1] - +a[1]) * t;
        value = unit === 'px' ? Math.round(value) : Math.round(value * 1000) / 1000;
        return String(value) + unit;
    }

    function stepAnimations() {
        var list = animations.slice(), i, a, position, k, value;
        for (i = 0; i < list.length; i++) {
            a = list[i];
            position = (now - a.start) / a.ticks * (a.keys.length - 1);
            if (position >= a.keys.length - 1) {
                value = a.keys[a.keys.length - 1];
                cancelAnimation(a.node, a.name);
            } else {
                k = Math.floor(position);
                value = interpolate(a.keys[k], a.keys[k + 1], position - k);
            }
            try {
                native.setStyle(a.epoch, a.node, a.name, value);
            } catch (e) {
                cancelAnimation(a.node, a.name);
                fault('an animation of ' + a.name, e);
            }
        }
    }

    /* Markup timing: the <timing> element's par and cue containers, their
       set, animate and event actions, and the XPath expressions their
       begin, end and select attributes use. The model follows SMIL as HDi
       markup uses it and is unconfirmed throughout (docs/hdi-host-api.md):
       - a container begins at an offset from its parent's begin, or when
         its begin condition becomes true (and again on each later rise);
       - it ends after dur, at an end offset from its parent's begin, when
         its end condition is true, or with its parent;
       - a cue's set and animate values apply to its selected elements (or,
         without select, to those its begin condition matched, else to its
         parent's default node) while it is active, and after it ends when fill="hold", until its parent
         ends; the cue that began last wins;
       - an event action dispatches its name, with its params as event
         fields and the cue's first selected element as target, when the
         cue begins.
       Timing values sit below script style values and held script state
       values. */

    var STYLE_NS = 'http://www.dvdforum.org/2005/ihd#style';
    var STATE_NS = 'http://www.dvdforum.org/2005/ihd#state';
    var ISSUE_TIMING = 5;
    /* Rounds of timing updates per host call while values or events change
       other conditions. */
    var TIMING_ROUNDS = 8;
    var variables = {};
    var timing = null;
    var compiled = {};

    function timingIssue(text) {
        native.issue(ISSUE_TIMING, text);
    }

    /* Ticks for "500ms", "2s", "1.5s", "12f" (frames at 60000/1001 per
       second, as in playlists) or "HH:MM:SS:FF"; Infinity for "indefinite";
       NaN for anything else. */
    function timeValue(text) {
        var m = /^\s*(\d+(?:\.\d+)?)(ms|s|f)\s*$/.exec(text);
        if (m)
            return Math.round(+m[1] * (m[2] === 'ms' ? 90 : m[2] === 's' ? TICKS_PER_SECOND :
                                                                           1501.5));
        if (/^\s*indefinite\s*$/.test(text))
            return Infinity;
        try {
            return timecodeTicks(text.replace(/^\s+|\s+$/g, ''), 'timing');
        } catch (e) {
            return NaN;
        }
    }

    /* XPath: the subset disc markup uses. Values are booleans, numbers,
       strings and node sets ({nodes: [...]}). */

    function tokenize(text) {
        var tokens = [], i = 0, m, rest,
            pattern = /^(?:\s+|'([^']*)'|"([^"]*)"|(\d+(?:\.\d+)?|\.\d+)|(\$?[A-Za-z_][\w.]*(?:-[A-Za-z_][\w.]*)*(?::[A-Za-z_][\w.]*(?:-[A-Za-z_][\w.]*)*)?)|(\/\/|!=|<=|>=|[\/\[\](),@=<>|*-]))/;
        while (i < text.length) {
            rest = text.substring(i);
            m = pattern.exec(rest);
            if (!m)
                throw new SyntaxError('unexpected "' + rest.charAt(0) + '"');
            i += m[0].length;
            if (m[1] !== undefined || m[2] !== undefined)
                tokens.push({t: 'str', v: m[1] !== undefined ? m[1] : m[2]});
            else if (m[3] !== undefined)
                tokens.push({t: 'num', v: +m[3]});
            else if (m[4] !== undefined)
                tokens.push(m[4].charAt(0) === '$' ? {t: 'var', v: m[4].substring(1)}
                                                   : {t: 'name', v: m[4]});
            else if (m[5] !== undefined)
                tokens.push({t: 'op', v: m[5]});
        }
        return tokens;
    }

    function parseXPath(text) {
        var tokens = tokenize(text), at = 0, tree;
        function peek(v) {
            var t = tokens[at];
            return t && (t.t === 'op' || t.t === 'name') && t.v === v;
        }
        function take(v) {
            if (!peek(v))
                throw new SyntaxError('expected "' + v + '"');
            at++;
        }
        function binary(next, ops, kind) {
            return function () {
                var left = next(), op;
                for (;;) {
                    op = null;
                    for (var i = 0; i < ops.length; i++)
                        if (peek(ops[i]))
                            op = ops[i];
                    if (op === null)
                        return left;
                    at++;
                    left = {k: kind, op: op, a: left, b: next()};
                }
            };
        }
        function predicates(base) {
            while (peek('[')) {
                at++;
                base = {k: 'filter', a: base, p: or()};
                take(']');
            }
            return base;
        }
        function primary() {
            var t = tokens[at], args;
            if (!t)
                throw new SyntaxError('unexpected end');
            if (t.t === 'str' || t.t === 'num') {
                at++;
                return {k: 'lit', v: t.v};
            }
            if (t.t === 'var') {
                at++;
                return {k: 'var', v: t.v};
            }
            if (peek('(')) {
                at++;
                args = or();
                take(')');
                return args;
            }
            if (peek('@')) {
                at++;
                t = tokens[at++];
                if (!t || t.t !== 'name')
                    throw new SyntaxError('expected an attribute name');
                return {k: 'attr', v: t.v};
            }
            if (peek('//')) {
                at++;
                t = tokens[at++];
                if (!t || !(t.t === 'name' || (t.t === 'op' && t.v === '*')))
                    throw new SyntaxError('expected an element name');
                return {k: 'all', v: t.v};
            }
            if (t.t === 'name' && tokens[at + 1] && tokens[at + 1].v === '(' &&
                tokens[at + 1].t === 'op') {
                at += 2;
                args = [];
                if (!peek(')')) {
                    args.push(or());
                    while (peek(',')) {
                        at++;
                        args.push(or());
                    }
                }
                take(')');
                return {k: 'call', v: t.v, args: args};
            }
            throw new SyntaxError('unexpected "' + t.v + '"');
        }
        function unary() {
            if (peek('-')) {
                at++;
                return {k: 'neg', a: unary()};
            }
            return binary(function () { return predicates(primary()); }, ['|'], 'union')();
        }
        var relational = binary(unary, ['<=', '>=', '<', '>'], 'cmp');
        var equality = binary(relational, ['=', '!='], 'cmp');
        var and = binary(equality, ['and'], 'and');
        var or = binary(and, ['or'], 'or');
        tree = or();
        if (at !== tokens.length)
            throw new SyntaxError('unexpected "' + tokens[at].v + '"');
        return tree;
    }

    /* A compiled expression, or null (with one issue) when it does not
       parse. */
    function compile(text) {
        if (compiled.hasOwnProperty(text))
            return compiled[text];
        try {
            compiled[text] = {text: text, tree: parseXPath(text), failed: false};
        } catch (e) {
            timingIssue('expression "' + text + '": ' + e.message);
            compiled[text] = null;
        }
        return compiled[text];
    }

    function isNodes(value) {
        return value !== null && typeof value === 'object';
    }

    function toBoolean(value) {
        if (isNodes(value))
            return value.nodes.length > 0;
        if (typeof value === 'number')
            return value === value && value !== 0;
        if (typeof value === 'string')
            return value.length > 0;
        return !!value;
    }

    function toNumber(value) {
        if (isNodes(value))
            return NaN;
        if (typeof value === 'string')
            return /^\s*-?(\d+(\.\d*)?|\.\d+)\s*$/.test(value) ? +value : NaN;
        return +value;
    }

    function toString(value) {
        if (isNodes(value))
            return value.nodes.length ? native.id(timing.epoch, value.nodes[0]) || '' : '';
        if (typeof value === 'boolean')
            return value ? 'true' : 'false';
        return String(value);
    }

    function compare(op, a, b) {
        var x, y;
        if (isNodes(a) || isNodes(b)) {
            /* Only against booleans: the markup compares node sets no other
               way. */
            if (typeof a !== 'boolean' && typeof b !== 'boolean')
                return false;
        }
        if (op === '=' || op === '!=') {
            if (typeof a === 'boolean' || typeof b === 'boolean') {
                x = toBoolean(a);
                y = toBoolean(b);
            } else if (typeof a === 'number' || typeof b === 'number') {
                x = toNumber(a);
                y = toNumber(b);
            } else {
                x = toString(a);
                y = toString(b);
            }
            return op === '=' ? x === y : x !== y;
        }
        x = toNumber(a);
        y = toNumber(b);
        return op === '<' ? x < y : op === '>' ? x > y : op === '<=' ? x <= y : x >= y;
    }

    function idNodes(value) {
        var out = [], ids, i, node;
        if (isNodes(value))
            return value.nodes;
        ids = toString(value).split(/\s+/);
        for (i = 0; i < ids.length; i++)
            if (ids[i] && (node = native.find(ids[i])) >= 0)
                out.push(node);
        return out;
    }

    function call(name, args, context) {
        var node = context.node, a, b, i;
        if (name.substring(0, 6) === 'state:' && args.length === 0) {
            if (node < 0)
                return false;
            a = native.getState(timing.epoch, node, name.substring(6));
            return name === 'state:value' ? a : a === 'true';
        }
        if (name.substring(0, 6) === 'style:' && args.length === 0)
            return node < 0 ? '' : native.getStyle(timing.epoch, node, name.substring(6));
        for (i = 0; i < args.length; i++)
            args[i] = evaluate(args[i], context);
        a = args[0];
        b = args[1];
        switch (name + '/' + args.length) {
            case 'id/1':
                return {nodes: idNodes(a)};
            case 'class/1':
                return {nodes: timing.classes[toString(a)] || []};
            case 'defaultNode/0':
                return {nodes: context.def >= 0 ? [context.def] : []};
            case 'true/0':
                return true;
            case 'false/0':
                return false;
            case 'not/1':
                return !toBoolean(a);
            case 'boolean/1':
                return toBoolean(a);
            case 'number/1':
                return toNumber(a);
            case 'string/1':
                return toString(a);
            case 'count/1':
                return isNodes(a) ? a.nodes.length : NaN;
            case 'contains/2':
                return toString(a).indexOf(toString(b)) >= 0;
            case 'starts-with/2':
                return toString(a).indexOf(toString(b)) === 0;
            case 'substring-before/2':
                a = toString(a);
                i = a.indexOf(toString(b));
                return i < 0 ? '' : a.substring(0, i);
            case 'substring-after/2':
                a = toString(a);
                i = a.indexOf(toString(b));
                return i < 0 ? '' : a.substring(i + toString(b).length);
            case 'concat/2':
                return toString(a) + toString(b);
        }
        throw new Error('unsupported function ' + name + '()');
    }

    function evaluate(tree, context) {
        var a, b, i, out, saved;
        switch (tree.k) {
            case 'lit':
                return tree.v;
            case 'var':
                return variables.hasOwnProperty(tree.v) ? variables[tree.v] : '';
            case 'attr':
                a = context.node < 0 ? null : native.attribute(timing.epoch, context.node, tree.v);
                return a === null ? {nodes: []} : a;
            case 'all':
                return {nodes: tree.v === '*' ? timing.elements : timing.names[tree.v] || []};
            case 'call':
                return call(tree.v, tree.args.slice(), context);
            case 'neg':
                return -toNumber(evaluate(tree.a, context));
            case 'or':
                return toBoolean(evaluate(tree.a, context)) || toBoolean(evaluate(tree.b, context));
            case 'and':
                return toBoolean(evaluate(tree.a, context)) && toBoolean(evaluate(tree.b, context));
            case 'cmp':
                return compare(tree.op, evaluate(tree.a, context), evaluate(tree.b, context));
            case 'union':
                a = evaluate(tree.a, context);
                b = evaluate(tree.b, context);
                if (!isNodes(a) || !isNodes(b))
                    throw new Error('| needs node sets');
                return {nodes: a.nodes.concat(b.nodes)};
            case 'filter':
                a = evaluate(tree.a, context);
                if (!isNodes(a))
                    throw new Error('a predicate needs a node set');
                out = [];
                saved = context.node;
                for (i = 0; i < a.nodes.length; i++) {
                    context.node = a.nodes[i];
                    b = evaluate(tree.p, context);
                    if (typeof b === 'number' ? b === i + 1 : toBoolean(b))
                        out.push(a.nodes[i]);
                }
                context.node = saved;
                return {nodes: out};
        }
        throw new Error('bad expression');
    }

    /* An expression's value in a context; an expression that fails reports
       one issue and is false from then on. */
    function run(expression, def) {
        if (expression === null || expression.failed)
            return false;
        try {
            return evaluate(expression.tree, {node: -1, def: def});
        } catch (e) {
            expression.failed = true;
            timingIssue('expression "' + expression.text + '": ' + e.message);
            return false;
        }
    }

    function selected(expression, def) {
        var value = run(expression, def);
        return isNodes(value) ? value.nodes : [];
    }

    /* Building the timing tree from markup. */

    function children(epoch, node) {
        var out = [], child = native.relative(epoch, node, 1);
        while (child >= 0) {
            out.push(child);
            child = native.relative(epoch, child, 2);
        }
        return out;
    }

    /* A begin or end attribute: an offset (ticks) or a condition. */
    function moment(epoch, node, name) {
        var text = native.attribute(epoch, node, name), ticks;
        if (text === null)
            return null;
        ticks = timeValue(text);
        if (ticks === ticks)
            return {time: ticks};
        return {condition: compile(text)};
    }

    function action(epoch, node) {
        var name = native.name(epoch, node), count, i, ns, prop, value, out, params;
        if (name === 'event') {
            params = [];
            out = {kind: 'event', name: native.attribute(epoch, node, 'name'), params: params};
            if (!out.name) {
                timingIssue('an event without a name');
                return null;
            }
            children(epoch, node).forEach(function (child) {
                if (native.name(epoch, child) === 'param')
                    params.push([native.attribute(epoch, child, 'name') || '',
                                 native.attribute(epoch, child, 'value') || '']);
            });
            return out;
        }
        if (name !== 'set' && name !== 'animate') {
            timingIssue('<' + name + '> is not supported in a cue');
            return null;
        }
        out = {kind: name, props: [],
               discrete: native.attribute(epoch, node, 'calcMode') === 'discrete'};
        count = native.attributeAt(epoch, node, -1);
        for (i = 0; i < count; i++) {
            ns = native.attributeAt(epoch, node, i, 0);
            if (ns !== STYLE_NS && ns !== STATE_NS)
                continue;
            prop = native.attributeAt(epoch, node, i, 1);
            value = native.attributeAt(epoch, node, i, 2);
            out.props.push({state: ns === STATE_NS, name: prop, discrete: out.discrete,
                            keys: name === 'animate' ? value.split(';') : [value]});
        }
        return out;
    }

    function container(epoch, node, defs) {
        var name = native.name(epoch, node), dur, item, use, i;
        if (name !== 'par' && name !== 'seq' && name !== 'excl' && name !== 'cue') {
            timingIssue('<' + name + '> is not supported in timing');
            return null;
        }
        if (name === 'seq' || name === 'excl')
            timingIssue('<' + name + '> runs as <par>');
        dur = native.attribute(epoch, node, 'dur');
        item = {
            cue: name === 'cue', node: node, begin: moment(epoch, node, 'begin'),
            end: moment(epoch, node, 'end'), dur: dur === null ? null : timeValue(dur),
            hold: native.attribute(epoch, node, 'fill') === 'hold',
            select: name === 'cue' && native.attribute(epoch, node, 'select') !== null
                ? compile(native.attribute(epoch, node, 'select')) : null,
            actions: [], items: [], active: false, held: false, started: false,
            rising: false, begun: 0, ended: 0, order: 0, def: -1, targets: []
        };
        if (item.dur !== null && !(item.dur === item.dur)) {
            timingIssue('dur "' + dur + '" is not a time');
            item.dur = null;
        }
        if (item.begin && item.begin.condition === undefined && item.begin.time === Infinity)
            item.begin = {condition: null};
        if (!item.begin)
            item.begin = {time: 0};
        children(epoch, node).forEach(function (child) {
            var made = item.cue ? action(epoch, child) : container(epoch, child, defs);
            if (made)
                (item.cue ? item.actions : item.items).push(made);
        });
        use = native.attribute(epoch, node, 'use');
        if (item.cue && use !== null) {
            use = use.split(/\s+/);
            for (i = 0; i < use.length; i++) {
                if (!use[i])
                    continue;
                if (defs.hasOwnProperty(use[i]))
                    item.actions = item.actions.concat(defs[use[i]]);
                else
                    timingIssue('use="' + use[i] + '" names no definition');
            }
        }
        return item;
    }

    function buildTiming(epoch) {
        var count = native.count(), i, node = -1, name, defs = {}, items = [], cls, names = {},
            classes = {}, elements = [], j;
        for (i = 0; i < count; i++) {
            name = native.name(epoch, i);
            if (name === 'timing' && node < 0)
                node = i;
            elements.push(i);
            (names[name] = names[name] || []).push(i);
            cls = native.attribute(epoch, i, 'class');
            if (cls !== null) {
                cls = cls.split(/\s+/);
                for (j = 0; j < cls.length; j++)
                    if (cls[j])
                        (classes[cls[j]] = classes[cls[j]] || []).push(i);
            }
        }
        if (node < 0)
            return null;
        timing = {epoch: epoch, names: names, classes: classes, elements: elements};
        children(epoch, node).forEach(function (child) {
            if (native.name(epoch, child) !== 'defs')
                return;
            children(epoch, child).forEach(function (d) {
                var id = native.attribute(epoch, d, 'id'), list = [];
                if (id === null)
                    return;
                if (native.name(epoch, d) === 'g')
                    children(epoch, d).forEach(function (a) {
                        var made = action(epoch, a);
                        if (made)
                            list.push(made);
                    });
                else if ((list[0] = action(epoch, d)) === null)
                    list = [];
                defs[id] = list;
            });
        });
        children(epoch, node).forEach(function (child) {
            var made;
            if (native.name(epoch, child) === 'defs')
                return;
            made = container(epoch, child, defs);
            if (made)
                items.push(made);
        });
        timing.items = items;
        timing.start = now;
        timing.order = 0;
        timing.applied = {};
        timing.events = [];
        return timing;
    }

    /* Running the tree. */

    function reset(item) {
        var i;
        item.active = item.held = item.started = item.rising = false;
        for (i = 0; i < item.items.length; i++)
            reset(item.items[i]);
    }

    /* matched: the elements a begin condition matched, if any. */
    function start(item, at, def, matched) {
        var i;
        for (i = 0; i < item.items.length; i++)
            reset(item.items[i]);
        item.active = item.started = true;
        item.held = false;
        item.begun = at;
        item.order = ++timing.order;
        if (item.cue) {
            item.targets = item.select ? selected(item.select, def)
                                       : matched.length ? matched : def >= 0 ? [def] : [];
            item.def = item.targets.length ? item.targets[0] : def;
            for (i = 0; i < item.actions.length; i++)
                if (item.actions[i].kind === 'event')
                    timing.events.push({action: item.actions[i],
                                        target: item.targets.length ? item.targets[0] : -1});
        } else {
            item.def = matched.length ? matched[0] : def;
        }
    }

    function finish(item, at) {
        var i;
        item.active = false;
        item.ended = at;
        item.held = item.cue && item.hold;
        for (i = 0; i < item.items.length; i++)
            reset(item.items[i]);
    }

    /* The default node a cue's conditions see before it begins: its first
       selected element, else its parent's. */
    function contextNode(item, def) {
        var nodes;
        if (item.active || item.held)
            return item.def;
        if (!item.select)
            return def;
        nodes = selected(item.select, def);
        return nodes.length ? nodes[0] : def;
    }

    function update(items, begun, def) {
        var i, item, value, rise, endAt;
        for (i = 0; i < items.length; i++) {
            item = items[i];
            if (item.begin.condition !== undefined) {
                value = run(item.begin.condition, contextNode(item, def));
                rise = toBoolean(value);
                if (rise && !item.rising)
                    start(item, now, def, isNodes(value) ? value.nodes : []);
                item.rising = rise;
            } else if (!item.started && now >= begun + item.begin.time) {
                start(item, begun + item.begin.time, def, []);
            }
            if (!item.active)
                continue;
            endAt = item.dur === null ? Infinity : item.begun + item.dur;
            if (item.end && item.end.time !== undefined)
                endAt = Math.min(endAt, begun + item.end.time);
            if (now >= endAt)
                finish(item, Math.max(endAt, item.begun));
            else if (item.end && item.end.condition !== undefined &&
                     toBoolean(run(item.end.condition, item.def)))
                finish(item, now);
            if (item.active)
                update(item.items, item.begun, item.def);
        }
    }

    function sample(prop, item) {
        var keys = prop.keys, position, k, t;
        if (keys.length === 1)
            return keys[0];
        if (!item.active)
            return keys[keys.length - 1];
        if (!(item.dur > 0) || item.dur === Infinity)
            return keys[0];
        t = Math.min(1, (now - item.begun) / item.dur);
        if (prop.discrete) {
            k = Math.min(keys.length - 1, Math.floor(t * keys.length));
            return keys[k];
        }
        position = t * (keys.length - 1);
        k = Math.min(keys.length - 2, Math.floor(position));
        return interpolate(keys[k], keys[k + 1], position - k);
    }

    function contributions(items, out) {
        var i, item;
        for (i = 0; i < items.length; i++) {
            item = items[i];
            if (item.cue && (item.active || item.held))
                out.push(item);
            if (item.active)
                contributions(item.items, out);
        }
        return out;
    }

    /* Sends the timing values that changed to the runtime; true when any
       did. */
    function applyTiming() {
        var cues = contributions(timing.items, []), values = {}, keys = [], changed = false,
            i, j, n, a, p, cue, key, before, entry;
        cues.sort(function (x, y) { return x.order - y.order; });
        for (i = 0; i < cues.length; i++) {
            cue = cues[i];
            for (j = 0; j < cue.actions.length; j++) {
                a = cue.actions[j];
                if (a.kind === 'event')
                    continue;
                for (p = 0; p < a.props.length; p++)
                    for (n = 0; n < cue.targets.length; n++) {
                        key = cue.targets[n] + (a.props[p].state ? ' state ' : ' style ') +
                              a.props[p].name;
                        if (!values.hasOwnProperty(key))
                            keys.push(key);
                        values[key] = {node: cue.targets[n], prop: a.props[p],
                                       value: sample(a.props[p], cue)};
                    }
            }
        }
        for (key in timing.applied)
            if (timing.applied.hasOwnProperty(key) && !values.hasOwnProperty(key)) {
                entry = timing.applied[key];
                delete timing.applied[key];
                changed = true;
                setTiming(entry.node, entry.prop, null);
            }
        for (i = 0; i < keys.length; i++) {
            entry = values[keys[i]];
            before = timing.applied[keys[i]];
            if (before && before.value === entry.value)
                continue;
            timing.applied[keys[i]] = entry;
            changed = true;
            setTiming(entry.node, entry.prop, entry.value);
        }
        return changed;
    }

    function setTiming(node, prop, value) {
        try {
            if (prop.state)
                native.timingState(timing.epoch, node, prop.name, value);
            else
                native.timingStyle(timing.epoch, node, prop.name, value);
        } catch (e) {
            timingIssue('state or style "' + prop.name + '": ' + e.message);
        }
    }

    function timingEvent(entry) {
        var event = new Event(), i;
        event.initEvent(entry.action.name, false, false);
        for (i = 0; i < entry.action.params.length; i++)
            event[entry.action.params[i][0]] = entry.action.params[i][1];
        deliver(event, entry.target >= 0 ? element(timing.epoch, entry.target) : null);
    }

    /* Runs at the end of every host call: until nothing changes (or for
       TIMING_ROUNDS rounds), updates the tree, applies its values and
       dispatches the events of the cues that began. */
    function runTiming() {
        var round, events, i, changed;
        for (round = 0; round < TIMING_ROUNDS; round++) {
            if (!timing || timing.epoch !== native.epoch())
                return;
            update(timing.items, timing.start, -1);
            changed = applyTiming();
            events = timing.events;
            timing.events = [];
            for (i = 0; i < events.length; i++)
                timingEvent(events[i]);
            if (!changed && !events.length)
                return;
        }
    }

    /* Globals */

    var application = {
        addEventListener: addEventListener,
        removeEventListener: removeEventListener,
        dispatchEvent: dispatchEvent,
        createEvent: createEvent,
        createTimer: createTimer
    };

    var document = {
        getElementById: function (id) {
            var epoch = native.epoch();
            return epoch ? element(epoch, native.find(String(id))) : null;
        },
        createEvent: createEvent
    };
    Object.defineProperty(document, 'body', {
        enumerable: true,
        get: function () {
            var epoch = native.epoch(), count = native.count(), i;
            for (i = 0; epoch && i < count; i++)
                if (native.name(epoch, i) === 'body')
                    return element(epoch, i);
            return null;
        }
    });

    global.document = document;
    global.application = application;
    global.addEventListener = addEventListener;
    global.removeEventListener = removeEventListener;
    global.dispatchEvent = dispatchEvent;
    global.createEvent = createEvent;
    global.createTimer = createTimer;
    global.setMarkupLoadedHandler = function (handler) {
        if (handler !== null && typeof handler !== 'function')
            throw new TypeError('setMarkupLoadedHandler needs a function');
        markupHandler = handler;
    };
    /* Its value is unconfirmed; createTimer ignores the clock argument. */
    global.TIMER_APPLICATION = 1;

    /* Player and application lifecycle (runtime_player.c). Calls that
       change playback queue commands for the host; reads return what the
       host last reported. */

    var COMMAND = {play: 0, pause: 1, jump: 2, audio: 3, subtitle: 4, audioLanguage: 5,
                   subtitleLanguage: 6, subtitleVisible: 7, activate: 8, inactivate: 9,
                   toTop: 10, toBottom: 11};
    var titles = [];
    var parameters = {};

    function unsupported(name) {
        return function () {
            throw new Error(name + ' is not supported');
        };
    }

    /* File, XML, network and storage access is denied. */
    function denied(name) {
        return function () {
            throw new Error(name + ' is not available: file and network access are denied');
        };
    }

    function stub(name, constants, methods) {
        var object = {}, key, i;
        for (key in constants)
            if (constants.hasOwnProperty(key))
                object[key] = constants[key];
        for (i = 0; i < methods.length; i++)
            object[methods[i]] = denied(name + '.' + methods[i]);
        return object;
    }

    function currentPlaylist(object) {
        if (object._epoch !== native.player(8))
            throw new Error('the title is no longer in the playlist');
    }

    function Chapter(epoch, title, index) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_title', title);
        hidden(this, '_index', index);
    }
    Chapter.prototype.jump = function (time) {
        var begin;
        currentPlaylist(this);
        begin = native.chapter(this._epoch, this._title, this._index);
        if (begin < 0)
            throw new Error('the chapter\'s time base is not supported');
        native.command(COMMAND.jump, this._title, begin + timecodeTicks(time, 'jump'));
    };
    Object.defineProperty(Chapter.prototype, 'number', {
        get: function () { return this._index + 1; }
    });

    function TitleAttributes(epoch, index) {
        hidden(this, '_epoch', epoch);
        hidden(this, '_index', index);
    }
    Object.defineProperties(TitleAttributes.prototype, {
        id: {enumerable: true, get: function () { return native.title(this._epoch, this._index, 0); }},
        titleNumber: {enumerable: true, get: function () { return this._index + 1; }},
        titleDuration: {enumerable: true, get: function () {
            var ticks = native.title(this._epoch, this._index, 1);
            return ticks < 0 ? null : formatTimecode(ticks);
        }}
    });

    function Title(epoch, index) {
        var chapters = [], count = native.title(epoch, index, 2), i;
        for (i = 0; i < count; i++)
            chapters[i] = new Chapter(epoch, index, i);
        hidden(this, '_epoch', epoch);
        hidden(this, '_index', index);
        hidden(this, 'attributes', new TitleAttributes(epoch, index));
        hidden(this, 'chapters', chapters);
    }
    /* The second argument is accepted and ignored (its meaning is unknown). */
    Title.prototype.jump = function (time) {
        currentPlaylist(this);
        native.command(COMMAND.jump, this._index, timecodeTicks(time, 'jump'));
    };
    /* The reported title time, on the current title only. */
    Object.defineProperty(Title.prototype, 'elapsedTime', {
        get: function () {
            return this._epoch === native.player(8) && native.player(0) === this._index
                ? formatTimecode(native.player(1)) : null;
        }
    });

    function trackLanguage(kind, number) {
        var title = native.player(0);
        return title < 0 || !number ? null
            : native.trackLanguage(native.player(8), title, kind, number);
    }

    var playlist = {
        PLAYSTATE_STOP: 0,
        PLAYSTATE_PLAY: 1,
        PLAYSTATE_PAUSE: 2,
        play: function () { native.command(COMMAND.play); },
        pause: function () { native.command(COMMAND.pause); }
    };
    Object.defineProperties(playlist, {
        titles: {enumerable: true, get: function () { return titles; }},
        currentTitle: {enumerable: true, get: function () {
            var title = native.player(0);
            return title < 0 ? null : titles[title];
        }},
        currentChapter: {enumerable: true, get: function () {
            return {number: native.player(2)};
        }},
        playState: {enumerable: true, get: function () { return native.player(3); }}
    });

    var track = {
        selectAudioTrackNumber: function (n) { native.command(COMMAND.audio, Number(n)); },
        selectSubtitleTrackNumber: function (n) { native.command(COMMAND.subtitle, Number(n)); },
        selectAudioLanguage: function (code) {
            native.command(COMMAND.audioLanguage, String(code));
        },
        selectSubtitleLanguage: function (code) {
            native.command(COMMAND.subtitleLanguage, String(code));
        }
    };
    Object.defineProperties(track, {
        currentAudioTrackNumber: {enumerable: true, get: function () { return native.player(4); }},
        currentSubtitleTrackNumber: {enumerable: true,
                                     get: function () { return native.player(5); }},
        selectedAudioTrackNumber: {enumerable: true, get: function () { return native.player(4); }},
        selectedSubtitleTrackNumber: {enumerable: true,
                                      get: function () { return native.player(5); }},
        selectedAudioLanguageCode: {enumerable: true,
                                    get: function () { return trackLanguage(0, native.player(4)); }},
        selectedSubtitleLanguageCode: {enumerable: true, get: function () {
            return trackLanguage(1, native.player(5));
        }}
    });

    var subtitle = {};
    Object.defineProperty(subtitle, 'visible', {
        enumerable: true,
        get: function () { return native.player(6); },
        set: function (value) { native.command(COMMAND.subtitleVisible, !!value); }
    });

    /* Names are kept with a prefix, so no name reaches Object.prototype. */
    var generalParameters = {
        getValue: function (name) {
            var key = '$' + String(name);
            return parameters.hasOwnProperty(key) ? parameters[key] : null;
        },
        setValue: function (name, value) {
            var key = '$' + String(name);
            if (value === undefined || value === null)
                delete parameters[key];
            else
                parameters[key] = value;
        }
    };

    function audioOutput(name) {
        return {setVolumes: unsupported(name + '.setVolumes'),
                setMixing: unsupported(name + '.setMixing')};
    }
    var effect = audioOutput('Player.audio.effect');
    effect.play = unsupported('Player.audio.effect.play');
    effect.stop = unsupported('Player.audio.effect.stop');

    var Player = {
        playlist: playlist,
        track: track,
        subtitle: subtitle,
        generalParameters: generalParameters,
        capabilities: {network: {connected: false}},
        audio: {main: audioOutput('Player.audio.main'), sub: audioOutput('Player.audio.sub'),
                effect: effect},
        video: {main: {changeLayout: unsupported('Player.video.main.changeLayout')},
                sub: {changeLayout: unsupported('Player.video.sub.changeLayout')}},
        createVideoScale: unsupported('Player.createVideoScale'),
        standardContentPlayer: {getSPRM: unsupported('Player.standardContentPlayer.getSPRM')},
        /* Placeholder values. */
        FINISHED: 0,
        FILE_NOT_FOUND: 1,
        WRONG_FORMAT: 2
    };
    Object.defineProperty(Player, 'menuLanguage', {
        enumerable: true,
        get: function () { return native.player(7); }
    });
    global.Player = Player;

    /* Markup timing, which these drive, is not modeled. */
    /* Variables markup timing expressions read as $name. */
    document.setXPathVariable = function (name, value) {
        variables[String(name)] = value === undefined || value === null ? '' : String(value);
    };
    document.getProperties = unsupported('document.getProperties');

    global.Diagnostics = {
        TRACE_LEVEL_ERROR: 0,
        TRACE_LEVEL_WARNING: 1,
        TRACE_LEVEL_INFO: 2,
        trace: function (message) { native.issue(4, String(message)); }
    };

    /* Constants have placeholder values: the stubs never return them. */
    global.FileIO = stub('FileIO', {SUCCEEDED: 0, FAILED: 1, FILE_NOT_FOUND: 2},
                         ['openTextFile', 'getFiles', 'getDirectoryInfo', 'createDirectory',
                          'copy', 'move', 'remove']);
    global.XMLParser = stub('XMLParser', {OK: 0, READY: 1, PARSING_ERR: 2, UTF8: 0, UTF16: 1},
                            ['parse', 'parseString', 'write', 'status']);
    global.Network = stub('Network', {HTTP_GET: 0, HTTP_POST: 1}, ['createHTTPClient']);
    global.PersistentStorageManager = stub('PersistentStorageManager', {contentId: ''},
                                           ['getPersistentStorageDevices',
                                            'callManagementMenu']);
    global.PersistentStorageDevice = {SUCCEEDED: 0, STORAGE_REQUIRED: 1};
    global.Node = {ELEMENT_NODE: 1, TEXT_NODE: 3};

    /* Applications, in the z-order the host reports. */
    function Application(index) {
        hidden(this, '_index', index);
    }
    Object.defineProperties(Application.prototype, {
        id: {enumerable: true, get: function () { return native.application(this._index); }},
        zOrder: {enumerable: true, get: function () { return this._index; }}
    });
    Application.prototype.activate = function () {
        native.command(COMMAND.activate, this._index);
    };
    Application.prototype.inactivate = function () {
        native.command(COMMAND.inactivate, this._index);
    };

    function self() {
        var index = native.application(-2);
        if (index < 0)
            throw new Error('the host has not said which application this is');
        return index;
    }
    application.moveToTop = function () { native.command(COMMAND.toTop, self()); };
    application.moveToBottom = function () { native.command(COMMAND.toBottom, self()); };
    Object.defineProperties(application, {
        advancedApplications: {enumerable: true, get: function () {
            var list = [], count = native.application(-1), i;
            for (i = 0; i < count; i++)
                list[i] = new Application(i);
            return list;
        }},
        thisAdvancedApplication: {enumerable: true, get: function () {
            var index = native.application(-2);
            return index < 0 ? null : new Application(index);
        }},
        attributes: {enumerable: true, get: function () {
            var index = native.application(-2);
            return {id: index < 0 ? null : native.application(index)};
        }}
    });

    /* Entries for the runtime */

    /* Compass keys by nav direction: left, right, up, down, then the
       diagonals left-up, left-down, right-up, right-down. */
    function direction(code) {
        switch (code) {
        case 0x25: return 0;
        case 0x27: return 1;
        case 0x26: return 2;
        case 0x28: return 3;
        case 0x29: return 4;
        case 0x2b: return 5;
        case 0x2a: return 6;
        case 0x2c: return 7;
        default: return -1;
        }
    }

    function idProperty(epoch, node) {
        return function () { return element(epoch, node); };
    }

    return {
        markupChanged: function () {
            var epoch = native.epoch(), count = native.count(), i, id;
            for (i = 0; i < idProperties.length; i++)
                delete document[idProperties[i]];
            idProperties = [];
            wrappers = [];
            animations = [];
            timing = null;
            compiled = {};
            for (i = 0; epoch && i < count; i++) {
                id = native.id(epoch, i);
                if (id === null || id in document)
                    continue;
                Object.defineProperty(document, id, {configurable: true, enumerable: true,
                                                     get: idProperty(epoch, i)});
                idProperties[idProperties.length] = id;
            }
            if (epoch) {
                buildTiming(epoch);
                runTiming();
            }
        },
        markupLoaded: function () {
            if (markupHandler) {
                try {
                    markupHandler.call(global);
                } catch (e) {
                    fault('the markup loaded handler', e);
                }
            }
            runTiming();
        },
        /* The event's target is the focused element, if any (discs read
           evt.target.core.id in key listeners). */
        key: function (code, down) {
            var event = new Event(), proceed, focus = native.focus(), d;
            event.initEvent(down ? 'controller_key_down' : 'controller_key_up', false, true);
            event.key = code;
            proceed = deliver(event, focus >= 0 ? element(native.epoch(), focus) : null);
            if (proceed) {
                d = direction(code);
                if (down && d >= 0)
                    native.navigate(d);
                else if (code === 0x0d) {
                    focus = native.focus();
                    if (focus >= 0)
                        native.setState(native.epoch(), focus, 'actioned', down ? 'true' : 'false');
                }
            }
            runTiming();
            return proceed ? 0 : 1;
        },
        /* The pointer is over `node` (-1: over nothing), button down or
           not. The element gets state pointer and the focus, and actioned
           while the button is down on it. */
        pointer: function (node, down) {
            var epoch = native.epoch();
            if (pointerEpoch !== epoch) {
                pointerNode = -1;
                pointerDown = false;
            }
            if (pointerNode >= 0 && pointerNode !== node) {
                native.setState(epoch, pointerNode, 'pointer', 'false');
                if (pointerDown)
                    native.setState(epoch, pointerNode, 'actioned', 'false');
                pointerDown = false;
            }
            pointerEpoch = epoch;
            pointerNode = epoch ? node : -1;
            if (pointerNode >= 0) {
                native.setState(epoch, node, 'pointer', 'true');
                native.setState(epoch, node, 'focused', 'true');
                if (down !== pointerDown)
                    native.setState(epoch, node, 'actioned', down ? 'true' : 'false');
                pointerDown = down;
            }
            runTiming();
        },
        advance: function (ticks) {
            now = ticks;
            runTimers();
            stepAnimations();
            runTiming();
        },
        playlistChanged: function () {
            var epoch = native.player(8), count = native.titleCount(), i, id;
            titles = [];
            for (i = 0; epoch && i < count; i++) {
                titles[i] = new Title(epoch, i);
                id = native.title(epoch, i, 0);
                if (id !== null && !(id in titles))
                    Object.defineProperty(titles, id, {value: titles[i]});
            }
            runTiming();
        },
        dispatch: function (type) {
            var event = new Event(), count = native.field(-1), i;
            event.initEvent(type, false, false);
            for (i = 0; i < count; i++)
                event[native.field(i, 0)] = native.field(i, 1);
            deliver(event);
            runTiming();
        }
    };
})
