#include "internal.h"
#include <stdio.h>
#include <string.h>
int hddvd_ifo_probe(const char *path,hddvd_ifo_info *o){FILE*f;unsigned char b[16]={0};long z;if(!path||!o)return HDDVD_EINVAL;memset(o,0,sizeof(*o));f=fopen(path,"rb");if(!f)return HDDVD_EIO;if(fread(b,1,16,f)<12){fclose(f);return HDDVD_EPARSE;}if(fseek(f,0,SEEK_END)|| (z=ftell(f))<0){fclose(f);return HDDVD_EIO;}fclose(f);o->size=(uint64_t)z;memcpy(o->identifier,b,16);o->identifier[16]=0;o->recognized_identifier=!memcmp(b,"DVDVIDEO-",9)||!memcmp(b,"HDDVD",5)||!memcmp(b,"HD",2);return HDDVD_OK;}
