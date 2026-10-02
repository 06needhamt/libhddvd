#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct dependency_record
{
    hddvd_dependency value;
};

struct requirement_record
{
    hddvd_requirement value;
};

struct hddvd_application
{
    hddvd_xmf *manifest;
    struct dependency_record *dependencies;
    size_t dependency_count;
    struct requirement_record *requirements;
    size_t requirement_count;
};

static int read_stream(hddvd_stream *stream, void **data, size_t *size)
{
    uint64_t length = hddvd_stream_size(stream);
    size_t got = 0;
    if (length > SIZE_MAX)
        return HDDVD_ELIMIT;
    *size = (size_t)length;
    *data = *size ? malloc(*size) : NULL;
    if (*size && !*data)
        return HDDVD_ENOMEM;
    if (hddvd_stream_read_at(stream, 0, *data, *size, &got) || got != *size)
    {
        free(*data);
        *data = NULL;
        return HDDVD_EIO;
    }
    return HDDVD_OK;
}

static int add_dependency(hddvd_application *application, hddvd_dependency_kind kind,
                          const char *id, const char *uri, int status)
{
    struct dependency_record *items;
    char *owned_id = id ? hddvd_strdup(id) : NULL;
    char *owned_uri = hddvd_strdup(uri);
    if ((id && !owned_id) || !owned_uri)
    {
        free(owned_id);
        free(owned_uri);
        return HDDVD_ENOMEM;
    }
    items =
        realloc(application->dependencies, (application->dependency_count + 1) * sizeof(*items));
    if (!items)
    {
        free(owned_id);
        free(owned_uri);
        return HDDVD_ENOMEM;
    }
    application->dependencies = items;
    items += application->dependency_count++;
    items->value.kind = kind;
    items->value.id = owned_id;
    items->value.uri = owned_uri;
    items->value.status = status;
    return HDDVD_OK;
}

static int add_requirement(hddvd_application *application, hddvd_requirement_kind kind,
                           const char *namespace_uri, const char *name, int supported)
{
    struct requirement_record *items;
    char *owned_namespace;
    size_t i;
    for (i = 0; i < application->requirement_count; i++)
        if (application->requirements[i].value.kind == kind &&
            ((!namespace_uri && !application->requirements[i].value.namespace_uri) ||
             (namespace_uri && application->requirements[i].value.namespace_uri &&
              !strcmp(application->requirements[i].value.namespace_uri, namespace_uri))) &&
            !strcmp(application->requirements[i].value.name, name))
        {
            application->requirements[i].value.occurrences++;
            return HDDVD_OK;
        }
    owned_namespace = namespace_uri ? hddvd_strdup(namespace_uri) : NULL;
    if (namespace_uri && !owned_namespace)
        return HDDVD_ENOMEM;
    items =
        realloc(application->requirements, (application->requirement_count + 1) * sizeof(*items));
    if (!items)
    {
        free(owned_namespace);
        return HDDVD_ENOMEM;
    }
    application->requirements = items;
    items += application->requirement_count++;
    memset(items, 0, sizeof(*items));
    items->value.namespace_uri = owned_namespace;
    items->value.name = hddvd_strdup(name);
    if (!items->value.name)
        return HDDVD_ENOMEM;
    items->value.kind = kind;
    items->value.occurrences = 1;
    items->value.supported = (uint8_t)supported;
    return HDDVD_OK;
}

static int supported_element(const char *namespace_uri, const char *name)
{
    static const char ihd_namespace[] = "http://www.dvdforum.org/2005/ihd";
    static const char *const names[] = {"root", "head", "styling", "style", "timing",
                                        "body", "div",  "button",  "img",   "p"};
    size_t i;
    if (!namespace_uri || strcmp(namespace_uri, ihd_namespace))
        return 0;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(name, names[i]))
            return 1;
    return 0;
}

static int supported_style_property(const char *name)
{
    static const char *const names[] = {
        "x",       "y",        "width",           "height",       "display",
        "zIndex",  "position", "backgroundImage", "contentWidth", "contentHeight",
        "navLeft", "navRight", "navUp",           "navDown"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(name, names[i]))
            return 1;
    return 0;
}

static int inspect_node_requirements(hddvd_application *application, const hddvd_xmu *xmu,
                                     size_t index, const hddvd_xmu_node *node)
{
    static const char style_namespace[] = "http://www.dvdforum.org/2005/ihd#style";
    size_t i;
    if (add_requirement(application, HDDVD_REQUIREMENT_ELEMENT, node->namespace_uri, node->name,
                        supported_element(node->namespace_uri, node->name)))
        return HDDVD_ENOMEM;
    for (i = 0; i < node->attribute_count; i++)
    {
        hddvd_xmu_attribute attribute;
        if (hddvd_xmu_get_attribute(xmu, index, i, &attribute))
            continue;
        if (attribute.namespace_uri && !strcmp(attribute.namespace_uri, style_namespace) &&
            add_requirement(application, HDDVD_REQUIREMENT_STYLE_PROPERTY, attribute.namespace_uri,
                            attribute.name, supported_style_property(attribute.name)))
            return HDDVD_ENOMEM;
    }
    return HDDVD_OK;
}

static char *image_uri(const hddvd_xmf *manifest, size_t root_index, const char *source)
{
    const char *root;
    char *result;
    size_t length;
    if (!strncmp(source, "file:///", 8))
        return hddvd_strdup(source);
    root = hddvd_xmf_resource_root(manifest, root_index);
    if (!root)
        return hddvd_strdup(source);
    length = strlen(root) + strlen(source) + 2;
    result = malloc(length);
    if (result)
        snprintf(result, length, "%s/%s", root, source);
    return result;
}

static int add_image_dependency(const hddvd_ctx *context, hddvd_application *application,
                                const char *id, const char *source)
{
    size_t root_count = hddvd_xmf_resource_root_count(application->manifest);
    size_t attempts = root_count ? root_count : 1;
    char *reported_uri = NULL;
    int reported_status = HDDVD_ENOTFOUND;
    size_t i;
    for (i = 0; i < attempts; i++)
    {
        hddvd_stream *image = NULL;
        char *candidate = image_uri(application->manifest, i, source);
        int status = candidate ? hddvd_open_resource(context, candidate, &image) : HDDVD_ENOMEM;
        hddvd_stream_close(image);
        if (!reported_uri)
        {
            reported_uri = candidate;
            reported_status = status;
        }
        else if (!status)
        {
            free(reported_uri);
            reported_uri = candidate;
            reported_status = status;
        }
        else
            free(candidate);
        if (!status || status == HDDVD_ENOMEM || !strncmp(source, "file:///", 8))
            break;
    }
    if (!reported_uri ||
        add_dependency(application, HDDVD_DEPENDENCY_IMAGE, id, reported_uri, reported_status))
    {
        free(reported_uri);
        return HDDVD_ENOMEM;
    }
    free(reported_uri);
    return HDDVD_OK;
}

static int inspect_script(const hddvd_ctx *context, hddvd_application *application, const char *id,
                          const char *uri)
{
    hddvd_stream *stream = NULL;
    hddvd_stream *payload = NULL;
    hddvd_object *object = NULL;
    hddvd_script *script = NULL;
    void *data = NULL;
    size_t size = 0;
    int rc = hddvd_open_resource(context, uri, &stream);
    if (!rc)
        rc = read_stream(stream, &data, &size);
    hddvd_stream_close(stream);
    if (!rc && size >= 8 && !memcmp(data, "HDDVDOBJ", 8))
    {
        rc = hddvd_object_open(data, size, &object);
        if (!rc && hddvd_object_get_type(object) != HDDVD_OBJECT_SCRIPT)
            rc = HDDVD_EPARSE;
        if (!rc)
            rc = hddvd_object_open_payload(object, &payload);
        free(data);
        data = NULL;
        if (!rc)
            rc = read_stream(payload, &data, &size);
    }
    hddvd_stream_close(payload);
    hddvd_object_close(object);
    if (!rc)
        rc = hddvd_script_open(data, size, &script);
    hddvd_script_close(script);
    free(data);
    if (add_dependency(application, HDDVD_DEPENDENCY_SCRIPT, id, uri, rc))
        return HDDVD_ENOMEM;
    return HDDVD_OK;
}

static int inspect_markup(const hddvd_ctx *context, hddvd_application *application, const char *id,
                          const char *uri)
{
    hddvd_stream *stream = NULL;
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    void *data = NULL;
    size_t size = 0, i, j;
    int rc = hddvd_open_resource(context, uri, &stream);
    if (!rc)
        rc = read_stream(stream, &data, &size);
    hddvd_stream_close(stream);
    if (!rc)
        rc = hddvd_xmu_open(data, size, &xmu);
    free(data);
    if (!rc)
        rc = hddvd_xmu_styles_open(xmu, &styles);
    if (add_dependency(application, HDDVD_DEPENDENCY_MARKUP, id, uri, rc))
        rc = HDDVD_ENOMEM;
    if (rc == HDDVD_ENOMEM || !xmu)
        goto done;
    for (i = 0; i < hddvd_xmu_node_count(xmu); i++)
    {
        hddvd_xmu_node node;
        hddvd_xmu_style style;
        if (hddvd_xmu_get_node(xmu, i, &node))
            continue;
        if (inspect_node_requirements(application, xmu, i, &node))
        {
            rc = HDDVD_ENOMEM;
            goto done;
        }
        if (!styles || hddvd_xmu_get_computed_style(styles, i, &style))
            continue;
        for (j = 0; j < style.background_image_count; j++)
        {
            const char *source = hddvd_xmu_style_background_image(styles, i, j, 1);
            if (add_image_dependency(context, application, node.id, source))
            {
                rc = HDDVD_ENOMEM;
                goto done;
            }
        }
    }
done:
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return rc == HDDVD_ENOMEM ? rc : HDDVD_OK;
}

int hddvd_application_open(const hddvd_ctx *context, const char *manifest_uri,
                           hddvd_application **result)
{
    hddvd_application *application;
    hddvd_stream *stream = NULL;
    void *data = NULL;
    size_t size = 0, i;
    int rc;
    if (!context || !manifest_uri || !result)
        return HDDVD_EINVAL;
    *result = NULL;
    application = calloc(1, sizeof(*application));
    if (!application)
        return HDDVD_ENOMEM;
    rc = hddvd_open_resource(context, manifest_uri, &stream);
    if (!rc)
        rc = read_stream(stream, &data, &size);
    hddvd_stream_close(stream);
    if (!rc)
        rc = hddvd_xmf_open(data, size, &application->manifest);
    free(data);
    if (rc)
        goto fail;
    for (i = 0; i < hddvd_xmf_script_count(application->manifest); i++)
    {
        const char *id, *uri;
        hddvd_xmf_get_script(application->manifest, i, &id, &uri);
        if (inspect_script(context, application, id, uri) == HDDVD_ENOMEM)
            goto fail_nomem;
    }
    for (i = 0; i < hddvd_xmf_markup_count(application->manifest); i++)
    {
        const char *id, *uri;
        hddvd_xmf_get_markup(application->manifest, i, &id, &uri);
        if (inspect_markup(context, application, id, uri) == HDDVD_ENOMEM)
            goto fail_nomem;
    }
    *result = application;
    hddvd_clear_error((hddvd_ctx *)context);
    return HDDVD_OK;
fail_nomem:
    rc = HDDVD_ENOMEM;
fail:
    hddvd_application_close(application);
    return rc;
}

void hddvd_application_close(hddvd_application *application)
{
    size_t i;
    if (!application)
        return;
    for (i = 0; i < application->dependency_count; i++)
    {
        free((char *)application->dependencies[i].value.id);
        free((char *)application->dependencies[i].value.uri);
    }
    for (i = 0; i < application->requirement_count; i++)
    {
        free((char *)application->requirements[i].value.namespace_uri);
        free((char *)application->requirements[i].value.name);
    }
    free(application->dependencies);
    free(application->requirements);
    hddvd_xmf_close(application->manifest);
    free(application);
}

const hddvd_xmf *hddvd_application_manifest(const hddvd_application *application)
{
    return application ? application->manifest : NULL;
}

size_t hddvd_application_dependency_count(const hddvd_application *application)
{
    return application ? application->dependency_count : 0;
}

int hddvd_application_get_dependency(const hddvd_application *application, size_t index,
                                     hddvd_dependency *value)
{
    if (!application || !value || index >= application->dependency_count)
        return HDDVD_EINVAL;
    *value = application->dependencies[index].value;
    return HDDVD_OK;
}

size_t hddvd_application_requirement_count(const hddvd_application *application)
{
    return application ? application->requirement_count : 0;
}

int hddvd_application_get_requirement(const hddvd_application *application, size_t index,
                                      hddvd_requirement *value)
{
    if (!application || !value || index >= application->requirement_count)
        return HDDVD_EINVAL;
    *value = application->requirements[index].value;
    return HDDVD_OK;
}
