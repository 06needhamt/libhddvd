#include "internal.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XMF_NS "http://www.dvdforum.org/2005/HDDVDVideo/Manifest"
#define XMF_DEFAULT_MAX_NODES 4096u
#define XMF_DEFAULT_MAX_REFS 1024u

struct xmf_ref { char *id; char *src; };
struct hddvd_xmf {
    char *id;
    hddvd_xmf_region region;
    int has_region;
    struct xmf_ref *scripts;
    size_t script_count;
    struct xmf_ref *markups;
    size_t markup_count;
    char **roots;
    size_t root_count;
};

static char *dup_prop(xmlNodePtr node, const char *name)
{
    xmlChar *value = xmlGetProp(node, (const xmlChar *)name);
    char *copy;
    if (!value)
        return NULL;
    copy = malloc(strlen((const char *)value) + 1);
    if (copy)
        strcpy(copy, (const char *)value);
    xmlFree(value);
    return copy;
}

static int in_xmf_namespace(const xmlNode *node)
{
    return node->ns && node->ns->href &&
           !xmlStrcmp(node->ns->href, (const xmlChar *)XMF_NS);
}

static int count_elements(xmlNodePtr node, size_t limit, size_t *count)
{
    for (; node; node = node->next) {
        int rc;
        if (node->type != XML_ELEMENT_NODE) continue;
        if (*count == SIZE_MAX || ++*count > limit) return HDDVD_ELIMIT;
        rc = count_elements(node->children, limit, count);
        if (rc) return rc;
    }
    return HDDVD_OK;
}

static int parse_uint_prop(xmlNodePtr node, const char *name, uint32_t *result)
{
    char *value = dup_prop(node, name);
    char *end;
    unsigned long long number;
    if (!value || !*value) {
        free(value);
        return HDDVD_EPARSE;
    }
    errno = 0;
    number = strtoull(value, &end, 10);
    if (errno || *end || number > UINT32_MAX || value[0] == '-') {
        free(value);
        return HDDVD_EPARSE;
    }
    *result = (uint32_t)number;
    free(value);
    return HDDVD_OK;
}

static int id_exists(const hddvd_xmf *xmf, const char *id)
{
    size_t i;
    if (!strcmp(xmf->id, id))
        return 1;
    for (i = 0; i < xmf->script_count; i++)
        if (!strcmp(xmf->scripts[i].id, id)) return 1;
    for (i = 0; i < xmf->markup_count; i++)
        if (!strcmp(xmf->markups[i].id, id)) return 1;
    return 0;
}

static int append_ref(struct xmf_ref **items, size_t *count, char *id, char *src)
{
    struct xmf_ref *grown;
    if (*count == SIZE_MAX / sizeof(**items))
        return HDDVD_ELIMIT;
    grown = realloc(*items, (*count + 1) * sizeof(**items));
    if (!grown)
        return HDDVD_ENOMEM;
    *items = grown;
    grown[*count].id = id;
    grown[*count].src = src;
    (*count)++;
    return HDDVD_OK;
}

static int append_root(hddvd_xmf *xmf, char *src)
{
    char **grown;
    if (xmf->root_count == SIZE_MAX / sizeof(*grown))
        return HDDVD_ELIMIT;
    grown = realloc(xmf->roots, (xmf->root_count + 1) * sizeof(*grown));
    if (!grown)
        return HDDVD_ENOMEM;
    xmf->roots = grown;
    grown[xmf->root_count++] = src;
    return HDDVD_OK;
}

void hddvd_xmf_limits_default(hddvd_xmf_limits *limits)
{
    if (limits) {
        limits->max_nodes = XMF_DEFAULT_MAX_NODES;
        limits->max_references = XMF_DEFAULT_MAX_REFS;
    }
}

void hddvd_xmf_close(hddvd_xmf *xmf)
{
    size_t i;
    if (!xmf) return;
    free(xmf->id);
    for (i = 0; i < xmf->script_count; i++) {
        free(xmf->scripts[i].id); free(xmf->scripts[i].src);
    }
    for (i = 0; i < xmf->markup_count; i++) {
        free(xmf->markups[i].id); free(xmf->markups[i].src);
    }
    for (i = 0; i < xmf->root_count; i++) free(xmf->roots[i]);
    free(xmf->scripts); free(xmf->markups); free(xmf->roots); free(xmf);
}

int hddvd_xmf_open_with_limits(const void *data, size_t size,
                               const hddvd_xmf_limits *limits,
                               hddvd_xmf **result)
{
    hddvd_xmf_limits defaults;
    hddvd_xmf *xmf = NULL;
    xmlDocPtr doc = NULL;
    xmlNodePtr root, node;
    size_t nodes = 0, references = 0;
    int rc = HDDVD_EPARSE, operation_rc;
    if (!data || !size || !result) return HDDVD_EINVAL;
    *result = NULL;
    if (size > (size_t)INT_MAX) return HDDVD_ELIMIT;
    if (!limits) { hddvd_xmf_limits_default(&defaults); limits = &defaults; }
    if (!limits->max_nodes || !limits->max_references) return HDDVD_EINVAL;
    doc = xmlReadMemory(data, (int)size, "manifest.xmf", NULL,
                        XML_PARSE_NONET | XML_PARSE_NOBLANKS |
                        XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return HDDVD_EPARSE;
    root = xmlDocGetRootElement(doc);
    if (!root || root->type != XML_ELEMENT_NODE ||
        xmlStrcmp(root->name, (const xmlChar *)"Application") ||
        !in_xmf_namespace(root)) goto done;
    operation_rc = count_elements(root, limits->max_nodes, &nodes);
    if (operation_rc) { rc = operation_rc; goto done; }
    xmf = calloc(1, sizeof(*xmf));
    if (!xmf) { rc = HDDVD_ENOMEM; goto done; }
    xmf->id = dup_prop(root, "id");
    if (!xmf->id || !*xmf->id) goto done;
    for (node = root->children; node; node = node->next) {
        char *id = NULL, *src = NULL;
        if (node->type != XML_ELEMENT_NODE) continue;
        if (!in_xmf_namespace(node)) goto done;
        if (!xmlStrcmp(node->name, (const xmlChar *)"Region")) {
            if (xmf->has_region || parse_uint_prop(node, "x", &xmf->region.x) ||
                parse_uint_prop(node, "y", &xmf->region.y) ||
                parse_uint_prop(node, "width", &xmf->region.width) ||
                parse_uint_prop(node, "height", &xmf->region.height) ||
                !xmf->region.width || !xmf->region.height) goto done;
            xmf->has_region = 1;
        } else if (!xmlStrcmp(node->name, (const xmlChar *)"Script") ||
                   !xmlStrcmp(node->name, (const xmlChar *)"Markup")) {
            if (++references > limits->max_references) { rc = HDDVD_ELIMIT; goto done; }
            id = dup_prop(node, "id"); src = dup_prop(node, "src");
            if (!id || !*id || !src || !*src || id_exists(xmf, id)) {
                free(id); free(src); goto done;
            }
            if (!xmlStrcmp(node->name, (const xmlChar *)"Script"))
                operation_rc = append_ref(&xmf->scripts, &xmf->script_count, id, src);
            else operation_rc = append_ref(&xmf->markups, &xmf->markup_count, id, src);
            if (operation_rc) { rc = operation_rc; free(id); free(src); goto done; }
        } else if (!xmlStrcmp(node->name, (const xmlChar *)"Resource")) {
            if (++references > limits->max_references) { rc = HDDVD_ELIMIT; goto done; }
            src = dup_prop(node, "src");
            if (!src || !*src) { free(src); goto done; }
            operation_rc = append_root(xmf, src);
            if (operation_rc) { rc = operation_rc; free(src); goto done; }
        }
        /* Unknown manifest elements are retained semantically by accepting
           them; typed access is added only once their meaning is known. */
    }
    if (!xmf->has_region) goto done;
    *result = xmf; xmf = NULL; rc = HDDVD_OK;
done:
    hddvd_xmf_close(xmf);
    xmlFreeDoc(doc);
    return rc;
}

int hddvd_xmf_open(const void *data, size_t size, hddvd_xmf **result)
{ return hddvd_xmf_open_with_limits(data, size, NULL, result); }
const char *hddvd_xmf_application_id(const hddvd_xmf *xmf)
{ return xmf ? xmf->id : NULL; }
int hddvd_xmf_get_region(const hddvd_xmf *xmf, hddvd_xmf_region *region)
{ if (!xmf || !region) return HDDVD_EINVAL; *region = xmf->region; return HDDVD_OK; }
size_t hddvd_xmf_script_count(const hddvd_xmf *xmf)
{ return xmf ? xmf->script_count : 0; }
size_t hddvd_xmf_markup_count(const hddvd_xmf *xmf)
{ return xmf ? xmf->markup_count : 0; }
size_t hddvd_xmf_resource_root_count(const hddvd_xmf *xmf)
{ return xmf ? xmf->root_count : 0; }
static int get_ref(const struct xmf_ref *refs, size_t count, size_t index,
                   const char **id, const char **src)
{
    if (!refs || index >= count || !id || !src) return HDDVD_EINVAL;
    *id = refs[index].id; *src = refs[index].src; return HDDVD_OK;
}
int hddvd_xmf_get_script(const hddvd_xmf *xmf, size_t index,
                         const char **id, const char **src)
{ return xmf ? get_ref(xmf->scripts, xmf->script_count, index, id, src) : HDDVD_EINVAL; }
int hddvd_xmf_get_markup(const hddvd_xmf *xmf, size_t index,
                         const char **id, const char **src)
{ return xmf ? get_ref(xmf->markups, xmf->markup_count, index, id, src) : HDDVD_EINVAL; }
const char *hddvd_xmf_resource_root(const hddvd_xmf *xmf, size_t index)
{ return xmf && index < xmf->root_count ? xmf->roots[index] : NULL; }

int hddvd_xmf_dump(const void *data, size_t size, const char *name, void *output)
{
    hddvd_xmf *xmf;
    FILE *file = output;
    size_t i;
    int rc;
    if (!file) return HDDVD_EINVAL;
    rc = hddvd_xmf_open(data, size, &xmf);
    if (rc) return rc;
    fprintf(file, "XMF: %s\nApplication id=%s\nRegion x=%u y=%u width=%u height=%u\n",
            name ? name : "(memory)", xmf->id, xmf->region.x, xmf->region.y,
            xmf->region.width, xmf->region.height);
    for (i = 0; i < xmf->script_count; i++)
        fprintf(file, "Script id=%s src=%s\n", xmf->scripts[i].id, xmf->scripts[i].src);
    for (i = 0; i < xmf->markup_count; i++)
        fprintf(file, "Markup id=%s src=%s\n", xmf->markups[i].id, xmf->markups[i].src);
    for (i = 0; i < xmf->root_count; i++) fprintf(file, "Resource src=%s\n", xmf->roots[i]);
    hddvd_xmf_close(xmf);
    return HDDVD_OK;
}

static void indent(FILE *file, int depth) { while (depth-- > 0) fputs("  ", file); }
static void dump_xml(FILE *file, xmlNodePtr node, int depth)
{
    for (; node; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) continue;
        indent(file, depth); fprintf(file, "%s\n", (const char *)node->name);
        dump_xml(file, node->children, depth + 1);
    }
}
int hddvd_xmu_dump(const void *data, size_t size, const char *name, void *output)
{
    xmlDocPtr doc;
    FILE *file = output;
    if (!data || !size || !file) return HDDVD_EINVAL;
    if (size > (size_t)INT_MAX) return HDDVD_ELIMIT;
    doc = xmlReadMemory(data, (int)size, name ? name : "markup.xmu", NULL,
                        XML_PARSE_NONET | XML_PARSE_NOBLANKS |
                        XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return HDDVD_EPARSE;
    fprintf(file, "XMU: %s\n", name ? name : "(memory)");
    dump_xml(file, xmlDocGetRootElement(doc), 0);
    xmlFreeDoc(doc);
    return HDDVD_OK;
}
