#include "internal.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdlib.h>
#include <string.h>
static int name_is(xmlNodePtr n, const char *s)
{
    return n && n->type == XML_ELEMENT_NODE && xmlStrcasecmp(n->name, (const xmlChar *)s) == 0;
}
static xmlChar *first_uri_attr(xmlNodePtr n)
{
    static const char *names[] = {"ref", "src", "href", "uri", NULL};
    int i;
    for (i = 0; names[i]; i++)
    {
        xmlChar *v = xmlGetProp(n, (const xmlChar *)names[i]);
        if (v) {
            return v;
        }
    }
    return NULL;
}
static int walk(xmlNodePtr n, int in_start, char **out)
{
    for (; n; n = n->next)
    {
        int here = in_start || name_is(n, "start");
        if (n->type == XML_ELEMENT_NODE && here)
        {
            xmlChar *v = first_uri_attr(n);
            if (v && *v)
            {
                *out = hddvd_strdup((const char *)v);
                xmlFree(v);
                return *out ? HDDVD_OK : HDDVD_ENOMEM;
            }
            if (v) {
                xmlFree(v);
            }
        }
        if (n->children)
        {
            int r = walk(n->children, here, out);
            if (r == HDDVD_OK || r == HDDVD_ENOMEM) {
                return r;
            }
        }
    }
    return HDDVD_ENOTFOUND;
}
int hddvd_xpl_find_start_ref(const char *path, char **out)
{
    xmlDocPtr d;
    int r;
    if (!path || !out) {
        return HDDVD_EINVAL;
    }
    *out = NULL;
    d = xmlReadFile(path, NULL,
                    XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!d) {
        return HDDVD_EPARSE;
    }
    r = walk(xmlDocGetRootElement(d), 0, out);
    xmlFreeDoc(d);
    return r;
}
