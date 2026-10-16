#include "internal.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XMU_NS "http://www.dvdforum.org/2005/ihd"
#define XMU_STATE_NS "http://www.dvdforum.org/2005/ihd#state"
#define XMU_DEFAULT_MAX_DEPTH 64u
#define XMU_DEFAULT_MAX_NODES 8192u
#define XMU_DEFAULT_MAX_ATTRIBUTES 32768u

struct xmu_attribute
{
    char *namespace_uri;
    char *name;
    char *value;
};

struct xmu_node
{
    char *namespace_uri;
    char *name;
    char *id;
    char *text;
    size_t parent;
    size_t first_child;
    size_t next_sibling;
    size_t child_count;
    size_t first_attribute;
    size_t attribute_count;
};

struct xmu_id
{
    const char *id;
    size_t node;
};

struct hddvd_xmu
{
    struct xmu_node *nodes;
    size_t node_count;
    struct xmu_attribute *attributes;
    size_t attribute_count;
    struct xmu_id *ids;
    size_t id_count;
    size_t initial_focus;
    char *clock;
};

static char *copy_xml(const xmlChar *value)
{
    size_t length;
    char *result;
    if (!value)
        return NULL;
    length = strlen((const char *)value);
    result = malloc(length + 1);
    if (result)
        memcpy(result, value, length + 1);
    return result;
}

static int unwrap_input(const void *input, size_t input_size, unsigned char **owned,
                        const void **data, size_t *size)
{
    return hddvd_object_unwrap(input, input_size, HDDVD_OBJECT_MARKUP, owned, data, size);
}

void hddvd_xmu_limits_default(hddvd_xmu_limits *limits)
{
    if (limits)
    {
        limits->max_depth = XMU_DEFAULT_MAX_DEPTH;
        limits->max_nodes = XMU_DEFAULT_MAX_NODES;
        limits->max_attributes = XMU_DEFAULT_MAX_ATTRIBUTES;
    }
}

void hddvd_xmu_close(hddvd_xmu *xmu)
{
    size_t i;
    if (!xmu)
        return;
    for (i = 0; i < xmu->node_count; i++)
    {
        free(xmu->nodes[i].namespace_uri);
        free(xmu->nodes[i].name);
        free(xmu->nodes[i].id);
        free(xmu->nodes[i].text);
    }
    for (i = 0; i < xmu->attribute_count; i++)
    {
        free(xmu->attributes[i].namespace_uri);
        free(xmu->attributes[i].name);
        free(xmu->attributes[i].value);
    }
    free(xmu->nodes);
    free(xmu->attributes);
    free(xmu->ids);
    free(xmu->clock);
    free(xmu);
}

static int append_attribute(hddvd_xmu *xmu, xmlNodePtr element, xmlAttrPtr property,
                            const hddvd_xmu_limits *limits)
{
    struct xmu_attribute *attribute;
    xmlChar *value;
    if (xmu->attribute_count >= limits->max_attributes)
        return HDDVD_ELIMIT;
    attribute = &xmu->attributes[xmu->attribute_count++];
    attribute->namespace_uri = copy_xml(property->ns ? property->ns->href : NULL);
    attribute->name = copy_xml(property->name);
    value = xmlNodeListGetString(element->doc, property->children, 1);
    attribute->value = copy_xml(value);
    xmlFree(value);
    if (!attribute->name || !attribute->value ||
        (property->ns && property->ns->href && !attribute->namespace_uri))
        return HDDVD_ENOMEM;
    return HDDVD_OK;
}

static int append_node(hddvd_xmu *xmu, xmlNodePtr source, size_t parent, size_t depth,
                       const hddvd_xmu_limits *limits, size_t *index)
{
    struct xmu_node *node;
    xmlNodePtr child;
    xmlAttrPtr property;
    xmlChar *content;
    size_t child_index;
    int rc;
    if (depth > limits->max_depth || xmu->node_count >= limits->max_nodes)
        return HDDVD_ELIMIT;
    *index = xmu->node_count++;
    node = &xmu->nodes[*index];
    node->parent = parent;
    node->first_child = SIZE_MAX;
    node->next_sibling = SIZE_MAX;
    node->first_attribute = xmu->attribute_count;
    node->namespace_uri = copy_xml(source->ns ? source->ns->href : NULL);
    node->name = copy_xml(source->name);
    if (!node->name || (source->ns && source->ns->href && !node->namespace_uri))
        return HDDVD_ENOMEM;
    for (property = source->properties; property; property = property->next)
    {
        rc = append_attribute(xmu, source, property, limits);
        if (rc)
            return rc;
        node->attribute_count++;
        if (!property->ns && !xmlStrcmp(property->name, (const xmlChar *)"id"))
        {
            node->id = copy_xml((const xmlChar *)xmu->attributes[xmu->attribute_count - 1].value);
            if (!node->id)
                return HDDVD_ENOMEM;
            if (!*node->id)
                return HDDVD_EPARSE;
        }
    }
    content = xmlNodeGetContent(source);
    if (content && !xmlChildElementCount(source) && *content)
        node->text = copy_xml(content);
    xmlFree(content);
    child_index = SIZE_MAX;
    for (child = source->children; child; child = child->next)
    {
        size_t previous_child = child_index;
        if (child->type != XML_ELEMENT_NODE)
            continue;
        rc = append_node(xmu, child, *index, depth + 1, limits, &child_index);
        if (rc)
            return rc;
        node = &xmu->nodes[*index];
        if (!node->child_count)
            node->first_child = child_index;
        if (previous_child != SIZE_MAX)
            xmu->nodes[previous_child].next_sibling = child_index;
        node->child_count++;
    }
    return HDDVD_OK;
}

static int index_model(hddvd_xmu *xmu)
{
    size_t i, j;
    for (i = 0; i < xmu->node_count; i++)
    {
        struct xmu_node *node = &xmu->nodes[i];
        if (node->id)
        {
            for (j = 0; j < xmu->id_count; j++)
                if (!strcmp(xmu->ids[j].id, node->id))
                    return HDDVD_EPARSE;
            xmu->ids[xmu->id_count].id = node->id;
            xmu->ids[xmu->id_count++].node = i;
        }
        if (!strcmp(node->name, "timing"))
            for (j = 0; j < node->attribute_count; j++)
            {
                struct xmu_attribute *a = &xmu->attributes[node->first_attribute + j];
                if (!a->namespace_uri && !strcmp(a->name, "clock"))
                {
                    if (xmu->clock)
                        return HDDVD_EPARSE;
                    xmu->clock = copy_xml((const xmlChar *)a->value);
                    if (!xmu->clock)
                        return HDDVD_ENOMEM;
                }
            }
        for (j = 0; j < node->attribute_count; j++)
        {
            struct xmu_attribute *a = &xmu->attributes[node->first_attribute + j];
            if (a->namespace_uri && !strcmp(a->namespace_uri, XMU_STATE_NS) &&
                !strcmp(a->name, "focused") && !strcmp(a->value, "true"))
            {
                if (xmu->initial_focus != SIZE_MAX)
                    return HDDVD_EPARSE;
                xmu->initial_focus = i;
            }
        }
    }
    return HDDVD_OK;
}

int hddvd_xmu_open_with_limits(const void *data, size_t size, const hddvd_xmu_limits *limits,
                               hddvd_xmu **result)
{
    hddvd_xmu_limits defaults;
    hddvd_xmu *xmu = NULL;
    xmlDocPtr document = NULL;
    xmlNodePtr root;
    unsigned char *owned = NULL;
    size_t root_index;
    int rc;
    if (!data || !size || !result)
        return HDDVD_EINVAL;
    *result = NULL;
    if (!limits)
    {
        hddvd_xmu_limits_default(&defaults);
        limits = &defaults;
    }
    if (!limits->max_depth || !limits->max_nodes || !limits->max_attributes)
        return HDDVD_EINVAL;
    rc = unwrap_input(data, size, &owned, &data, &size);
    if (rc)
        return rc;
    if (size > (size_t)INT_MAX)
    {
        free(owned);
        return HDDVD_ELIMIT;
    }
    document = xmlReadMemory(data, (int)size, "markup.xmu", NULL,
                             XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR |
                                 XML_PARSE_NOWARNING);
    free(owned);
    if (!document)
        return HDDVD_EPARSE;
    root = xmlDocGetRootElement(document);
    if (!root || xmlStrcmp(root->name, (const xmlChar *)"root") || !root->ns ||
        xmlStrcmp(root->ns->href, (const xmlChar *)XMU_NS))
    {
        xmlFreeDoc(document);
        return HDDVD_EPARSE;
    }
    xmu = calloc(1, sizeof(*xmu));
    if (!xmu)
        rc = HDDVD_ENOMEM;
    else
    {
        xmu->initial_focus = SIZE_MAX;
        xmu->nodes = calloc(limits->max_nodes, sizeof(*xmu->nodes));
        xmu->attributes = calloc(limits->max_attributes, sizeof(*xmu->attributes));
        xmu->ids = calloc(limits->max_nodes, sizeof(*xmu->ids));
        if (!xmu->nodes || !xmu->attributes || !xmu->ids)
            rc = HDDVD_ENOMEM;
        else
            rc = append_node(xmu, root, SIZE_MAX, 1, limits, &root_index);
        if (!rc)
            rc = index_model(xmu);
    }
    xmlFreeDoc(document);
    if (rc)
        hddvd_xmu_close(xmu);
    else
        *result = xmu;
    return rc;
}

int hddvd_xmu_open_with_config(const void *data, size_t size, const hddvd_config_limits *config,
                               hddvd_xmu **out)
{
    hddvd_xmu_limits limits;
    if (!hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    limits.max_depth = config->max_xml_depth;
    limits.max_nodes = config->max_xml_nodes;
    limits.max_attributes = config->max_xml_attributes;
    return hddvd_xmu_open_with_limits(data, size, &limits, out);
}

int hddvd_xmu_open(const void *data, size_t size, hddvd_xmu **result)
{
    return hddvd_xmu_open_with_limits(data, size, NULL, result);
}

size_t hddvd_xmu_node_count(const hddvd_xmu *xmu)
{
    return xmu ? xmu->node_count : 0;
}

int hddvd_xmu_get_node(const hddvd_xmu *xmu, size_t index, hddvd_xmu_node *result)
{
    const struct xmu_node *node;
    if (!xmu || !result || index >= xmu->node_count)
        return HDDVD_EINVAL;
    node = &xmu->nodes[index];
    result->namespace_uri = node->namespace_uri;
    result->name = node->name;
    result->id = node->id;
    result->text = node->text;
    result->parent = node->parent;
    result->first_child = node->first_child;
    result->next_sibling = node->next_sibling;
    result->child_count = node->child_count;
    result->attribute_count = node->attribute_count;
    return HDDVD_OK;
}

int hddvd_xmu_get_attribute(const hddvd_xmu *xmu, size_t node_index, size_t attribute_index,
                            hddvd_xmu_attribute *result)
{
    const struct xmu_node *node;
    const struct xmu_attribute *attribute;
    if (!xmu || !result || node_index >= xmu->node_count)
        return HDDVD_EINVAL;
    node = &xmu->nodes[node_index];
    if (attribute_index >= node->attribute_count)
        return HDDVD_EINVAL;
    attribute = &xmu->attributes[node->first_attribute + attribute_index];
    result->namespace_uri = attribute->namespace_uri;
    result->name = attribute->name;
    result->value = attribute->value;
    return HDDVD_OK;
}

size_t hddvd_xmu_find_id(const hddvd_xmu *xmu, const char *id)
{
    size_t i;
    if (!xmu || !id)
        return SIZE_MAX;
    for (i = 0; i < xmu->id_count; i++)
        if (!strcmp(xmu->ids[i].id, id))
            return xmu->ids[i].node;
    return SIZE_MAX;
}

size_t hddvd_xmu_initial_focus(const hddvd_xmu *xmu)
{
    return xmu ? xmu->initial_focus : SIZE_MAX;
}

const char *hddvd_xmu_clock(const hddvd_xmu *xmu)
{
    return xmu ? xmu->clock : NULL;
}

int hddvd_xmu_dump(const void *data, size_t size, const char *name, void *output)
{
    hddvd_xmu *xmu;
    hddvd_xmu_node node;
    FILE *file = output;
    size_t i, depth;
    int rc;
    if (!file)
        return HDDVD_EINVAL;
    rc = hddvd_xmu_open(data, size, &xmu);
    if (rc)
        return rc;
    fprintf(file, "XMU: %s\n", name ? name : "(memory)");
    for (i = 0; i < xmu->node_count; i++)
    {
        size_t parent = xmu->nodes[i].parent;
        for (depth = 0; parent != SIZE_MAX; depth++)
            parent = xmu->nodes[parent].parent;
        (void)hddvd_xmu_get_node(xmu, i, &node);
        while (depth--)
            fputs("  ", file);
        fprintf(file, "%s\n", node.name);
    }
    hddvd_xmu_close(xmu);
    return HDDVD_OK;
}
