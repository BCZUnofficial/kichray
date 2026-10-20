#ifndef KICH_R4_STREAM_RULES_H
#define KICH_R4_STREAM_RULES_H
#include "../KichR4Stream.h"
namespace kich_r4 {
inline bool ValidStreamDraw(const KichR4StreamDrawDesc& d) {
    return d.struct_size==sizeof(d) && d.draw.struct_size==sizeof(d.draw) && d.texture &&
        d.blend_enable<=1 && d.source_blend>=1 && d.source_blend<=10 &&
        d.dest_blend>=1 && d.dest_blend<=10 && d.clamp_texture<=1 &&
        d.scissor_enable<=1 && d.depth_write<=1 && d.min_depth>=0 &&
        d.max_depth<=1 && d.min_depth<=d.max_depth &&
        (!d.scissor_enable || (d.scissor[0]>=0 && d.scissor[1]>=0 &&
        d.scissor[2]>=d.scissor[0] && d.scissor[3]>=d.scissor[1] &&
        d.scissor[2]<=16384 && d.scissor[3]<=16384));
}
}
#endif
