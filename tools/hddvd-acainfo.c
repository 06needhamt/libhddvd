#include "hddvd.h"
#include <stdio.h>
static int show(void*o,const hddvd_aca_entry*e){(void)o;printf("%-24s %10u bytes  off=0x%08x crc=%08x flags=%02x\n",e->name,e->size,e->offset,e->crc32,e->flags);return 0;}
int main(int argc,char**argv){size_t bad=0;int rc;if(argc!=2){fprintf(stderr,"Usage: %s archive.aca\n",argv[0]);return 2;}rc=hddvd_aca_list(argv[1],show,NULL);if(rc){fprintf(stderr,"ACA parse failed: %s\n",hddvd_strerror(rc));return 1;}rc=hddvd_aca_verify(argv[1],&bad);printf("CRC verification: %s (%zu bad)\n",rc==HDDVD_OK?"OK":"FAILED",bad);return rc==HDDVD_OK?0:1;}
