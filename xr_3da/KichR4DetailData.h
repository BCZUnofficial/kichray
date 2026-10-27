#ifndef KICH_R4_DETAIL_DATA_H
#define KICH_R4_DETAIL_DATA_H
#include "KichR4Detail.h"
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cstring>
namespace kich_r4_detail {
inline bool Finite(float v){return std::isfinite(v) && std::fabs(v)<1.e20f;}
inline float Quantized(float v){if(!Finite(v))throw std::runtime_error("nonfinite detail vertex");
    const double q=std::floor(double(v)*16384.0);return float(q< -32768?-32768:q>32767?32767:q)/16384.f;}
struct Mesh {
    std::vector<KichR4DetailVertex> vertices;
    std::vector<uint16_t> indices;
    std::string texture,shader,name;
    float minimum[3],maximum[3],height_factor;
    bool bounds_known;
    Mesh():height_factor(0),bounds_known(false){for(unsigned i=0;i<3;++i)minimum[i]=maximum[i]=0;}
    void CalculateBounds(){bounds_known=!vertices.empty();height_factor=0;
        for(size_t i=0;i<vertices.size();++i){const KichR4DetailVertex& v=vertices[i];
            for(unsigned a=0;a<3;++a){if(!Finite(v.position[a])){bounds_known=false;return;}
                if(!i || v.position[a]<minimum[a])minimum[a]=v.position[a];
                if(!i || v.position[a]>maximum[a])maximum[a]=v.position[a];}
            if(!Finite(v.misc[2])){bounds_known=false;return;}height_factor=std::max(height_factor,std::fabs(v.misc[2]));}}
};
// P0-S4: fallen leaves, litter and pebbles lie within a few cm of the ground.
// R2 renders them into its sun map too, but its coarse map and 2x2 PCF wash
// their shadows out; the sharp contact cascade would show a dark halo under
// every leaf. Flat meshes (local height < 10 cm) therefore never cast sun
// shadows; grass and bushes keep theirs. They still receive shadows.
static const float kFlatClutterHeight=.1f;
inline bool FlatGroundClutter(const Mesh& m){return m.bounds_known && m.maximum[1]-m.minimum[1]<kFlatClutterHeight;}
struct Group {uint32_t mesh,variant,first,count;};
// World AABB of one instance's mesh box; known=false keeps it unbounded.
struct Bounds {float low[3],high[3];bool known;Bounds():known(false){memset(low,0,sizeof(low));memset(high,0,sizeof(high));}};
// P0-S8 bounds of a detail instance: the AABB of the transformed mesh box
// directly (centre plus |row|.half-extent, the same box as its eight
// transformed corners), widened by the wind sway of variants 1 and 2.
inline Bounds InstanceBounds(const Mesh& mesh,const KichR4DetailInstance& instance,uint32_t variant,const float wind[3][4]){
    Bounds result;if(!mesh.bounds_known || variant>2)return result;
    const float centre[3]={(mesh.minimum[0]+mesh.maximum[0])*.5f,(mesh.minimum[1]+mesh.maximum[1])*.5f,(mesh.minimum[2]+mesh.maximum[2])*.5f};
    const float half[3]={(mesh.maximum[0]-mesh.minimum[0])*.5f,(mesh.maximum[1]-mesh.minimum[1])*.5f,(mesh.maximum[2]-mesh.minimum[2])*.5f};
    for(unsigned a=0;a<3;++a){const float* r=instance.row[a];
        const float middle=r[0]*centre[0]+r[1]*centre[1]+r[2]*centre[2]+r[3];
        const float extent=std::fabs(r[0])*half[0]+std::fabs(r[1])*half[1]+std::fabs(r[2])*half[2];
        if(!Finite(middle) || !Finite(extent))return result;
        result.low[a]=middle-extent;result.high[a]=middle+extent;}
    if(variant){const float height=std::max(std::fabs(result.low[1]-instance.row[1][3]),std::fabs(result.high[1]-instance.row[1][3]));
        for(unsigned a=0;a<3;a+=2){const float pad=std::fabs(wind[variant][a])*height*mesh.height_factor;result.low[a]-=pad;result.high[a]+=pad;}}
    // Invalid authoring/wind data must never poison a merged node and
    // make another instance disappear. Preserve it as an unbounded leaf.
    for(unsigned a=0;a<3;++a)if(!Finite(result.low[a]) || !Finite(result.high[a]) || result.low[a]>result.high[a])return result;
    result.known=true;return result;
}
// EXE-side provenance only. A cache slot remains the same spatial cell while
// fade/SSA/variant changes repack its accepted instances into different groups.
// sx/sz distinguish a recycled cache slot; no item pointer crosses the frame.
struct ShadowCell {
    int32_t sx,sz;float center[3];bool ready;
    ShadowCell():sx(0),sz(0),ready(false){center[0]=center[1]=center[2]=0;}
};
struct Frame {
    uint64_t generation;
    uint32_t engine_frame;
    bool source_frame_known; // Diagnostic validity, independent of frame zero and wrap.
    std::vector<Mesh> meshes;
    std::vector<Group> groups;
    std::vector<KichR4DetailInstance> instances;
    /* Independent candidates from ready cache cells, before the camera frustum.
     * The camera arrays above preserve the original camera fade/SSA choices. */
    std::vector<Group> shadow_groups;
    std::vector<KichR4DetailInstance> shadow_instances;
    std::vector<ShadowCell> shadow_cells;
    std::vector<uint32_t> shadow_instance_cells;
    // P0-S11 PERF02: InstanceBounds of each shadow instance, computed by the
    // assembly threads; another size than shadow_instances means none.
    std::vector<Bounds> shadow_bounds;
    float wave[3][4],wind[3][4];
    // P0-S8: metres within which grass casts shadows (r__detail_radius); 0 = unknown.
    float shadow_radius;
    uint32_t variants[3];
    uint32_t legacy_draws;
    uint64_t legacy_vertices,legacy_indices;
    double prepare_ms;
    Frame():generation(0),engine_frame(0),legacy_draws(0),legacy_vertices(0),legacy_indices(0),prepare_ms(0){ClearFrame(0);source_frame_known=false;}
    void ClearFrame(uint32_t frame){if(engine_frame!=frame){legacy_draws=0;legacy_vertices=legacy_indices=0;}
        engine_frame=frame;source_frame_known=true;groups.clear();instances.clear();shadow_groups.clear();shadow_instances.clear();shadow_cells.clear();shadow_instance_cells.clear();shadow_bounds.clear();prepare_ms=0;shadow_radius=0;
        for(unsigned i=0;i<3;++i){variants[i]=0;for(unsigned j=0;j<4;++j)wave[i][j]=wind[i][j]=0;}}
    void Reset(){meshes.clear();ClearFrame(0);legacy_draws=0;legacy_vertices=legacy_indices=0;++generation;source_frame_known=false;}
    // P0-S11 PERF02: Finite (finite and |v| < 1e20) as one integer compare of
    // the IEEE magnitude bits per value; NaN and infinity have larger patterns.
    // MSVC std::isfinite is a function call, sixteen of them per instance.
    static bool FiniteBits(const float* values,unsigned count){uint32_t largest=0;
        for(unsigned i=0;i<count;++i){uint32_t u;memcpy(&u,values+i,4);u&=0x7fffffffu;largest=u>largest?u:largest;}
        return largest<FiniteLimitBits;}
    static const uint32_t FiniteLimitBits=0x60AD78ECu; // the bits of 1.e20f
    // The reason an instance is rejected, or NULL.
    static const char* Invalid(const KichR4DetailInstance& v){
        if(!FiniteBits(&v.row[0][0],12))return "nonfinite detail transform";
        if(!FiniteBits(v.lighting,4))return "nonfinite detail lighting";
        return NULL;}
    static void Validate(const KichR4DetailInstance& v){if(const char* reason=Invalid(v))throw std::runtime_error(reason);}
    void Append(const KichR4DetailInstance& v){if(instances.size()>=KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail instance safety limit");Validate(v);instances.push_back(v);}
    void AppendShadow(const KichR4DetailInstance& v,uint32_t cell=UINT32_MAX){if(shadow_instances.size()>=KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail shadow candidate safety limit");Validate(v);shadow_instances.push_back(v);shadow_instance_cells.push_back(cell);}
};
}
namespace kich_r4_detail {
/* P0-S11 PERF02: Details->Render only defers the frame; its instances are
 * published here, at their first reader (texture preflight's detail block,
 * or Draw), so the parallel assembly overlaps the frame plan and preflight.
 * The native frontend installs the hook; elsewhere it is a no-op. */
typedef void (*FinishHook)();
inline FinishHook& FinishFrameHook(){static FinishHook hook=NULL;return hook;}
inline void FinishFrame(){if(FinishFrameHook())FinishFrameHook()();}
}
/* EXE-only frontend store. Contains compact instances, never expanded or
 * world-transformed copies of every mesh vertex. */
kich_r4_detail::Frame& KichR4_DetailFrame();
class CDetailManager;
void KichR4_DetailLoad(CDetailManager&);
void KichR4_DetailUnload();
void KichR4_DetailPrepare(CDetailManager&); // publish or assemble now
void KichR4_DetailDefer(CDetailManager&);   // Details->Render: publish at the first reader
void KichR4_DetailFinish();                 // that first reader (FinishFrame)
#endif
