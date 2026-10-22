#ifndef KICH_WALLMARK_SNAPSHOT_H
#define KICH_WALLMARK_SNAPSHOT_H
#include "../RenderSnapshot.h"
#include <stdio.h>
#include <stdint.h>
/* Diagnostic-only helpers shared by the R1/R2/native CPU wallmark frontend.
 * These functions never select, allocate, fade or change renderer state. */
namespace kich_wallmark_snapshot {
inline void ID(char* out,size_t size,const char* type,const void* address,uint64_t generation=0){
    snprintf(out,size,"%s@%llx/%llu",type,static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)),static_cast<unsigned long long>(generation));}
inline void Float(const char* id,const char* key,float value){if(!RenderSnapshot_Detailed())return;char out[48];snprintf(out,sizeof(out),"%.9g",value);RenderSnapshot_Record("DECALS",id,key,out);}
inline void Pointer(const char* id,const char* key,const void* value){if(!RenderSnapshot_Detailed())return;char out[48];snprintf(out,sizeof(out),"0x%llx",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(value)));RenderSnapshot_Record("DECALS",id,key,out);}
inline void Event(const char* kind,const void* subject,const char* action,const char* result,uint64_t generation=0){
    if(!RenderSnapshot_DiagnosticActive())return;
    char id[96];ID(id,sizeof(id),kind,subject,generation);RenderSnapshot_Event("DECALS",id,action,result);}
inline void Mark(const char* id,const char* type,const void* owner,const void* receiver,const float* center,float radius,
                 uint64_t fragments,float ttl,float age,float fade,bool created_known,uint64_t created_frame,bool creation_diagnostic,const char* timer){
    if(!RenderSnapshot_Detailed())return;
    RenderSnapshot_Record("DECALS",id,"type",type);RenderSnapshot_Record("DECALS",id,"id_scope",created_known?"PROCESS_LIFETIME_ADDRESS_AND_GENERATION; NOT_CROSS_RUN":"LIVE_ADDRESS; NO_LIFETIME_SERIAL; NOT_CROSS_RUN");
    Pointer(id,"owner_process_handle",owner);Pointer(id,"receiver_process_handle",receiver);
    RenderSnapshot_Record("DECALS",id,"owner_receiver_scope",created_known?"WALLMARK_ENGINE_CONTAINER_AND_STATIC_TRIANGLE":"CKINEMATICS_OWNER_AND_RECEIVER");
    RenderSnapshot_Record("DECALS",id,"bounds_validity","OBSERVED_EXISTING_ENGINE_SPHERE; NOT_RECOMPUTED_OR_VALIDATED");
    char bounds[160];snprintf(bounds,sizeof(bounds),"%.9g,%.9g,%.9g;radius=%.9g",center[0],center[1],center[2],radius);
    RenderSnapshot_Record("DECALS",id,"world_sphere",bounds);RenderSnapshot_RecordU64("DECALS",id,"geometry_fragments",fragments);
    RenderSnapshot_Record("DECALS",id,"historical_creation_request",creation_diagnostic?"OBSERVED_DURING_DIAGNOSTIC":"NOT_OBSERVED");
    RenderSnapshot_Record("DECALS",id,"creation_time_valid",created_known?"1":"0");
    if(created_known){Float(id,"age_game_seconds",age);RenderSnapshot_RecordU64("DECALS",id,"creation_engine_frame",created_frame);}
    else {RenderSnapshot_Record("DECALS",id,"age_game_seconds","NOT_MEASURED");RenderSnapshot_Record("DECALS",id,"creation_engine_frame","NOT_MEASURED");}
    Float(id,"ttl_remaining",ttl);Float(id,"fade_parameter_before_u8",fade);RenderSnapshot_Record("DECALS",id,"timer",timer);
    RenderSnapshot_Record("DECALS",id,"effect_origin","SEE_DIAGNOSTIC_ORIGIN_EVENT_IF_PRESENT; OTHERWISE_NOT_INSTRUMENTED; DO_NOT_INFER_FROM_TEXTURE");
    RenderSnapshot_Record("DECALS",id,"pixel_contribution","NOT_MEASURED");
    RenderSnapshot_Record("DECALS",id,"material_binding","WALLMARK_STREAM_BATCH; SEE_DRAW_ROUTE");
}
inline void Selection(const char* id,bool selected,const char* reason){
    if(!g_render_snapshot_active)return;
    RenderSnapshot_Record("DECALS",id,"selection",selected?"SELECTED":"REJECTED");RenderSnapshot_Record("DECALS",id,"selection_reason",reason);
    RenderSnapshot_Note("CANDIDATE_CLASSIFIED","DECALS",id,reason);RenderSnapshot_Note(selected?"SELECTED":"CULLED","DECALS",id,reason);
}
inline void Batch(bool skeletal,uint32_t vertices,uint32_t offset){
    if(!RenderSnapshot_Detailed())return;
    char id[96];snprintf(id,sizeof(id),"wallmark_stream/%s/offset_%u/vertices_%u",skeletal?"skeletal":"static",offset,vertices);
    RenderSnapshot_Context(skeletal?"DECAL_SKELETAL":"DECAL_STATIC",id,"WALLMARK_SELECTED_SHADER");
    RenderSnapshot_Object(skeletal?"DECAL_SKELETAL":"DECAL_STATIC",id,"WALLMARK_SELECTED_SHADER","WallmarksEngine::FlushStream; CPU_PROJECTED_OR_SKINNED",0);
    RenderSnapshot_Queue(skeletal?"WALLMARK_SKELETAL_STREAM":"WALLMARK_STATIC_STREAM");RenderSnapshot_Skin(skeletal?"CPU_WALLMARK_1W_2W":"NONE");
    RenderSnapshot_Record("DECALS",id,"kind",skeletal?"SKELETAL_STREAM_BATCH":"STATIC_STREAM_BATCH");
    RenderSnapshot_Record("DECALS",id,"mark_to_draw_relation","MANY_TO_ONE; INDIVIDUAL_MARK_DRAW_ID_NOT_AVAILABLE_AFTER_BATCHING");
    RenderSnapshot_RecordU64("DECALS",id,"submitted_vertex_request",vertices);RenderSnapshot_RecordU64("DECALS",id,"submitted_triangle_request",vertices/3);
    RenderSnapshot_Record("DECALS",id,"backend_result","SEE_ACTUAL_DRAW_CALLBACK; REQUEST_IS_NOT_PROOF_OF_SUBMISSION");
}
}
#endif
