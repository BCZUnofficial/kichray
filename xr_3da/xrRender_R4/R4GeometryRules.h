#ifndef KICH_R4_GEOMETRY_RULES_H
#define KICH_R4_GEOMETRY_RULES_H
#include "../KichR4Geometry.h"
namespace kich_r4 {
inline uint32_t VertexFormatBytes(uint32_t format) {
    switch(format){case KICH_R4_FLOAT2:return 8;case KICH_R4_FLOAT3:return 12;
    case KICH_R4_FLOAT4:return 16;case KICH_R4_UNORM8X4:return 4;default:return 0;}
}
inline bool ValidPipeline(const KichR4PipelineDesc& d) {
    if(d.struct_size!=sizeof(d) || !d.vertex_shader || !d.pixel_shader || d.flags ||
        !d.vertex_stride || d.vertex_stride>2048 || !d.element_count || d.element_count>8)return false;
    for(uint32_t i=0;i<d.element_count;++i) {
        const KichR4VertexElement& a=d.elements[i];const uint32_t bytes=VertexFormatBytes(a.format);
        if(a.semantic<KICH_R4_POSITION || a.semantic>KICH_R4_COLOR || a.semantic_index>15 || !bytes ||
            a.byte_offset>d.vertex_stride || bytes>d.vertex_stride-a.byte_offset || a.byte_offset%4)return false;
        for(uint32_t j=0;j<i;++j) {
            const KichR4VertexElement& b=d.elements[j];
            if(a.semantic==b.semantic && a.semantic_index==b.semantic_index)return false;
            if(a.byte_offset<b.byte_offset+VertexFormatBytes(b.format) && b.byte_offset<a.byte_offset+bytes)return false;
        }
    }
    return true;
}
inline bool ValidDraw(const KichR4DrawIndexedDesc& d,uint32_t index_bytes,uint32_t index_stride) {
    if(d.struct_size!=sizeof(d) || !d.pipeline || !d.vertex_buffer || !d.index_buffer || !d.constant_buffer ||
        !d.index_count || d.index_count%3 || d.cull_mode>KICH_R4_CULL_NONE || d.depth_mode>KICH_R4_DEPTH_ON ||
        (index_stride!=2 && index_stride!=4))return false;
    const uint32_t capacity=index_bytes/index_stride;
    return d.start_index<=capacity && d.index_count<=capacity-d.start_index;
}
}
#endif
