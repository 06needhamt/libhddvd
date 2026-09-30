#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
static int dump_xmf(void*o,const char*n,const void*b,size_t z){return hddvd_xmf_dump(b,z,n,o);}
int main(int argc,char**argv){void*b=NULL;size_t n=0;int rc;if(argc!=2){fprintf(stderr,"Usage: %s archive.aca\n",argv[0]);return 2;}rc=hddvd_aca_visit_xmf(argv[1],dump_xmf,stdout);if(rc){fprintf(stderr,"XMF inspection failed: %s\n",hddvd_strerror(rc));return 1;}rc=hddvd_aca_read_member(argv[1],"main.xmu",&b,&n);if(rc==HDDVD_OK){rc=hddvd_xmu_dump(b,n,"main.xmu",stdout);hddvd_free(b);}return rc==HDDVD_ENOTFOUND?0:(rc?1:0);}
