#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *hddvd_version(void)
{
    return "0.4.0";
}
uint32_t hddvd_abi_version(void)
{
    return HDDVD_ABI_VERSION;
}
hddvd_features hddvd_get_features(void)
{
    hddvd_features features = HDDVD_FEATURE_OBJECT_ENVELOPE | HDDVD_FEATURE_MARKUP_INSPECTION |
                              HDDVD_FEATURE_SCRIPT_DECODER | HDDVD_FEATURE_DISPLAY_LIST |
                              HDDVD_FEATURE_NAV_SCHEDULER | HDDVD_FEATURE_PROTECTION_PROVIDER |
                              HDDVD_FEATURE_TIME_MAP;
    if (hddvd_runtime_engine())
        features |= HDDVD_FEATURE_SCRIPT_RUNTIME;
    return features;
}
const char *hddvd_feature_name(hddvd_feature feature)
{
    switch (feature)
    {
        case HDDVD_FEATURE_OBJECT_ENVELOPE:
            return "object-envelope";
        case HDDVD_FEATURE_MARKUP_INSPECTION:
            return "markup-inspection";
        case HDDVD_FEATURE_SCRIPT_DECODER:
            return "script-decoder";
        case HDDVD_FEATURE_DISPLAY_LIST:
            return "display-list";
        case HDDVD_FEATURE_NAV_SCHEDULER:
            return "navigation-scheduler";
        case HDDVD_FEATURE_PROTECTION_PROVIDER:
            return "protection-provider";
        case HDDVD_FEATURE_SCRIPT_RUNTIME:
            return "script-runtime";
        case HDDVD_FEATURE_IMAGE_DECODER:
            return "image-decoder";
        case HDDVD_FEATURE_TIME_MAP:
            return "time-map";
        default:
            return NULL;
    }
}
const char *hddvd_strerror(int s)
{
    switch (s)
    {
        case HDDVD_OK:
            return "success";
        case HDDVD_EINVAL:
            return "invalid argument";
        case HDDVD_ENOMEM:
            return "out of memory";
        case HDDVD_EIO:
            return "I/O error";
        case HDDVD_EPARSE:
            return "parse error";
        case HDDVD_ENOTFOUND:
            return "not found";
        case HDDVD_ENOTSUP:
            return "not supported by this build";
        case HDDVD_ELIMIT:
            return "configured limit exceeded";
        case HDDVD_EPROTECTED:
            return "protected content requires a decryption provider";
        case HDDVD_ESCRIPT:
            return "uncaught script exception";
        default:
            return "unknown error";
    }
}
hddvd_ctx *hddvd_open(const char *p)
{
    return hddvd_open_with_limits(p, NULL);
}
hddvd_ctx *hddvd_open_with_limits(const char *p, const hddvd_config_limits *limits)
{
    hddvd_ctx *c;
    if (!p || !*p || !hddvd_path_is_dir(p))
        return NULL;
    c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->root = hddvd_strdup(p);
    if (!c->root)
    {
        free(c);
        return NULL;
    }
    if (limits)
        c->limits = *limits;
    else
        hddvd_config_limits_default(&c->limits);
    if (!hddvd_config_limits_valid(&c->limits))
    {
        hddvd_close(c);
        return NULL;
    }
    return c;
}
void hddvd_close(hddvd_ctx *c)
{
    if (c)
    {
        free(c->root);
        free(c);
    }
}
const char *hddvd_root(const hddvd_ctx *c)
{
    return c ? c->root : NULL;
}
void hddvd_set_log_callback(hddvd_ctx *c, hddvd_log_fn f, void *o)
{
    if (c)
    {
        c->log_fn = f;
        c->log_opaque = o;
    }
}
void hddvd_clear_error(hddvd_ctx *c)
{
    if (c)
        memset(&c->error, 0, sizeof(c->error));
}
int hddvd_get_last_error(const hddvd_ctx *c, hddvd_error_detail *out)
{
    if (!c || !out)
        return HDDVD_EINVAL;
    *out = c->error;
    return HDDVD_OK;
}
void hddvd_ctx_set_error(const hddvd_ctx *context, int status, hddvd_subsystem subsystem,
                         const char *resource, const char *message)
{
    hddvd_ctx *c = (hddvd_ctx *)context;
    if (!c)
        return;
    memset(&c->error, 0, sizeof(c->error));
    c->error.status = status;
    c->error.subsystem = subsystem;
    if (resource)
        snprintf(c->error.resource, sizeof(c->error.resource), "%s", resource);
    snprintf(c->error.message, sizeof(c->error.message), "%s",
             message ? message : hddvd_strerror(status));
    if (c->log_fn && status != HDDVD_OK)
        c->log_fn(c->log_opaque, status, c->error.message);
}
int hddvd_has_advanced_content(const hddvd_ctx *c)
{
    char *p;
    int r;
    if (!c)
        return 0;
    p = hddvd_path_join(c->root, "ADV_OBJ");
    if (!p)
        return 0;
    r = hddvd_path_is_dir(p);
    free(p);
    return r;
}
