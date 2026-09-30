#include "hddvd.h"
#include <assert.h>
#include <string.h>
int main(void){assert(strcmp(hddvd_version(),"0.2.0")==0);assert(strcmp(hddvd_strerror(HDDVD_OK),"success")==0);assert(hddvd_open(NULL)==NULL);return 0;}
