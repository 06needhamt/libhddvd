#include "internal.h"
#include <stdlib.h>
#include <string.h>
char *hddvd_strdup(const char *s){ if(!s)return NULL; size_t n=strlen(s)+1; char *p=malloc(n); if(p)memcpy(p,s,n); return p; }
void hddvd_free(void *p){ free(p); }
static unsigned char ascii_lower(unsigned char c){return c>='A'&&c<='Z'?(unsigned char)(c+('a'-'A')):c;}
int hddvd_ascii_endswith_ci(const char *s,const char *x){ size_t a,b,i;if(!s||!x)return 0;a=strlen(s);b=strlen(x);if(b>a)return 0;for(i=0;i<b;i++)if(ascii_lower((unsigned char)s[a-b+i])!=ascii_lower((unsigned char)x[i]))return 0;return 1; }
int hddvd_ascii_equal_ci(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*b){if(ascii_lower((unsigned char)*a)!=ascii_lower((unsigned char)*b))return 0;a++;b++;}return *a==*b;}
