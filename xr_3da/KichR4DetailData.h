#ifndef KICH_R4_DETAIL_DATA_H
#define KICH_R4_DETAIL_DATA_H
#include "KichR4Detail.h"
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <algorithm>
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
struct Group {uint32_t mesh,variant,first,count;};
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
    float wave[3][4],wind[3][4];
    uint32_t variants[3];
    uint32_t legacy_draws;
    uint64_t legacy_vertices,legacy_indices;
    double prepare_ms;
    Frame():generation(0),engine_frame(0),legacy_draws(0),legacy_vertices(0),legacy_indices(0),prepare_ms(0){ClearFrame(0);source_frame_known=false;}
    void ClearFrame(uint32_t frame){if(engine_frame!=frame){legacy_draws=0;legacy_vertices=legacy_indices=0;}
        engine_frame=frame;source_frame_known=true;groups.clear();instances.clear();shadow_groups.clear();shadow_instances.clear();prepare_ms=0;
        for(unsigned i=0;i<3;++i){variants[i]=0;for(unsigned j=0;j<4;++j)wave[i][j]=wind[i][j]=0;}}
    void Reset(){meshes.clear();ClearFrame(0);legacy_draws=0;legacy_vertices=legacy_indices=0;++generation;source_frame_known=false;}
    static void Validate(const KichR4DetailInstance& v){
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)if(!Finite(v.row[r][c]))throw std::runtime_error("nonfinite detail transform");
        for(unsigned c=0;c<4;++c)if(!Finite(v.lighting[c]))throw std::runtime_error("nonfinite detail lighting");}
    void Append(const KichR4DetailInstance& v){if(instances.size()>=KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail instance safety limit");Validate(v);instances.push_back(v);}
    void AppendShadow(const KichR4DetailInstance& v){if(shadow_instances.size()>=KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail shadow candidate safety limit");Validate(v);shadow_instances.push_back(v);}
};
}
/* EXE-only frontend store. Contains compact instances, never expanded or
 * world-transformed copies of every mesh vertex. */
kich_r4_detail::Frame& KichR4_DetailFrame();
class CDetailManager;
void KichR4_DetailLoad(CDetailManager&);
void KichR4_DetailUnload();
void KichR4_DetailPrepare(CDetailManager&);
#endif
