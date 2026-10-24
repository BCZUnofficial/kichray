#ifndef KICH_R4_LIGHT_PROJECTOR_H
#define KICH_R4_LIGHT_PROJECTOR_H
#include "KichR4LightingMath.h"
#include "KichR4SurfaceState.h"
#include "KichR4TexturePolicy.h"
namespace kich_r4_projector {
// Local SPOT pass only: t2/shadow[0] remains the shadow map. Point lights
// retain all six t2..t7 shadow faces. sun_direction.w is local projector enable.
enum {TextureSlot=3,MapIndex=1,MatrixIndex=1};
inline bool Enabled(const kich_r4_lighting::Constants& c){return c.light_color[3]>1.5f && c.sun_direction[3]>.5f;}
inline void Sampler(KichR4SurfaceState& state){
    if(!state.valid)state=KichR4DefaultSurface();
    state.samplers[TextureSlot]={3,3,3,2,2,0,1,0,0,0}; // R2/1.6 clamp, linear min/mag, no mip interpolation.
}
inline kich_r4_scene::Matrix Projection(const KichR4LocalLight& light,const KichR4LightProjector& projector){
    using namespace kich_r4_scene;
    const Vec3 eye={light.position[0],light.position[1],light.position[2]};
    const Vec3 forward=Normalize(Vec3{light.direction[0],light.direction[1],light.direction[2]});
    const Vec3 supplied={projector.right[0],projector.right[1],projector.right[2]};
    Vec3 up=Cross(forward,supplied);const float square=Dot(up,up);
    Matrix view=kich_r4_lighting::StableView(eye,forward);
    if(kich_r4_lighting::Finite(square) && square>1.e-8f){
        up=Normalize(up);const Vec3 right=Normalize(Cross(up,forward));
        view=Matrix{{right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,
            -Dot(right,eye),-Dot(up,eye),-Dot(forward,eye),1}};
    }
    // Match this game's R2 projection, independently of shadow-map resolution.
    const float fov=light.cone+3.5f*3.14159265358979323846f/180.f;
    return Multiply(view,kich_r4_lighting::Projection(fov,kich_r4_lighting::LocalShadowNear(light.range),light.range));
}
template<class Level> inline uint64_t Resident(const Level& level,const std::string& name,const char*& status){
    status="ANALYTIC_NO_TEXTURE";if(name.empty())return 0;
    try{
        const auto found=level.texture_registry.entries.find(kich_r4_texture::Key(name));
        if(found==level.texture_registry.entries.end()){status="ANALYTIC_NOT_PREFLIGHTED";return 0;}
        const auto& entry=found->second;
        if(entry.faces!=1){status="ANALYTIC_NOT_2D";return 0;}
        if(entry.planned!=level.texture_registry.epoch || entry.choice==UINT32_MAX || !entry.handle || entry.handle==level.checker){
            status="ANALYTIC_UNAVAILABLE_OR_BUDGET";return 0;}
        status="PROJECTED_TEXTURE";return entry.handle;
    }catch(...){status="ANALYTIC_METADATA_UNAVAILABLE";return 0;}
}
template<class Level> inline const char* Configure(const Level& level,const KichR4LocalLight& light,const KichR4LightProjector* projector,
    kich_r4_lighting::Constants& constants,uint64_t* maps){
    constants.sun_direction[3]=0;
    if(light.type!=2)return "POINT_NO_2D_PROJECTOR";
    maps[MapIndex]=0;
    if(!projector)return "ANALYTIC_NO_METADATA";
    const char* status;const uint64_t cookie=Resident(level,projector->texture,status);
    if(cookie){constants.shadow[MatrixIndex]=Projection(light,*projector);constants.sun_direction[3]=1;maps[MapIndex]=cookie;}
    return status;
}
}
#endif
