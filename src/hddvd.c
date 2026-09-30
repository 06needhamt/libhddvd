#include "internal.h"
#include <stdlib.h>
#include <string.h>
const char *hddvd_version(void){ return "0.2.0"; }
const char *hddvd_strerror(int s){switch(s){case HDDVD_OK:return "success";case HDDVD_EINVAL:return "invalid argument";case HDDVD_ENOMEM:return "out of memory";case HDDVD_EIO:return "I/O error";case HDDVD_EPARSE:return "parse error";case HDDVD_ENOTFOUND:return "not found";case HDDVD_ENOTSUP:return "not supported by this build";case HDDVD_ELIMIT:return "configured limit exceeded";case HDDVD_EPROTECTED:return "protected content requires a decryption provider";default:return "unknown error";}}
hddvd_ctx *hddvd_open(const char *p){hddvd_ctx *c;if(!p||!*p||!hddvd_path_is_dir(p))return NULL;c=calloc(1,sizeof(*c));if(!c)return NULL;c->root=hddvd_strdup(p);if(!c->root){free(c);return NULL;}return c;}
void hddvd_close(hddvd_ctx *c){if(c){free(c->root);free(c);}}
const char *hddvd_root(const hddvd_ctx *c){return c?c->root:NULL;}
void hddvd_set_log_callback(hddvd_ctx *c,hddvd_log_fn f,void *o){if(c){c->log_fn=f;c->log_opaque=o;}}
int hddvd_has_advanced_content(const hddvd_ctx *c){char *p;int r;if(!c)return 0;p=hddvd_path_join(c->root,"ADV_OBJ");if(!p)return 0;r=hddvd_path_is_dir(p);free(p);return r;}
