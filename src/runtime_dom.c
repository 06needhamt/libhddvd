#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

/* The document state behind the HDi host objects (hdi_prelude.js): the
   focus, script state and style values per markup node, and the natives the
   prelude calls. Everything here is engine-neutral. */

#define STATE_NS "http://www.dvdforum.org/2005/ihd#state"
#define STATE_FOCUSED 0
#define STATE_VALUE 4
/* Issue messages are cut to this many bytes. */
#define MAX_MESSAGE 512u

enum native_index
{
    NATIVE_FIND,
    NATIVE_RELATIVE,
    NATIVE_NAME,
    NATIVE_ID,
    NATIVE_ATTRIBUTE,
    NATIVE_GET_STATE,
    NATIVE_SET_STATE,
    NATIVE_GET_STYLE,
    NATIVE_SET_STYLE,
    NATIVE_UNSET_STYLE,
    NATIVE_FOCUS,
    NATIVE_NAVIGATE,
    NATIVE_EPOCH,
    NATIVE_COUNT,
    NATIVE_ISSUE,
    NATIVE_FIELD
};

/* In enum native_index order. */
const char *const hddvd_runtime_native_names[] = {
    "find", "relative", "name", "id", "attribute", "getState", "setState", "getStyle", "setStyle",
    "unsetStyle", "focus", "navigate", "epoch", "count", "issue", "field",
    /* runtime_player.c, from HDDVD_RUNTIME_PLAYER_NATIVES. */
    "titleCount", "title", "chapter", "trackLanguage", "player", "command", "application"};
const size_t hddvd_runtime_native_count =
    sizeof(hddvd_runtime_native_names) / sizeof(hddvd_runtime_native_names[0]);

static const char *const state_names[HDDVD_RUNTIME_STATE_COUNT] = {"focused", "enabled", "actioned",
                                                                   "pointer", "value"};
/* Values before markup or a script sets one (unconfirmed for enabled). */
static const char *const state_defaults[HDDVD_RUNTIME_STATE_COUNT] = {"false", "true", "false",
                                                                      "false", ""};

static void node_free(struct hddvd_runtime_node *node)
{
    size_t i;
    for (i = 0; i < HDDVD_RUNTIME_STATE_COUNT; i++)
        free(node->state[i]);
    for (i = 0; i < node->style_count; i++)
    {
        free(node->styles[i].name);
        free(node->styles[i].value);
    }
    free(node->styles);
}

void hddvd_runtime_document_free(hddvd_runtime *runtime)
{
    size_t i;
    for (i = 0; runtime->nodes && i < runtime->node_count; i++)
        node_free(&runtime->nodes[i]);
    free(runtime->nodes);
    hddvd_xmu_styles_close(runtime->styles);
    hddvd_xmu_styles_close(runtime->pristine);
    runtime->nodes = NULL;
    runtime->node_count = 0;
    runtime->styles = NULL;
    runtime->pristine = NULL;
    runtime->xmu = NULL;
    runtime->focus = SIZE_MAX;
}

void hddvd_runtime_issues_free(hddvd_runtime *runtime)
{
    size_t i;
    for (i = 0; i < runtime->issue_count; i++)
        free((char *)runtime->issues[i].message);
    runtime->issue_count = 0;
    runtime->dropped = 0;
}

void hddvd_runtime_add_issue(hddvd_runtime *runtime, hddvd_runtime_issue_kind kind, size_t node,
                             const char *text, size_t length)
{
    char *message;
    if (length > MAX_MESSAGE)
        length = MAX_MESSAGE;
    message = malloc(length + 1);
    if (message)
    {
        memcpy(message, text, length);
        message[length] = '\0';
    }
    if (kind == HDDVD_RUNTIME_ISSUE_EXCEPTION && runtime->call_exceptions++ == 0 && message)
        runtime->call_exception = hddvd_strdup(message);
    if (!message || runtime->issue_count == HDDVD_RUNTIME_MAX_ISSUES)
    {
        free(message);
        runtime->dropped++;
        return;
    }
    runtime->issues[runtime->issue_count].kind = kind;
    runtime->issues[runtime->issue_count].node = node;
    runtime->issues[runtime->issue_count].message = message;
    runtime->issue_count++;
}

static void changed(hddvd_runtime *runtime)
{
    runtime->generation++;
}

static void set_focus(hddvd_runtime *runtime, size_t node)
{
    if (runtime->focus != node)
    {
        runtime->focus = node;
        changed(runtime);
    }
}

static int state_index(const char *name)
{
    int i;
    for (i = 0; i < HDDVD_RUNTIME_STATE_COUNT; i++)
        if (!strcmp(name, state_names[i]))
            return i;
    return -1;
}

static const char *markup_attribute(const hddvd_xmu *xmu, size_t node, const char *namespace_uri,
                                    const char *name)
{
    hddvd_xmu_node n;
    hddvd_xmu_attribute a;
    size_t i;
    if (hddvd_xmu_get_node(xmu, node, &n))
        return NULL;
    for (i = 0; i < n.attribute_count; i++)
        if (!hddvd_xmu_get_attribute(xmu, node, i, &a) && !strcmp(a.name, name) &&
            (namespace_uri ? a.namespace_uri && !strcmp(a.namespace_uri, namespace_uri)
                           : !a.namespace_uri))
            return a.value;
    return NULL;
}

static const char *get_state(const hddvd_runtime *runtime, size_t node, int index)
{
    const char *value;
    if (index == STATE_FOCUSED)
        return runtime->focus == node ? "true" : "false";
    if (runtime->nodes[node].state[index])
        return runtime->nodes[node].state[index];
    value = markup_attribute(runtime->xmu, node, STATE_NS, state_names[index]);
    return value ? value : state_defaults[index];
}

/* Flags hold "true" or "false"; anything but "true" is false. */
static int set_state(hddvd_runtime *runtime, size_t node, int index, const char *value)
{
    char *copy;
    if (index == STATE_FOCUSED)
    {
        if (!strcmp(value, "true"))
            set_focus(runtime, node);
        else if (runtime->focus == node)
            set_focus(runtime, SIZE_MAX);
        return HDDVD_OK;
    }
    if (index != STATE_VALUE)
        value = strcmp(value, "true") ? "false" : "true";
    if (!strcmp(get_state(runtime, node, index), value))
        return HDDVD_OK;
    copy = hddvd_strdup(value);
    if (!copy)
        return HDDVD_ENOMEM;
    free(runtime->nodes[node].state[index]);
    runtime->nodes[node].state[index] = copy;
    changed(runtime);
    return HDDVD_OK;
}

static struct hddvd_runtime_style *find_style(struct hddvd_runtime_node *node, const char *name)
{
    size_t i;
    for (i = 0; i < node->style_count; i++)
        if (!strcmp(node->styles[i].name, name))
            return &node->styles[i];
    return NULL;
}

static const char *get_style(const hddvd_runtime *runtime, size_t node, const char *name)
{
    const struct hddvd_runtime_style *style = find_style(&runtime->nodes[node], name);
    const char *declared;
    if (style)
        return style->value;
    declared = hddvd_style_declared(runtime->pristine, node, name);
    if (declared)
        return declared;
    /* Initial values (TTML's, which HDi markup styling follows). */
    if (!strcmp(name, "display"))
        return "auto";
    if (!strcmp(name, "visibility"))
        return "visible";
    if (!strcmp(name, "opacity"))
        return "1";
    return "";
}

/* Recomputes a node from markup plus its script values; a value the model
   rejects leaves its property as markup had it. */
static int rebuild(hddvd_runtime *runtime, size_t node)
{
    struct hddvd_runtime_node *n = &runtime->nodes[node];
    size_t i;
    int rc = hddvd_style_restore(runtime->styles, runtime->pristine, node);
    for (i = 0; !rc && i < n->style_count; i++)
    {
        rc = hddvd_style_override(runtime->styles, node, n->styles[i].name, n->styles[i].value);
        if (rc == HDDVD_ENOTSUP || rc == HDDVD_EPARSE)
            rc = HDDVD_OK;
    }
    return rc;
}

static void style_issue(hddvd_runtime *runtime, size_t node, const char *name, const char *value,
                        int rc)
{
    char message[MAX_MESSAGE + 1];
    snprintf(message, sizeof(message), "style %s: %s value \"%.200s\"", name,
             rc == HDDVD_ENOTSUP ? "unmodeled" : "malformed", value);
    hddvd_runtime_add_issue(runtime, HDDVD_RUNTIME_ISSUE_STYLE, node, message, strlen(message));
}

static int set_style(hddvd_runtime *runtime, size_t node, const char *name, const char *value)
{
    struct hddvd_runtime_node *n = &runtime->nodes[node];
    struct hddvd_runtime_style *style = find_style(n, name);
    char *copy;
    int rc;
    if (style && !strcmp(style->value, value))
        return HDDVD_OK;
    copy = hddvd_strdup(value);
    if (!copy)
        return HDDVD_ENOMEM;
    if (!style)
    {
        struct hddvd_runtime_style *grown;
        char *name_copy = hddvd_strdup(name);
        grown = name_copy ? realloc(n->styles, (n->style_count + 1) * sizeof(*grown)) : NULL;
        if (!grown)
        {
            free(name_copy);
            free(copy);
            return HDDVD_ENOMEM;
        }
        n->styles = grown;
        style = &n->styles[n->style_count++];
        style->name = name_copy;
        style->value = NULL;
    }
    free(style->value);
    style->value = copy;
    changed(runtime);
    rc = hddvd_style_override(runtime->styles, node, name, value);
    if (rc == HDDVD_ENOTSUP || rc == HDDVD_EPARSE)
        style_issue(runtime, node, name, value, rc);
    /* Bring back a value an earlier script set for this property. */
    return rc ? rebuild(runtime, node) : HDDVD_OK;
}

static int unset_style(hddvd_runtime *runtime, size_t node, const char *name)
{
    struct hddvd_runtime_node *n = &runtime->nodes[node];
    struct hddvd_runtime_style *style = find_style(n, name);
    if (!style)
        return HDDVD_OK;
    free(style->name);
    free(style->value);
    *style = n->styles[--n->style_count];
    changed(runtime);
    return rebuild(runtime, node);
}

/* Moves the focus along the focused element's nav property; direction
   follows the prelude: left, right, up, down, left-up, left-down, right-up,
   right-down. */
static size_t navigate(hddvd_runtime *runtime, unsigned direction)
{
    hddvd_xmu_style style;
    hddvd_xmu_style_details details;
    size_t target;
    if (runtime->focus == SIZE_MAX ||
        hddvd_xmu_get_computed_style(runtime->styles, runtime->focus, &style) ||
        hddvd_xmu_get_style_details(runtime->styles, runtime->focus, 1, &details))
        return runtime->focus;
    switch (direction)
    {
        case 0:
            target = style.nav_left;
            break;
        case 1:
            target = style.nav_right;
            break;
        case 2:
            target = style.nav_up;
            break;
        case 3:
            target = style.nav_down;
            break;
        case 4:
            target = details.nav_left_up;
            break;
        case 5:
            target = details.nav_left_down;
            break;
        case 6:
            target = details.nav_right_up;
            break;
        default:
            target = details.nav_right_down;
            break;
    }
    if (target != SIZE_MAX)
        set_focus(runtime, target);
    return runtime->focus;
}

/* Native argument checks. */

static int arg_number(const hddvd_engine_value *args, size_t count, size_t i, double *out)
{
    if (i >= count || args[i].type != HDDVD_ENGINE_NUMBER)
        return 0;
    *out = args[i].number;
    return 1;
}

static int arg_string(const hddvd_engine_value *args, size_t count, size_t i, const char **out)
{
    if (i >= count || args[i].type != HDDVD_ENGINE_STRING || !args[i].string ||
        memchr(args[i].string, '\0', args[i].length))
        return 0;
    *out = args[i].string;
    return 1;
}

/* Arguments 0 and 1: the document the element came from and its node. */
static int arg_node(const hddvd_runtime *runtime, const hddvd_engine_value *args, size_t count,
                    size_t *node, const char **message)
{
    double epoch, index;
    if (!arg_number(args, count, 0, &epoch) || !arg_number(args, count, 1, &index))
    {
        *message = "invalid element";
        return 0;
    }
    if (!runtime->xmu || epoch != (double)runtime->documents)
    {
        *message = "the element is no longer in the document";
        return 0;
    }
    if (!(index >= 0 && index < (double)runtime->node_count) || index != (double)(size_t)index)
    {
        *message = "invalid element";
        return 0;
    }
    *node = (size_t)index;
    return 1;
}

static void set_string(hddvd_engine_value *result, const char *value)
{
    if (!value)
    {
        result->type = HDDVD_ENGINE_NULL;
        return;
    }
    result->type = HDDVD_ENGINE_STRING;
    result->string = value;
    result->length = strlen(value);
}

static void set_number(hddvd_engine_value *result, double value)
{
    result->type = HDDVD_ENGINE_NUMBER;
    result->number = value;
}

static void set_node(hddvd_engine_value *result, size_t node)
{
    set_number(result, node == SIZE_MAX ? -1.0 : (double)node);
}

static int fail(const char **message, const char *text, int rc)
{
    *message = text;
    return rc;
}

static int native_node(hddvd_runtime *runtime, unsigned index, const hddvd_engine_value *args,
                       size_t count, hddvd_engine_value *result, const char **message)
{
    hddvd_xmu_node node;
    const char *name, *value;
    double which;
    size_t at;
    int state, rc;
    if (!arg_node(runtime, args, count, &at, message))
        return HDDVD_EINVAL;
    if (hddvd_xmu_get_node(runtime->xmu, at, &node))
        return fail(message, "invalid element", HDDVD_EINVAL);
    switch (index)
    {
        case NATIVE_RELATIVE:
            if (!arg_number(args, count, 2, &which))
                return fail(message, "invalid relative", HDDVD_EINVAL);
            set_node(result, which == 0   ? node.parent
                             : which == 1 ? node.first_child
                                          : node.next_sibling);
            return HDDVD_OK;
        case NATIVE_NAME:
            set_string(result, node.name);
            return HDDVD_OK;
        case NATIVE_ID:
            set_string(result, node.id);
            return HDDVD_OK;
        case NATIVE_ATTRIBUTE:
            if (!arg_string(args, count, 2, &name))
                return fail(message, "invalid attribute name", HDDVD_EINVAL);
            set_string(result, markup_attribute(runtime->xmu, at, NULL, name));
            return HDDVD_OK;
        case NATIVE_GET_STATE:
        case NATIVE_SET_STATE:
            if (!arg_string(args, count, 2, &name) || (state = state_index(name)) < 0)
                return fail(message, "unknown state property", HDDVD_EINVAL);
            if (index == NATIVE_GET_STATE)
            {
                set_string(result, get_state(runtime, at, state));
                return HDDVD_OK;
            }
            if (!arg_string(args, count, 3, &value))
                return fail(message, "invalid state value", HDDVD_EINVAL);
            rc = set_state(runtime, at, state, value);
            return rc ? fail(message, "out of memory", rc) : HDDVD_OK;
        case NATIVE_GET_STYLE:
        case NATIVE_SET_STYLE:
        case NATIVE_UNSET_STYLE:
            if (!arg_string(args, count, 2, &name) || !*name)
                return fail(message, "invalid style property", HDDVD_EINVAL);
            if (index == NATIVE_GET_STYLE)
            {
                set_string(result, get_style(runtime, at, name));
                return HDDVD_OK;
            }
            /* As in CSS, setting "" removes the script's value. */
            if (index == NATIVE_UNSET_STYLE ||
                (count > 3 && args[3].type == HDDVD_ENGINE_STRING && args[3].length == 0))
                rc = unset_style(runtime, at, name);
            else if (!arg_string(args, count, 3, &value))
                return fail(message, "invalid style value", HDDVD_EINVAL);
            else
                rc = set_style(runtime, at, name, value);
            return rc ? fail(message, "out of memory", rc) : HDDVD_OK;
        default:
            return fail(message, "unknown host function", HDDVD_EINVAL);
    }
}

int hddvd_runtime_native(void *opaque, unsigned index, const hddvd_engine_value *args, size_t count,
                         hddvd_engine_value *result, const char **message)
{
    hddvd_runtime *runtime = opaque;
    const char *text;
    double number;
    switch (index)
    {
        case NATIVE_FIND:
            if (!arg_string(args, count, 0, &text))
                return fail(message, "invalid id", HDDVD_EINVAL);
            set_node(result, runtime->xmu ? hddvd_xmu_find_id(runtime->xmu, text) : SIZE_MAX);
            return HDDVD_OK;
        case NATIVE_FOCUS:
            set_node(result, runtime->focus);
            return HDDVD_OK;
        case NATIVE_NAVIGATE:
            if (!arg_number(args, count, 0, &number) || !(number >= 0 && number < 8))
                return fail(message, "invalid direction", HDDVD_EINVAL);
            set_node(result, navigate(runtime, (unsigned)number));
            return HDDVD_OK;
        case NATIVE_EPOCH:
            set_number(result, runtime->xmu ? (double)runtime->documents : 0.0);
            return HDDVD_OK;
        case NATIVE_COUNT:
            set_number(result, (double)runtime->node_count);
            return HDDVD_OK;
        case NATIVE_ISSUE:
            if (!arg_number(args, count, 0, &number) ||
                (number != HDDVD_RUNTIME_ISSUE_EXCEPTION && number != HDDVD_RUNTIME_ISSUE_TRACE) ||
                count < 2 || args[1].type != HDDVD_ENGINE_STRING)
                return fail(message, "invalid issue", HDDVD_EINVAL);
            hddvd_runtime_add_issue(runtime, (hddvd_runtime_issue_kind)number, SIZE_MAX,
                                    args[1].string, args[1].length);
            return HDDVD_OK;
        case NATIVE_FIELD:
            /* field(-1) counts the dispatched event's fields; field(i, 0 or 1)
               is a name or value. */
            if (!arg_number(args, count, 0, &number))
                return fail(message, "invalid field", HDDVD_EINVAL);
            if (number < 0)
            {
                set_number(result, (double)runtime->field_count);
                return HDDVD_OK;
            }
            if (!(number < (double)runtime->field_count) || count < 2)
                return fail(message, "invalid field", HDDVD_EINVAL);
            set_string(result, args[1].number != 0.0 ? runtime->field_values[(size_t)number]
                                                     : runtime->field_names[(size_t)number]);
            return HDDVD_OK;
        default:
            if (index >= HDDVD_RUNTIME_PLAYER_NATIVES)
                return hddvd_runtime_player_native(runtime, index - HDDVD_RUNTIME_PLAYER_NATIVES,
                                                   args, count, result, message);
            return native_node(runtime, index, args, count, result, message);
    }
}

/* Public API */

int hddvd_runtime_load_markup(hddvd_runtime *runtime, const hddvd_xmu *xmu)
{
    hddvd_xmu_styles *styles = NULL, *pristine = NULL;
    struct hddvd_runtime_node *nodes = NULL;
    size_t count = 0;
    int rc;
    if (!runtime)
        return HDDVD_EINVAL;
    if (xmu)
    {
        count = hddvd_xmu_node_count(xmu);
        rc = hddvd_xmu_styles_open(xmu, &styles);
        if (!rc)
            rc = hddvd_xmu_styles_open(xmu, &pristine);
        if (!rc && !(nodes = calloc(count ? count : 1, sizeof(*nodes))))
            rc = HDDVD_ENOMEM;
        if (rc)
        {
            hddvd_xmu_styles_close(styles);
            hddvd_xmu_styles_close(pristine);
            return rc;
        }
    }
    hddvd_runtime_document_free(runtime);
    runtime->xmu = xmu;
    runtime->styles = styles;
    runtime->pristine = pristine;
    runtime->nodes = nodes;
    runtime->node_count = count;
    runtime->focus = xmu ? hddvd_xmu_initial_focus(xmu) : SIZE_MAX;
    runtime->documents++;
    changed(runtime);
    return hddvd_runtime_run_entry(runtime, "markupChanged", NULL, 0, NULL);
}

int hddvd_runtime_markup_loaded(hddvd_runtime *runtime)
{
    if (!runtime)
        return HDDVD_EINVAL;
    return hddvd_runtime_run_entry(runtime, "markupLoaded", NULL, 0, NULL);
}

const hddvd_xmu_styles *hddvd_runtime_styles(const hddvd_runtime *runtime)
{
    return runtime ? runtime->styles : NULL;
}

size_t hddvd_runtime_focus(const hddvd_runtime *runtime)
{
    return runtime ? runtime->focus : SIZE_MAX;
}

const char *hddvd_runtime_get_state(const hddvd_runtime *runtime, size_t node, const char *name)
{
    int index;
    if (!runtime || !runtime->xmu || !name || node >= runtime->node_count ||
        (index = state_index(name)) < 0)
        return NULL;
    return get_state(runtime, node, index);
}

const char *hddvd_runtime_get_style(const hddvd_runtime *runtime, size_t node, const char *name)
{
    if (!runtime || !runtime->xmu || !name || !*name || node >= runtime->node_count)
        return NULL;
    return get_style(runtime, node, name);
}

uint64_t hddvd_runtime_generation(const hddvd_runtime *runtime)
{
    return runtime ? runtime->generation : 0;
}

int hddvd_runtime_key(hddvd_runtime *runtime, uint32_t key, int down, int *prevented)
{
    hddvd_engine_value args[2];
    double result = 0;
    int rc;
    if (prevented)
        *prevented = 0;
    if (!runtime)
        return HDDVD_EINVAL;
    memset(args, 0, sizeof(args));
    args[0].type = HDDVD_ENGINE_NUMBER;
    args[0].number = (double)key;
    args[1].type = HDDVD_ENGINE_BOOLEAN;
    args[1].number = down ? 1.0 : 0.0;
    rc = hddvd_runtime_run_entry(runtime, "key", args, 2, &result);
    if (prevented && (rc == HDDVD_OK || rc == HDDVD_ESCRIPT))
        *prevented = result == 1.0;
    return rc;
}

int hddvd_runtime_advance(hddvd_runtime *runtime, hddvd_time now)
{
    hddvd_engine_value arg;
    /* Ticks must stay exact as an ECMAScript number. */
    if (!runtime || now.ticks < runtime->now || now.ticks > (UINT64_C(1) << 53))
        return HDDVD_EINVAL;
    runtime->now = now.ticks;
    memset(&arg, 0, sizeof(arg));
    arg.type = HDDVD_ENGINE_NUMBER;
    arg.number = (double)now.ticks;
    return hddvd_runtime_run_entry(runtime, "advance", &arg, 1, NULL);
}

int hddvd_runtime_dispatch(hddvd_runtime *runtime, const char *type)
{
    hddvd_engine_value arg;
    if (!runtime || !type || !*type)
        return HDDVD_EINVAL;
    memset(&arg, 0, sizeof(arg));
    arg.type = HDDVD_ENGINE_STRING;
    arg.string = type;
    arg.length = strlen(type);
    return hddvd_runtime_run_entry(runtime, "dispatch", &arg, 1, NULL);
}

size_t hddvd_runtime_issue_count(const hddvd_runtime *runtime)
{
    return runtime ? runtime->issue_count : 0;
}

int hddvd_runtime_get_issue(const hddvd_runtime *runtime, size_t index, hddvd_runtime_issue *out)
{
    if (!runtime || !out || index >= runtime->issue_count)
        return HDDVD_EINVAL;
    *out = runtime->issues[index];
    return HDDVD_OK;
}

size_t hddvd_runtime_issues_dropped(const hddvd_runtime *runtime)
{
    return runtime ? runtime->dropped : 0;
}

void hddvd_runtime_clear_issues(hddvd_runtime *runtime)
{
    if (runtime)
        hddvd_runtime_issues_free(runtime);
}
