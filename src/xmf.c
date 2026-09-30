#include "internal.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static void indent(FILE*f,int d){while(d-->0)fputs("  ",f);}
static const char*prop(xmlNodePtr n,const char*k,xmlChar**v){*v=xmlGetProp(n,(const xmlChar*)k);return *v?(const char*)*v:NULL;}
static void dump_manifest(FILE*f,xmlNodePtr n,int d){for(;n;n=n->next){xmlChar*v=NULL;if(n->type!=XML_ELEMENT_NODE)continue;indent(f,d);fprintf(f,"%s",(const char*)n->name);if(prop(n,"src",&v)){fprintf(f," src=%s",(char*)v);xmlFree(v);v=NULL;}if(prop(n,"id",&v)){fprintf(f," id=%s",(char*)v);xmlFree(v);v=NULL;}if(prop(n,"x",&v)){fprintf(f," x=%s",(char*)v);xmlFree(v);v=NULL;}if(prop(n,"y",&v)){fprintf(f," y=%s",(char*)v);xmlFree(v);v=NULL;}if(prop(n,"width",&v)){fprintf(f," width=%s",(char*)v);xmlFree(v);v=NULL;}if(prop(n,"height",&v)){fprintf(f," height=%s",(char*)v);xmlFree(v);}fputc('\n',f);dump_manifest(f,n->children,d+1);}}
static int xml_dump(const void*x,size_t n,const char*name,FILE*f,const char*kind){const void*xml;size_t xml_size;hddvd_object*owner;xmlDocPtr d;int rc;if(!x||!n||!f)return HDDVD_EINVAL;rc=hddvd_object_unwrap_xml(x,n,&xml,&xml_size,&owner);if(rc)return rc;if(xml_size>(size_t)INT_MAX){hddvd_object_free(owner);return HDDVD_ELIMIT;}d=xmlReadMemory(xml,(int)xml_size,name?name:kind,NULL,XML_PARSE_NONET|XML_PARSE_NOBLANKS|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);if(!d){hddvd_object_free(owner);return HDDVD_EPARSE;}fprintf(f,"%s: %s\n",kind,name?name:"(memory)");dump_manifest(f,xmlDocGetRootElement(d),0);xmlFreeDoc(d);hddvd_object_free(owner);return HDDVD_OK;}
int hddvd_xmf_dump(const void*x,size_t n,const char*name,void*fp){return xml_dump(x,n,name,(FILE*)fp,"XMF");}
int hddvd_xmu_dump(const void*x,size_t n,const char*name,void*fp){return xml_dump(x,n,name,(FILE*)fp,"XMU");}
