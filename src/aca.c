#include "internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p),0755)
#endif
#define ACA_HEADER 32u
#define ACA_FIXED_ENTRY 46u
#define ACA_MAX_ENTRIES 4096u
#define ACA_MAX_MEMBER (128u*1024u*1024u)
static uint16_t be16(const unsigned char*p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t be32(const unsigned char*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint32_t crc32_calc(const unsigned char *p,size_t n){uint32_t c=0xffffffffu;size_t i;for(i=0;i<n;i++){unsigned k;c^=p[i];for(k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c^0xffffffffu;}
typedef struct aca_file { FILE*f; uint64_t file_size; uint16_t count; } aca_file;
static int aca_open(const char*path,aca_file*a){unsigned char h[ACA_HEADER];long end;if(!path||!a)return HDDVD_EINVAL;memset(a,0,sizeof(*a));a->f=fopen(path,"rb");if(!a->f)return HDDVD_EIO;if(fread(h,1,sizeof(h),a->f)!=sizeof(h)||memcmp(h,"HDDVDACA",8)){fclose(a->f);a->f=NULL;return HDDVD_EPARSE;}a->count=be16(h+12);if(!a->count||a->count>ACA_MAX_ENTRIES){fclose(a->f);a->f=NULL;return HDDVD_EPARSE;}if(fseek(a->f,0,SEEK_END)||((end=ftell(a->f))<0)){fclose(a->f);a->f=NULL;return HDDVD_EIO;}a->file_size=(uint64_t)end;return HDDVD_OK;}
static int aca_entries(aca_file*a,hddvd_aca_entry_cb cb,void*o){uint64_t pos=ACA_HEADER,first=0;uint16_t i;for(i=0;i<a->count;i++){unsigned char b[14],pad[32];hddvd_aca_entry e;uint8_t nl;if(pos+ACA_FIXED_ENTRY>a->file_size||fseek(a->f,(long)pos,SEEK_SET)||fread(b,1,14,a->f)!=14)return HDDVD_EPARSE;e.offset=be32(b);e.size=be32(b+4);e.crc32=be32(b+8);e.flags=b[12];nl=b[13];if(!nl||pos+ACA_FIXED_ENTRY+nl>a->file_size)return HDDVD_EPARSE;if(fread(e.name,1,nl,a->f)!=nl||fread(pad,1,32,a->f)!=32)return HDDVD_EPARSE;e.name[nl]=0;if((uint64_t)e.offset+e.size>a->file_size||e.size>ACA_MAX_MEMBER)return HDDVD_EPARSE;if(i==0)first=e.offset;if(e.offset<first)return HDDVD_EPARSE;if(cb){int rc=cb(o,&e);if(rc)return rc;}pos+=ACA_FIXED_ENTRY+nl;}if(first&&pos!=first)return HDDVD_EPARSE;return HDDVD_OK;}
int hddvd_aca_supported(void){return 1;}
int hddvd_aca_list(const char*p,hddvd_aca_entry_cb cb,void*o){aca_file a;int rc=aca_open(p,&a);if(rc)return rc;rc=aca_entries(&a,cb,o);fclose(a.f);return rc;}
typedef struct find_ctx{const char*n;hddvd_aca_entry e;int found;}find_ctx;
static int find_cb(void*o,const hddvd_aca_entry*e){find_ctx*c=o;if(!strcmp(c->n,e->name)){c->e=*e;c->found=1;return 1;}return 0;}
int hddvd_aca_read_member(const char*p,const char*n,void**out,size_t*sz){aca_file a;find_ctx c;unsigned char*b;int rc;if(!p||!n||!out||!sz)return HDDVD_EINVAL;*out=NULL;*sz=0;rc=aca_open(p,&a);if(rc)return rc;memset(&c,0,sizeof(c));c.n=n;rc=aca_entries(&a,find_cb,&c);if(rc!=0&&rc!=1){fclose(a.f);return rc;}if(!c.found){fclose(a.f);return HDDVD_ENOTFOUND;}b=malloc((size_t)c.e.size+1);if(!b){fclose(a.f);return HDDVD_ENOMEM;}if(fseek(a.f,(long)c.e.offset,SEEK_SET)||fread(b,1,c.e.size,a.f)!=c.e.size){free(b);fclose(a.f);return HDDVD_EIO;}fclose(a.f);b[c.e.size]=0;*out=b;*sz=c.e.size;return HDDVD_OK;}
typedef struct verify_ctx{FILE*f;size_t bad;}verify_ctx;
static int verify_cb(void*o,const hddvd_aca_entry*e){verify_ctx*c=o;unsigned char*b=malloc(e->size?e->size:1);uint32_t crc;if(!b)return HDDVD_ENOMEM;if(fseek(c->f,(long)e->offset,SEEK_SET)||fread(b,1,e->size,c->f)!=e->size){free(b);return HDDVD_EIO;}crc=crc32_calc(b,e->size);free(b);if(crc!=e->crc32)c->bad++;return 0;}
int hddvd_aca_verify(const char*p,size_t*bad){aca_file a;verify_ctx c;int rc;if(!bad)return HDDVD_EINVAL;*bad=0;rc=aca_open(p,&a);if(rc)return rc;c.f=a.f;c.bad=0;rc=aca_entries(&a,verify_cb,&c);fclose(a.f);*bad=c.bad;return rc?rc:(c.bad?HDDVD_EPARSE:HDDVD_OK);}
static int safe_name(const char*n){return n&&*n&&!strstr(n,"..")&&!strchr(n,'/')&&!strchr(n,'\\');}
typedef struct extract_ctx{const char*aca,*dir;}extract_ctx;
static int extract_cb(void*o,const hddvd_aca_entry*e){extract_ctx*c=o;void*b=NULL;size_t n=0;char*path;FILE*f;int rc;if(!safe_name(e->name))return HDDVD_EPARSE;rc=hddvd_aca_read_member(c->aca,e->name,&b,&n);if(rc)return rc;path=hddvd_path_join(c->dir,e->name);if(!path){free(b);return HDDVD_ENOMEM;}f=fopen(path,"wb");free(path);if(!f){free(b);return HDDVD_EIO;}if(n&&fwrite(b,1,n,f)!=n)rc=HDDVD_EIO;else rc=HDDVD_OK;fclose(f);free(b);return rc;}
int hddvd_aca_extract(const char*p,const char*d){extract_ctx c;if(!p||!d)return HDDVD_EINVAL;if(!hddvd_path_is_dir(d)&&MKDIR(d)!=0)return HDDVD_EIO;c.aca=p;c.dir=d;return hddvd_aca_list(p,extract_cb,&c);}
typedef struct visit_ctx{const char*aca;hddvd_xmf_cb cb;void*o;}visit_ctx;
static int visit_cb(void*o,const hddvd_aca_entry*e){visit_ctx*c=o;void*b;size_t n;int rc;if(!hddvd_ascii_endswith_ci(e->name,".xmf"))return 0;rc=hddvd_aca_read_member(c->aca,e->name,&b,&n);if(rc)return rc;rc=c->cb(c->o,e->name,b,n);free(b);return rc;}
int hddvd_aca_visit_xmf(const char*p,hddvd_xmf_cb cb,void*o){visit_ctx c;if(!p||!cb)return HDDVD_EINVAL;c.aca=p;c.cb=cb;c.o=o;return hddvd_aca_list(p,visit_cb,&c);}
