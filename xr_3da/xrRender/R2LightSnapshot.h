#ifndef KICH_R2_LIGHT_SNAPSHOT_H
#define KICH_R2_LIGHT_SNAPSHOT_H
#include "../RenderSnapshot.h"
#include <stdio.h>
/* Light class is defined by the including renderer. This helper observes
 * its existing policy and transforms; it never modifies a light or query. */
namespace kich_r2_light_snapshot {
inline void NodeID(char* out,size_t size,const light* value){snprintf(out,size,"legacy_light_%llu",static_cast<unsigned long long>(value->snapshot_serial));}
inline void SourceID(char* out,size_t size,const light* value){NodeID(out,size,value->snapshot_parent?value->snapshot_parent:value);}
inline void Float(const char* id,const char* key,float value){if(!RenderSnapshot_Detailed())return;char out[48];snprintf(out,sizeof(out),"%.9g",value);RenderSnapshot_Record("LIGHT",id,key,out);}
inline void Matrix(const char* id,const char* key,const float* m){if(!RenderSnapshot_Detailed())return;char out[640];size_t used=0;
    for(unsigned i=0;i<16 && used<sizeof(out);++i){int n=snprintf(out+used,sizeof(out)-used,i?",%.9g":"%.9g",m[i]);if(n<0 || size_t(n)>=sizeof(out)-used)break;used+=size_t(n);}RenderSnapshot_Record("SHADOW",id,key,out);}
inline void ShadowID(char* out,size_t size,const light* value,const char* phase){char node[64];NodeID(node,sizeof(node),value);snprintf(out,size,"%s/%s",node,phase);}
inline void ShadowFloat(const light* value,const char* phase,const char* key,float v){if(!RenderSnapshot_Detailed())return;char id[96],out[48];ShadowID(id,sizeof(id),value,phase);snprintf(out,sizeof(out),"%.9g",v);RenderSnapshot_Record("SHADOW",id,key,out);}
inline void ShadowText(const light* value,const char* phase,const char* key,const char* v){if(!RenderSnapshot_Detailed())return;char id[96];ShadowID(id,sizeof(id),value,phase);RenderSnapshot_Record("SHADOW",id,key,v);}
inline void Shadow(const light* value,const char* phase,const float* matrix){if(!RenderSnapshot_Detailed())return;char id[96],source[64],node[64];ShadowID(id,sizeof(id),value,phase);SourceID(source,sizeof(source),value);NodeID(node,sizeof(node),value);RenderSnapshot_Record("SHADOW",id,"source_light",source);RenderSnapshot_Record("SHADOW",id,"render_light",node);RenderSnapshot_Record("SHADOW",id,"phase",phase);Matrix(id,"caster_view_projection",matrix);RenderSnapshot_Record("SHADOW",id,"matrix_layout","ENGINE_FMATRIX_ROW_MAJOR; 16_FLOATS");RenderSnapshot_Record("SHADOW",id,"visible_shadow_pixels","NOT_MEASURED");}
inline void Event(const light* value,const char* action,const char* result){if(!RenderSnapshot_DiagnosticActive())return;char id[64];NodeID(id,sizeof(id),value);RenderSnapshot_Event("LIGHT",id,action,result);}
inline void Record(const light* value,const char* stage,const char* result,const char* reason){
    if(!RenderSnapshot_Detailed())return;
    char id[64],source[64],text[192],key[96];NodeID(id,sizeof(id),value);SourceID(source,sizeof(source),value);
    RenderSnapshot_Record("LIGHT",id,"source_id",source);RenderSnapshot_Record("LIGHT",id,"id_scope","RENDERER_PROCESS_LIFETIME_SERIAL; NOT_CROSS_RUN");
    RenderSnapshot_RecordU64("LIGHT",id,"serial",value->snapshot_serial);RenderSnapshot_Record("LIGHT",id,"role",value->snapshot_parent?"POINT_SHADOW_FACE":value->flags.bStatic?"LEVEL":"RUNTIME");
    const light* original=value->snapshot_parent?value->snapshot_parent:value;
    RenderSnapshot_Record("LIGHT",id,"record_controller_valid",original->snapshot_record_valid?"1":"0");
    if(original->snapshot_record_valid){RenderSnapshot_RecordU64("LIGHT",id,"record",original->snapshot_record);RenderSnapshot_RecordU64("LIGHT",id,"controller",original->snapshot_controller);}
    else{RenderSnapshot_Record("LIGHT",id,"record","NOT_AVAILABLE");RenderSnapshot_Record("LIGHT",id,"controller","NOT_AVAILABLE");}
    if(value->snapshot_parent)RenderSnapshot_RecordU64("LIGHT",id,"point_face",value->snapshot_face);else RenderSnapshot_Record("LIGHT",id,"point_face","NOT_APPLICABLE");
    RenderSnapshot_RecordU64("LIGHT",id,"type",value->flags.type);RenderSnapshot_RecordU64("LIGHT",id,"active",value->flags.bActive);RenderSnapshot_RecordU64("LIGHT",id,"shadow_flag",value->flags.bShadow);
    snprintf(text,sizeof(text),"%.9g,%.9g,%.9g",value->position.x,value->position.y,value->position.z);RenderSnapshot_Record("LIGHT",id,"position",text);
    snprintf(text,sizeof(text),"%.9g,%.9g,%.9g",value->direction.x,value->direction.y,value->direction.z);RenderSnapshot_Record("LIGHT",id,"direction",text);
    snprintf(text,sizeof(text),"%.9g,%.9g,%.9g",value->color.r,value->color.g,value->color.b);RenderSnapshot_Record("LIGHT",id,"stored_color",text);
    Float(id,"range",value->range);Float(id,"cone",value->cone);
    snprintf(key,sizeof(key),"stage_%s",stage);RenderSnapshot_Record("LIGHT",id,key,result);
    snprintf(key,sizeof(key),"reason_%s",stage);RenderSnapshot_Record("LIGHT",id,key,reason);
    RenderSnapshot_Record("LIGHT",id,"pixel_contribution","NOT_MEASURED");
}
inline void Bind(const light* value,const char* phase){if(!RenderSnapshot_Detailed())return;char id[64];SourceID(id,sizeof(id),value);RenderSnapshot_Light(id,"RENDERER_PROCESS_LIFETIME_SERIAL");RenderSnapshot_Phase(phase);}
}
#endif
