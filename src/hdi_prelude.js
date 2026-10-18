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
    function parseTimecode(text) {
        var m = /^(\d+):(\d\d):(\d\d):(\d\d)$/.exec(String(text)), frames, ticks;
        if (!m || +m[2] > 59 || +m[3] > 59 || +m[4] > 59)
            throw new RangeError('createTimer needs a time as HH:MM:SS:FF');
        frames = ((+m[1] * 60 + +m[2]) * 60 + +m[3]) * 60 + +m[4];
        ticks = Math.floor(frames * 3003 / 2);
        if (!(ticks > 0) || ticks > 9007199254740991)
            throw new RangeError('createTimer needs a time above zero');
        return ticks;
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
    /* Markup timing, which would take the property back, is not modeled;
       the name is still checked. */
    State.prototype.unsetProperty = function (name) {
        native.getState(this._epoch, this._node, String(name));
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
            for (i = 0; epoch && i < count; i++) {
                id = native.id(epoch, i);
                if (id === null || id in document)
                    continue;
                Object.defineProperty(document, id, {configurable: true, enumerable: true,
                                                     get: idProperty(epoch, i)});
                idProperties[idProperties.length] = id;
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
            return proceed ? 0 : 1;
        },
        advance: function (ticks) {
            now = ticks;
            runTimers();
            stepAnimations();
        },
        dispatch: function (type) {
            var event = new Event();
            event.initEvent(type, false, false);
            deliver(event);
        }
    };
})
