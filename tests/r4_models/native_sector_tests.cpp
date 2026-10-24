#include "../../xr_3da/KichR4LevelVisibility.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
typedef uint32_t u32;
const float EPS=.00001f;
bool _valid(float x){return std::isfinite(x);}
struct Fvector {float x,y,z;void set(float a,float b,float c){x=a;y=b;z=c;}};
namespace CDB {
enum {OPT_ONLYNEAREST=4};
struct RESULT {int id;float range;};
struct MODEL {};
struct TRI {u32 sector;};
struct COLLIDER {
    unsigned query_count=0;bool hit=false;float down=-1,up=-1;u32 options=0;RESULT result={0,0};
    void ray_options(u32 value){options=value;}
    void ray_query(const MODEL*,const Fvector&,const Fvector& direction,float range){
        ++query_count;result.range=direction.y<0?down:up;hit=result.range>=0 && result.range<=range;
    }
    unsigned r_count()const{return hit?1:0;}
    const RESULT* r_begin()const{return &result;}
};
}
struct Space {
    CDB::MODEL model;CDB::TRI triangle={0};
    const CDB::MODEL* GetStaticModel()const{return &model;}
    CDB::TRI* GetStaticTris(){return &triangle;}
};
struct Level {Space ObjectSpace;};
Level level;Level* g_pGameLevel=&level;
kich_r4_level::Topology native_topology;
CDB::COLLIDER native_sector_collider;
#include "native_sector_under_test.cpp"
int main(){
    using namespace kich_r4_level;
    native_topology.valid=true;native_topology.sectors.resize(2);
    Portal portal;portal.front=0;portal.back=1;portal.normal={0,1,0};portal.distance=0;
    portal.vertices={{-1,0,-1},{-1,0,1},{1,0,1},{1,0,-1}};
    native_topology.portals.push_back(portal);
    const float above[3]={0,2,0},below[3]={0,-2,0},doorway[3]={0,.01f,0};
    // The nearest portal wins over a floor in another sector; CFORM wins when
    // an actual surface precedes that portal (as in the R2 probe).
    native_sector_collider.down=4;level.ObjectSpace.triangle.sector=1;
    assert(KichR4_NativeSector(above)==0 && native_sector_collider.options==CDB::OPT_ONLYNEAREST);
    native_sector_collider.down=1;
    assert(KichR4_NativeSector(above)==1);
    native_sector_collider.down=-1;native_sector_collider.up=-1;
    assert(KichR4_NativeSector(below)==1);
    // Never guess past a straddled portal, malformed topology or invalid hit.
    const unsigned queries=native_sector_collider.query_count;
    assert(KichR4_NativeSector(doorway)==UINT32_MAX && native_sector_collider.query_count==queries);
    native_topology.portals.clear();native_sector_collider.up=3;level.ObjectSpace.triangle.sector=0;
    assert(KichR4_NativeSector(above)==0);
    level.ObjectSpace.triangle.sector=5;
    assert(KichR4_NativeSector(above)==UINT32_MAX);
    native_sector_collider.up=-1;
    assert(KichR4_NativeSector(above)==UINT32_MAX);
    native_topology.valid=false;assert(KichR4_NativeSector(above)==UINT32_MAX);
    native_topology.valid=true;g_pGameLevel=NULL;assert(KichR4_NativeSector(above)==UINT32_MAX);
    g_pGameLevel=&level;assert(KichR4_NativeSector(NULL)==UINT32_MAX);
    const float invalid[3]={std::numeric_limits<float>::quiet_NaN(),0,0};
    assert(KichR4_NativeSector(invalid)==UINT32_MAX);
    std::puts("E1.2 production native sector probe: 11 checks passed");
}
