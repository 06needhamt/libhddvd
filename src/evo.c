#include "internal.h"
#include <stdio.h>
#include <string.h>
static hddvd_stream_codec classify(unsigned sid,unsigned sub)
{
    if(sid==0xe0) return HDDVD_CODEC_MPEG2_VIDEO;
    if(sid==0xe2) return HDDVD_CODEC_H264;
    if(sid==0xc0) return HDDVD_CODEC_MPEG_AUDIO;
    if(sid==0xfd && sub>=0x55 && sub<=0x5f)return HDDVD_CODEC_VC1;
    if(sid==0xbd){if(sub>=0x20&&sub<=0x3f)return HDDVD_CODEC_SUBPICTURE;if(sub>=0x88&&sub<=0x9f)return HDDVD_CODEC_DTS_HD;if(sub>=0xa0&&sub<=0xaf)return HDDVD_CODEC_LPCM;if(sub>=0xb0&&sub<=0xbf)return HDDVD_CODEC_TRUEHD_MLP;if(sub>=0xc0&&sub<=0xcf)return HDDVD_CODEC_AC3_EAC3;}
    return HDDVD_CODEC_UNKNOWN;
}
const char *hddvd_codec_name(hddvd_stream_codec c){switch(c){case HDDVD_CODEC_MPEG2_VIDEO:return "MPEG-2 video";case HDDVD_CODEC_H264:return "H.264/AVC";case HDDVD_CODEC_VC1:return "VC-1";case HDDVD_CODEC_MPEG_AUDIO:return "MPEG audio";case HDDVD_CODEC_AC3_EAC3:return "AC-3/E-AC-3";case HDDVD_CODEC_DTS_HD:return "DTS-HD";case HDDVD_CODEC_LPCM:return "LPCM";case HDDVD_CODEC_TRUEHD_MLP:return "MLP/TrueHD";case HDDVD_CODEC_SUBPICTURE:return "subpicture";default:return "unknown";}}
static void add(hddvd_stream_info*out,size_t cap,size_t*n,unsigned sid,unsigned sub){size_t i;hddvd_stream_codec c=classify(sid,sub);for(i=0;i<*n;i++)if(out[i].stream_id==sid&&out[i].substream_id==sub){out[i].packet_count++;return;}if(*n<cap){out[*n].stream_id=(uint8_t)sid;out[*n].substream_id=(uint8_t)sub;out[*n].codec=c;out[*n].packet_count=1;(*n)++;}}
int hddvd_evo_probe(const char *path,hddvd_stream_info*out,size_t cap,size_t*count){FILE*f;unsigned char b[65536];size_t carry=0,n=0,r,i;if(!path||!count||(cap&& !out))return HDDVD_EINVAL;*count=0;f=fopen(path,"rb");if(!f)return HDDVD_EIO;while((r=fread(b+carry,1,sizeof(b)-carry,f))>0){size_t total=carry+r;for(i=0;i+6<total;i++){if(b[i]||b[i+1]||b[i+2]!=1)continue;unsigned sid=b[i+3],sub=0;if(sid==0xba||sid==0xbb||sid==0xbe||sid==0xbf)continue;if(sid==0xbd){size_t p=i+6;if(p+3<total){size_t h=p+3+b[p+2];if(h<total)sub=b[h];}}else if(sid==0xfd){size_t p=i+6;if(p+3<total){unsigned flags=b[p+1];size_t h=p+3+b[p+2];if((flags&1)&&h<total)sub=(unsigned)(b[h]&0x7f);else if(h<total)sub=(unsigned)(b[h]&0x7f);}}add(out,cap,&n,sid,sub);}carry=total<16?total:16;memmove(b,b+total-carry,carry);}fclose(f);*count=n;return HDDVD_OK;}
