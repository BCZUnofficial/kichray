#ifndef KICH_R4_TELEMETRY_TEXT_H
#define KICH_R4_TELEMETRY_TEXT_H
#include "RenderSnapshot.h"
#include <stdio.h>
#include <stdint.h>
namespace kich_r4_telemetry {
inline void Text(const char* category,const char* id,const char* key,const char* value){if(g_render_snapshot_active)RenderSnapshot_Record(category,id,key,value);}
inline void U64(const char* category,const char* id,const char* key,uint64_t value){if(g_render_snapshot_active)RenderSnapshot_RecordU64(category,id,key,value);}
inline void Float(const char* category,const char* id,const char* key,float value){if(!g_render_snapshot_active)return;char text[64];snprintf(text,sizeof(text),"%.9g",value);Text(category,id,key,text);}
inline void Vector(const char* category,const char* id,const char* key,const float* value,unsigned count){
    if(!g_render_snapshot_active || !value)return;
    char text[768]={};size_t used=0;
    for(unsigned i=0;i<count && i<16 && used<sizeof(text);++i){const int n=snprintf(text+used,sizeof(text)-used,"%s%.9g",i?",":"",value[i]);if(n<0 || size_t(n)>=sizeof(text)-used)break;used+=size_t(n);}
    Text(category,id,key,text);
}
inline void Handle(char* out,size_t bytes,const char* kind,uint64_t value){snprintf(out,bytes,"%s_%llu",kind,static_cast<unsigned long long>(value));}
}
#endif
