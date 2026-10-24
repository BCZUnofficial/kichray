#ifndef KICH_R4_RENDERER_CONTRACT_H
#define KICH_R4_RENDERER_CONTRACT_H
#include "KichR4Renderer.h"
#include <string.h>

/* Kept independent of Windows and engine headers: the actual DLL provider and
 * EXE loader use the same validation, including every callable service. */
static inline int KichR4_BuildHashValid(const char* text) {
    unsigned i;
    for(i=0;i<64;++i)if(!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f')))return 0;
    return text[64]==0;
}
static inline int KichR4_BuildInfoValid(const KichR4BuildInfo* b) {
    return b && b->struct_size==sizeof(*b) && b->pointer_size==sizeof(void*) && b->dirty<=2 &&
        b->revision[0] && memchr(b->revision,0,sizeof(b->revision)) &&
        b->compiled[0] && memchr(b->compiled,0,sizeof(b->compiled)) &&
        b->configuration[0] && memchr(b->configuration,0,sizeof(b->configuration)) &&
        KichR4_BuildHashValid(b->source_id) && KichR4_BuildHashValid(b->shader_id);
}
static inline int KichR4_BuildsMatch(const KichR4BuildInfo* a,const KichR4BuildInfo* b) {
    /* An archive has no Git revision. Content IDs remain mandatory. Separate
     * compile times/CRT configurations are allowed by the C allocation rules. */
    return KichR4_BuildInfoValid(a) && KichR4_BuildInfoValid(b) &&
        !memcmp(a->source_id,b->source_id,64) && !memcmp(a->shader_id,b->shader_id,64);
}
static inline const char* KichR4_ValidateRendererAPI(const KichR4RendererAPI* a) {
    if(!a || a->struct_size!=sizeof(*a) || a->abi_version!=KICH_R4_RENDERER_ABI ||
        a->pointer_size!=sizeof(void*) || a->flags!=KICH_R4_RENDERER_TRANSITIONAL)return "renderer header/ABI";
    if(!KichR4_BuildInfoValid(&a->build))return "renderer build identity";
#define KICH_R4_TABLE_HEADER(member,version) \
    if(a->member.struct_size!=sizeof(a->member) || a->member.abi_version!=(version) || \
       a->member.pointer_size!=sizeof(void*) || a->member.flags)return #member " header/ABI"
    KICH_R4_TABLE_HEADER(frame,KICH_R4_FRAME_ABI);
    KICH_R4_TABLE_HEADER(resources,KICH_R4_RESOURCE_ABI);
    KICH_R4_TABLE_HEADER(geometry,KICH_R4_GEOMETRY_ABI);
    KICH_R4_TABLE_HEADER(level,KICH_R4_LEVEL_ABI);
    KICH_R4_TABLE_HEADER(stream,KICH_R4_STREAM_ABI);
    KICH_R4_TABLE_HEADER(deferred,KICH_R4_DEFERRED_ABI);
    KICH_R4_TABLE_HEADER(tools,KICH_R4_NATIVE_TOOLS_ABI);
    KICH_R4_TABLE_HEADER(details,KICH_R4_DETAIL_ABI);
    KICH_R4_TABLE_HEADER(models,KICH_R4_MODEL_ABI);
#undef KICH_R4_TABLE_HEADER
    if(!a->frame.create_device || !a->frame.destroy_device || !a->frame.create_target || !a->frame.destroy_target ||
        !a->frame.resize || !a->frame.begin_frame || !a->frame.clear || !a->frame.present || !a->frame.get_stats)return "frame callbacks";
    if(!a->resources.create_device || !a->resources.destroy_device || !a->resources.create_texture || !a->resources.create_buffer ||
        !a->resources.compile_shader || !a->resources.get_info || !a->resources.get_binding || !a->resources.get_variable ||
        !a->resources.release_resource || !a->resources.get_stats)return "resource callbacks";
    if(!a->geometry.create_pipeline || !a->geometry.update_buffer || !a->geometry.draw_indexed ||
        !a->geometry.get_stats || !a->geometry.destroy_device)return "geometry callbacks";
    if(!a->level.create_texture_dds || !a->level.create_pipeline || !a->level.draw_textured)return "level callbacks";
    if(!a->stream.create_pipeline || !a->stream.draw)return "stream callbacks";
    if(!a->deferred.create_pipeline || !a->deferred.begin_pass || !a->deferred.draw)return "deferred callbacks";
    if(!a->tools.read_pixels || !a->tools.profile_mark || !a->tools.get_profile || !a->tools.reset_resources || !a->tools.set_snapshot_sink || !a->tools.upload_bgra8)return "tools callbacks";
    if(!a->details.create_pipeline || !a->details.create_instances || !a->details.upload_instances || !a->details.draw)return "detail callbacks";
    if(!a->models.create_pipeline || !a->models.create_pose || !a->models.upload_pose || !a->models.draw)return "model callbacks";
    return NULL;
}
#endif
