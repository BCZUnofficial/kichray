#include "detail_engine.h"
#include "../../xr_3da/KichR4DetailData.h"
#include <limits>
DetailDevice Device={};
float ps_r__Detail_radius=50.f,r_ssaDISCARD=1.e-6f;
#include "frontend_under_test.cpp"
static unsigned checks=0;
void Require(bool good){++checks;if(!good)throw std::runtime_error("detail frontend check failed");}
void CoherentCachedPose(){
    const float saved_radius=ps_r__Detail_radius,saved_discard=r_ssaDISCARD;
    ps_r__Detail_radius=60;r_ssaDISCARD=.0005f;
    CDetail::fvfVertexIn vertices[3]={{{0,0,0},0,0},{{1,2,0},1,1},{{0,1,1},0,.5f}};uint16_t indices[3]={0,1,2};
    CDetail mesh={vertices,indices,3,3,{{0,0,0},{1,2,1}},NULL,{2}};CDetailManager manager;
    manager.objects.push_back(&mesh);manager.swing_current={10,20,.1f,.2f,1};
    for(unsigned i=0;i<3;++i)manager.m_visibles[i].resize(1);
    const float cached_distance=18.5f,cached_scale=1-(cached_distance*cached_distance-1)/3599;
    CDetailManager::SlotItem visible={cached_scale,{1,0,0,0,0,1,0,0,0,0,1,0,0,0,20,1},.5f,.75f,1,2};
    CDetailManager::SlotItem offcamera=visible;offcamera.mRotY._41=10;offcamera.mRotY._43=-20;
    for(unsigned i=0;i<2;++i){auto& cell=manager.cache_pool[i];cell.empty=false;cell.vis.sphere.P={0,0,i?-20.f:20.f};cell.G[0].id=0;}
    auto& part=manager.cache_pool[0].G[0];part.items.push_back(&visible);part.r_items[2].push_back(&visible);
    manager.cache_pool[1].G[0].items.push_back(&offcamera);
    KichR4_DetailLoad(manager);auto& frame=KichR4_DetailFrame();Device.fTimeGlobal=17;
    // Reproduce the existing cache cadence while moving through the SSA
    // wind/still boundary. Before E1.9 the camera kept variant 2 but its shadow
    // immediately used variant 0 and a smaller, independently computed scale.
    for(unsigned step=0;step<20;++step){
        manager.cache_pool[0].vis.sphere.P.z=20.f+step*.1f;
        manager.m_visibles[2][0].push_back(&part.r_items[2]);Device.dwFrame=300+step;KichR4_DetailPrepare(manager);
        Require(frame.instances.size()==1 && frame.shadow_instances.size()==2);
        bool matched=false,independent=false;
        for(const auto& group:frame.shadow_groups)for(uint32_t i=0;i<group.count;++i){const auto& shadow=frame.shadow_instances[group.first+i];
            if(shadow.row[0][3]==visible.mRotY._41){matched=true;Require(group.variant==frame.groups[0].variant);
                Require(memcmp(&shadow,&frame.instances[0],sizeof(shadow))==0);}
            else{independent=true;Require(group.variant==0);Require(std::fabs(shadow.row[1][1]-(1.f-399.f/3599))<1.e-6f);}}
        Require(matched && independent);
    }
    // A still-drawn cached part can briefly extend beyond the fresh radius.
    // Keep its matching caster, without extending off-camera admission.
    manager.cache_pool[0].vis.sphere.P.z=61;manager.cache_pool[1].vis.sphere.P.z=-61;
    manager.m_visibles[2][0].push_back(&part.r_items[2]);KichR4_DetailPrepare(manager);
    Require(frame.instances.size()==1 && frame.shadow_instances.size()==1 && frame.shadow_groups[0].variant==2);
    Require(memcmp(&frame.instances[0],&frame.shadow_instances[0],sizeof(frame.instances[0]))==0);
    // Leaving the camera list must not leave a stale pointer/pose override.
    manager.cache_pool[0].vis.sphere.P.z=20;KichR4_DetailPrepare(manager);
    Require(frame.instances.empty() && frame.shadow_instances.size()==1 && frame.shadow_groups[0].variant==0);
    Require(std::fabs(frame.shadow_instances[0].row[1][1]-(1.f-399.f/3599))<1.e-6f);
    KichR4_DetailUnload();ps_r__Detail_radius=saved_radius;r_ssaDISCARD=saved_discard;
}
int main(){try{
    CDetail::fvfVertexIn vertices[3]={{{0,0,0},-.1f,0},{{1,2,0},1,1},{{0,1,1},0,.5f}};uint16_t indices[3]={0,1,2};
    Texture texture={"grass\\test"};VS vs={"deffer_detail_w_flat"};STextureList textures={{0,&texture}};SPass pass={&vs,&textures};Element e;e.passes.push_back(&pass);Shader shader={{&e,&e}};
    CDetail mesh={vertices,indices,3,3,{{0,0,0},{1,2,1}},&shader,{2}};CDetailManager manager;manager.objects.push_back(&mesh);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i].resize(1);
    manager.swing_current={10,20,.1f,.2f,1};KichR4_DetailLoad(manager);auto& frame=KichR4_DetailFrame();
    Require(frame.meshes.size()==1 && frame.meshes[0].vertices.size()==3 && frame.meshes[0].indices.size()==3);
    Require(frame.meshes[0].texture==texture.cName && frame.meshes[0].shader==vs.cName);
    Require(frame.meshes[0].vertices[0].misc[0]==std::floor(-.1f*16384.f)/16384.f);
    Require(frame.meshes[0].vertices[1].misc[2]==1);
    Require(frame.meshes[0].bounds_known && frame.meshes[0].maximum[1]==2 && frame.meshes[0].height_factor==1);
    CDetailManager::SlotItem item={2,{1,0,0,0,0,1,0,0,0,0,1,0,4,5,6,1},.25f,.75f,3,2};
    CDetailManager::SlotItemVec items;items.push_back(&item);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].push_back(&items);
    Device.dwFrame=123;Device.fTimeGlobal=17;KichR4_DetailPrepare(manager);
    Require(frame.engine_frame==123 && frame.instances.size()==3 && frame.groups.size()==3);
    Require(frame.prepare_ms==0); // log_record OFF also gates frontend timing.
    for(unsigned i=0;i<3;++i){Require(frame.variants[i]==1 && frame.groups[i].first==i && frame.groups[i].variant==i);
        Require(frame.instances[i].row[0][0]==2 && frame.instances[i].row[0][3]==4 && frame.instances[i].row[1][3]==5);
        Require(frame.instances[i].lighting[0]==.25f && frame.instances[i].lighting[1]==.75f);
        Require(manager.m_visibles[i][0].empty());}
    Require(frame.wave[0][0]==0 && frame.wind[0][0]==0);
    Require(std::fabs(frame.wave[1][0]-(1.f/5)/PI_MUL_2)<1.e-8f && std::fabs(frame.wave[2][0]-(1.f/3)/PI_MUL_2)<1.e-8f);
    Require(std::fabs(frame.wind[1][0]-std::sin(PI_MUL_2*17/10)*.1f)<1.e-7f);
    Require(std::fabs(frame.wind[2][2]-std::cos(PI_MUL_2*17/20)*.2f)<1.e-7f);
    KichR4_DetailPrepare(manager);Require(frame.instances.empty() && frame.groups.empty());
    // Original cache items behind the camera remain eligible for a light even
    // when the camera lists are empty. Pending and out-of-radius cells do not.
    for(unsigned cell=0;cell<3;++cell){auto& slot=manager.cache_pool[cell];slot.empty=false;slot.sx=int(cell)-2;slot.sz=-3;slot.G[0].id=0;slot.G[0].items.push_back(&item);slot.vis.sphere.P={0,0,-5};}
    manager.cache_pool[1].type=CDetailManager::stPending;manager.cache_pool[2].vis.sphere.P.z=-51;
    KichR4_DetailPrepare(manager);
    Require(frame.instances.empty() && frame.shadow_instances.size()==1 && frame.shadow_groups.size()==1);
    Require(frame.shadow_cells.size()==dm_cache_size && frame.shadow_instance_cells.size()==1 && frame.shadow_instance_cells[0]==0);
    Require(frame.shadow_cells[0].ready && frame.shadow_cells[0].sx==-2 && frame.shadow_cells[0].sz==-3 && frame.shadow_cells[0].center[2]==-5);
    Require(!frame.shadow_cells[1].ready && frame.shadow_cells[2].ready); // Radius does not remove cache topology.
    Require(frame.shadow_groups[0].variant==2 && frame.shadow_groups[0].mesh==0);
    Require(std::fabs(frame.shadow_instances[0].row[0][0]-3.f*(1.f-24.f/2499.f))<1.e-6f);
    Require(item.scale_calculated==2 && item.scale==3 && manager.cache_pool[0].G[0].items.size()==1);
    manager.cache_pool[0].G[0].items[0]=&item;manager.cache_pool[0].vis.sphere.P={0,0,0};
    KichR4_DetailPrepare(manager);Require(frame.shadow_instances.size()==1 && frame.shadow_instances[0].row[0][0]==3);
    // Crossing an SSA wind/still threshold changes packed group, not cell ID.
    const auto old_discard=r_ssaDISCARD;r_ssaDISCARD=10.f;manager.cache_pool[0].vis.sphere.P={0,0,-1};
    KichR4_DetailPrepare(manager);Require(frame.shadow_groups.size()==1 && frame.shadow_groups[0].variant==0 && frame.shadow_instance_cells[0]==0);
    r_ssaDISCARD=old_discard;manager.cache_pool[0].sx=37;manager.cache_pool[0].sz=-11;
    KichR4_DetailPrepare(manager);Require(frame.shadow_cells[0].sx==37 && frame.shadow_cells[0].sz==-11 && frame.shadow_instance_cells[0]==0);
    manager.cache_pool[0].empty=true;KichR4_DetailPrepare(manager);Require(frame.shadow_instances.empty() && frame.shadow_groups.empty());
    bool rejected=false;try{auto bad=item;bad.mRotY._11=std::numeric_limits<float>::quiet_NaN();items[0]=&bad;manager.m_visibles[0][0].push_back(&items);KichR4_DetailPrepare(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    const auto generation=frame.generation;KichR4_DetailUnload();Require(frame.meshes.empty() && frame.instances.empty() && frame.shadow_cells.empty() && frame.shadow_instance_cells.empty() && frame.generation==generation+1);
    mesh.bv_bb.max.y=0;mesh.bv_bb.min.y=0;KichR4_DetailLoad(manager);Require(frame.meshes[0].vertices[2].misc[2]==0);
    indices[2]=99;rejected=false;try{KichR4_DetailLoad(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    CoherentCachedPose();printf("G3b real detail frontend: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
