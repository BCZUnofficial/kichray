#ifndef KICH_R4_DEFERRED_RULES_H
#define KICH_R4_DEFERRED_RULES_H
#include "../KichR4Deferred.h"
#include "R4StreamRules.h"
#include <float.h>
namespace kich_r4 {
inline bool PassFinite(float v){return v==v && v>=-FLT_MAX && v<=FLT_MAX;}
inline bool ValidPass(const KichR4PassDesc& p){
    if(p.struct_size!=sizeof(p) || p.flags || p.color_count>4 ||
        (!p.color_count && !p.depth) || !p.width || !p.height ||
        p.width>16384 || p.height>16384 || p.clear_flags>3 ||
        ((p.clear_flags&KICH_R4_CLEAR_COLOR) && !p.color_count) ||
        ((p.clear_flags&KICH_R4_CLEAR_DEPTH) && !p.depth) ||
        !PassFinite(p.depth_value) || p.depth_value<0 || p.depth_value>1)return false;
    for(unsigned i=0;i<4;++i){
        if(!PassFinite(p.color[i]) || (i>=p.color_count && p.colors[i]))return false;
        if(i<p.color_count){
            if(!p.colors[i] && (i || p.color_count!=1))return false;
            if(p.depth && p.colors[i]==p.depth)return false;
            for(unsigned j=0;j<i;++j)if(p.colors[j]==p.colors[i])return false;
        }
    }return true;
}
inline bool ValidDeferredDraw(const KichR4DeferredDrawDesc& d){
    KichR4StreamDrawDesc state=d.state;state.texture=1;
    return d.struct_size==sizeof(d) && ValidStreamDraw(state);
}
}
#endif
