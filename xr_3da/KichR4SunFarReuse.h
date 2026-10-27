#ifndef KICH_R4_SUN_FAR_REUSE_H
#define KICH_R4_SUN_FAR_REUSE_H
/* P0-S11 PERF02 r4_sun_far_interval: the 512 m sun cascade (0.06-0.25 m
 * texels, ~half of a frame's draw calls) is rendered once every N frames;
 * between renders receivers sample the kept map with the matrix it was
 * rendered with, so lookups stay exact and only its content is up to N-1
 * frames old. The map is re-rendered at once when it could be wrong:
 * - another map, map generation, size, level revision or sun cull mode;
 * - the camera moved more than kMaxShift since the render. The kept map is
 *   centred on that camera, and its cascade hole (statics inside 0.88 of the
 *   128 m cascade, AddSunHole) has 0.02 x 64 m = 1.28 m of slack before
 *   0.9, where the far map gains weight; 0.5 m keeps the hole covered;
 * - the light basis turned (lightning, a re-based held basis): the linear part
 *   of the world-to-shadow matrix changed by more than kMaxTurn of its scale
 *   (about 0.06 degrees; the sun's own drift per frame is ~100x smaller). */
#include "KichR4TestScene.h"
#include <stdint.h>
#include <string.h>
#include <cmath>
namespace kich_r4_sun_far {
const float kMaxShift=.5f;
const float kMaxTurn=1.e-3f;
struct Key {
    uint64_t map,generation;uint32_t size,revision;int32_t cull;
    bool operator==(const Key& o)const{return map==o.map && generation==o.generation && size==o.size && revision==o.revision && cull==o.cull;}
};
struct State {
    bool valid;uint32_t frame;float position[3];kich_r4_scene::Matrix view;Key key;
    State():valid(false),frame(0),position(),view(),key(){}
    void Rendered(uint32_t at,const float* camera,const kich_r4_scene::Matrix& matrix,const Key& with){
        valid=true;frame=at;memcpy(position,camera,sizeof(position));view=matrix;key=with;}
    void Clear(){valid=false;}
};
// True: this frame draws with state's map and state.view instead of a render.
inline bool Reuse(const State& s,uint32_t interval,uint32_t frame,const float* camera,const kich_r4_scene::Matrix& view,const Key& key){
    if(interval<2 || !s.valid || !(s.key==key))return false;
    if(uint32_t(frame-s.frame)>=interval)return false; // also a frame counter that went back
    double shift=0;for(unsigned a=0;a<3;++a){const double d=double(camera[a])-s.position[a];shift+=d*d;}
    if(!(shift<=double(kMaxShift)*kMaxShift))return false;
    float scale=0,change=0;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){const float then=s.view.m[r*4+c],now=view.m[r*4+c];
        if(!std::isfinite(now))return false;
        scale=std::fabs(then)>scale?std::fabs(then):scale;const float d=std::fabs(now-then);change=d>change?d:change;}
    return scale>0 && change<=kMaxTurn*scale;
}
}
#endif
