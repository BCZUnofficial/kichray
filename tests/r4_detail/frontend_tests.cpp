#include "detail_engine.h"
#include "../../xr_3da/KichR4DetailData.h"
#include <limits>
DetailDevice Device={};
DetailPersistent persistent={{.5f}};DetailPersistent* g_pGamePersistent=&persistent;
float ps_r__Detail_radius=50.f,r_ssaDISCARD=1.e-6f;
int ps_r4_detail_threads=3;
#include "frontend_under_test.cpp"
static unsigned checks=0;
void RequireAt(bool good,int line){++checks;if(!good)throw std::runtime_error("detail frontend check failed at line "+std::to_string(line));}
#define Require(condition) RequireAt((condition),__LINE__)
// P0-S11 PERF02: the bit test equals Finite (finite and |v| < 1e20) everywhere.
void FiniteBitsMatchFinite(){
    float limit=1.e20f;uint32_t bits;memcpy(&bits,&limit,4);Require(bits==kich_r4_detail::Frame::FiniteLimitBits);
    const float specials[]={0.f,-0.f,1.f,-1.f,1.e20f,-1.e20f,std::nextafter(1.e20f,0.f),-std::nextafter(1.e20f,0.f),3.e38f,1.e-45f,
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),-std::numeric_limits<float>::quiet_NaN()};
    for(float v:specials)Require(kich_r4_detail::Frame::FiniteBits(&v,1)==kich_r4_detail::Finite(v));
    uint32_t seed=7;for(unsigned i=0;i<200000;++i){seed=seed*1664525u+1013904223u;float v;memcpy(&v,&seed,4);Require(kich_r4_detail::Frame::FiniteBits(&v,1)==kich_r4_detail::Finite(v));}
    float row[4]={1,2,3,4};Require(kich_r4_detail::Frame::FiniteBits(row,4));row[3]=std::numeric_limits<float>::quiet_NaN();Require(!kich_r4_detail::Frame::FiniteBits(row,4));
}
// P0-S11 PERF02: the instance assembly on the engine's parallel thread
// publishes exactly the frame the render thread would have assembled.
void ParallelMatchesRenderThread(){
    CDetail::fvfVertexIn vertices[3]={{{0,0,0},0,0},{{1,2,0},1,1},{{0,1,1},0,.5f}};uint16_t indices[3]={0,1,2};
    CDetail mesh={vertices,indices,3,3,{{0,0,0},{1,2,1}},NULL,{2}};CDetailManager manager;manager.objects.push_back(&mesh);
    manager.swing_desc[0]={8,16,.05f,.1f,1};manager.swing_desc[1]={12,24,.15f,.3f,1};manager.swing_current.lerp(manager.swing_desc[0],manager.swing_desc[1],.5f);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i].resize(1);
    CDetailManager::SlotItem item={2,{1,0,0,0,0,1,0,0,0,0,1,0,4,5,6,1},.25f,.75f,3,2},behind=item;behind.mRotY._43=-6;
    CDetailManager::SlotItemVec items;items.push_back(&item);
    auto& cell=manager.cache_pool[0];cell.empty=false;cell.sx=-1;cell.sz=-2;cell.G[0].id=0;cell.G[0].items.push_back(&behind);cell.vis.sphere.P={0,0,-5};
    KichR4_DetailLoad(manager);auto& frame=KichR4_DetailFrame();Device.fTimeGlobal=23;
    auto fill=[&]{for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].push_back(&items);};
    fill();Device.dwFrame=500;KichR4_DetailPrepare(manager);
    const auto instances=frame.instances;const auto shadows=frame.shadow_instances;const auto groups=frame.groups.size();const auto cells=frame.shadow_instance_cells;
    float wind[3][4];memcpy(wind,frame.wind,sizeof(wind));
    Require(!instances.empty() && !shadows.empty());
    // Next frame: the parallel thread runs after MT_CALC, then the render thread publishes.
    fill();Device.dwFrame=501;manager.m_frame_calc=501;manager.m_frame_rendered=500;
    auto job=KichR4_DetailParallel(&manager);job();
    Require(frame.instances.size()==instances.size() && manager.m_visibles[0][0].empty()); // the job consumed the lists, published nothing yet
    Device.fTimeGlobal=23;frame.ClearFrame(501);KichR4_DetailPrepare(manager);
    Require(frame.engine_frame==501 && frame.instances.size()==instances.size() && frame.groups.size()==groups && frame.shadow_instance_cells==cells);
    Require(!memcmp(frame.instances.data(),instances.data(),instances.size()*sizeof(instances[0])) && !memcmp(frame.shadow_instances.data(),shadows.data(),shadows.size()*sizeof(shadows[0])));
    Require(!memcmp(frame.wind,wind,sizeof(wind))); // swing lerped from the environment, as Render sets swing_current
    // A late job for a frame the render thread already prepared does nothing.
    fill();Device.dwFrame=502;manager.m_frame_calc=502;manager.m_frame_rendered=501;KichR4_DetailPrepare(manager);
    const size_t prepared=frame.instances.size();fill();job();Require(manager.m_visibles[0][0].size()==1 && frame.instances.size()==prepared);
    manager.m_visibles[0][0].clear();manager.m_visibles[1][0].clear();manager.m_visibles[2][0].clear();
    // No MT_CALC for this frame on the parallel thread: the job leaves it to Render.
    fill();Device.dwFrame=503;manager.m_frame_calc=502;job();Require(manager.m_visibles[0][0].size()==1);
    KichR4_DetailPrepare(manager);Require(frame.engine_frame==503 && frame.instances.size()==instances.size());
    // A failure on the parallel thread surfaces on the render thread.
    auto bad=item;bad.mRotY._11=std::numeric_limits<float>::quiet_NaN();items[0]=&bad;
    fill();Device.dwFrame=504;manager.m_frame_calc=504;manager.m_frame_rendered=503;job();
    bool rejected=false;try{KichR4_DetailPrepare(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].clear(); // the failed assembly stopped before consuming them
    // A job that starts after Details->Render deferred the frame still assembles it; the first reader publishes it.
    items[0]=&item;fill();Device.dwFrame=506;manager.m_frame_calc=506;KichR4_DetailDefer(manager);manager.m_frame_rendered=506;job();
    Require(manager.m_visibles[0][0].empty());kich_r4_detail::FinishFrame();
    Require(frame.engine_frame==506 && frame.instances.size()==instances.size());
    // Rendered without a deferral: the job leaves the frame to Render.
    fill();Device.dwFrame=507;manager.m_frame_calc=507;manager.m_frame_rendered=507;job();Require(manager.m_visibles[0][0].size()==1);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].clear();items[0]=&bad;
    KichR4_DetailParallelStop();
    // Details->Render defers; the first reader publishes once, the shadow radius is known at once.
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].clear();
    items[0]=&item;fill();Device.dwFrame=520;frame.ClearFrame(520);KichR4_DetailDefer(manager);
    Require(frame.instances.empty() && frame.shadow_radius==50.f && manager.m_visibles[0][0].size()==1);
    kich_r4_detail::FinishFrame();Require(frame.instances.size()==instances.size() && manager.m_visibles[0][0].empty());
    const size_t published=frame.instances.size();fill();kich_r4_detail::FinishFrame();Require(frame.instances.size()==published && manager.m_visibles[0][0].size()==1);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].clear();items[0]=&bad;
    fill();Device.dwFrame=505;manager.m_frame_calc=505;manager.m_frame_rendered=504;job();Require(manager.m_visibles[0][0].size()==1); // stopped: no manager
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].clear();
    KichR4_DetailUnload();
}
// P0-S11 PERF02: the R4 helper threads assemble exactly the serial frame,
// shadow bounds included, for every thread count; a bad instance anywhere
// still fails the frame.
void ThreadsMatchSerial(){
    const int saved_threads=ps_r4_detail_threads;
    CDetail::fvfVertexIn vertices[3]={{{0,0,0},0,0},{{1,2,0},1,1},{{0,1,1},0,.5f}};uint16_t indices[3]={0,1,2};
    CDetail mesh={vertices,indices,3,3,{{0,0,0},{1,2,1}},NULL,{2}},other=mesh;CDetailManager manager;
    manager.objects.push_back(&mesh);manager.objects.push_back(&other);manager.swing_current={10,20,.1f,.2f,1};
    for(unsigned i=0;i<3;++i)manager.m_visibles[i].resize(2);
    std::vector<CDetailManager::SlotItem> items(400);
    for(size_t i=0;i<items.size();++i){const float x=float(i%13)-6,z=float(i%17)-8;
        items[i]={.5f+.001f*i,{1,0,0,0,0,1,0,0,0,0,1,0,x,.1f*float(i%5),z,1},.25f,.75f,1+.002f*i,u32(i%3)};}
    for(unsigned cell=0;cell<dm_cache_size;++cell){auto& slot=manager.cache_pool[cell];slot.empty=false;slot.sx=int(cell%3)-1;slot.sz=int(cell/3)-1;
        slot.vis.sphere.P={float(cell%3)*6-6,0,float(cell/3)*6-6};
        for(unsigned part=0;part<2;++part){slot.G[part].id=part;slot.G[part].items.clear();
            for(size_t i=0;i<20;++i)slot.G[part].items.push_back(&items[(cell*40+part*20+i)%items.size()]);
            for(unsigned v=0;v<3;++v)slot.G[part].r_items[v].clear();
            if((cell+part)%3)slot.G[part].r_items[(cell+part)%3].assign(slot.G[part].items.begin(),slot.G[part].items.begin()+5);}}
    KichR4_DetailLoad(manager);auto& frame=KichR4_DetailFrame();Device.fTimeGlobal=11;Device.vCameraPosition={0,0,0};
    auto fill=[&]{for(unsigned cell=0;cell<dm_cache_size;++cell)for(unsigned part=0;part<2;++part)for(unsigned v=0;v<3;++v){
        auto& list=manager.cache_pool[cell].G[part].r_items[v];if(!list.empty())manager.m_visibles[v][part].push_back(&list);}};
    struct Copy {std::vector<kich_r4_detail::Group> groups,shadow_groups;std::vector<KichR4DetailInstance> instances,shadow;std::vector<uint32_t> cells;std::vector<kich_r4_detail::Bounds> bounds;};
    auto take=[&]{Copy c={frame.groups,frame.shadow_groups,frame.instances,frame.shadow_instances,frame.shadow_instance_cells,frame.shadow_bounds};return c;};
    ps_r4_detail_threads=0;fill();Device.dwFrame=700;KichR4_DetailPrepare(manager);const Copy serial=take();
    Require(!serial.instances.empty() && !serial.shadow.empty() && serial.bounds.size()==serial.shadow.size() && serial.groups.size()>2 && serial.shadow_groups.size()>2);
    for(const auto& g:serial.shadow_groups)for(uint32_t i=0;i<g.count;++i){
        const auto expected=kich_r4_detail::InstanceBounds(frame.meshes[g.mesh],serial.shadow[g.first+i],g.variant,frame.wind);const auto& got=serial.bounds[g.first+i];
        Require(got.known==expected.known && !memcmp(got.low,expected.low,sizeof(got.low)) && !memcmp(got.high,expected.high,sizeof(got.high)));}
    for(int threads=1;threads<=8;threads+=threads<4?1:4){
        ps_r4_detail_threads=threads;fill();KichR4_DetailPrepare(manager);const Copy threaded=take();
        Require(threaded.groups.size()==serial.groups.size() && !memcmp(threaded.groups.data(),serial.groups.data(),serial.groups.size()*sizeof(serial.groups[0])));
        Require(threaded.shadow_groups.size()==serial.shadow_groups.size() && !memcmp(threaded.shadow_groups.data(),serial.shadow_groups.data(),serial.shadow_groups.size()*sizeof(serial.shadow_groups[0])));
        Require(threaded.instances.size()==serial.instances.size() && !memcmp(threaded.instances.data(),serial.instances.data(),serial.instances.size()*sizeof(serial.instances[0])));
        Require(threaded.shadow.size()==serial.shadow.size() && !memcmp(threaded.shadow.data(),serial.shadow.data(),serial.shadow.size()*sizeof(serial.shadow[0])));
        Require(threaded.cells==serial.cells && threaded.bounds.size()==serial.bounds.size());
        for(size_t i=0;i<serial.bounds.size();++i)Require(threaded.bounds[i].known==serial.bounds[i].known &&
            !memcmp(threaded.bounds[i].low,serial.bounds[i].low,sizeof(serial.bounds[i].low)) && !memcmp(threaded.bounds[i].high,serial.bounds[i].high,sizeof(serial.bounds[i].high)));
        for(unsigned v=0;v<3;++v)for(unsigned part=0;part<2;++part)Require(manager.m_visibles[v][part].empty());
    }
    // A nonfinite shadow-only item (off camera) fails the frame on any thread.
    ps_r4_detail_threads=3;const auto saved=items[10];items[10].mRotY._11=std::numeric_limits<float>::quiet_NaN(); // cell 0, part 0: off camera
    bool rejected=false;fill();try{KichR4_DetailPrepare(manager);}catch(const std::exception& e){rejected=std::string(e.what())=="nonfinite detail transform";}
    Require(rejected && frame.instances.empty() && frame.shadow_instances.empty() && frame.shadow_bounds.empty());
    items[10]=saved;for(unsigned v=0;v<3;++v)for(unsigned part=0;part<2;++part)manager.m_visibles[v][part].clear();
    for(unsigned cell=0;cell<dm_cache_size;++cell){manager.cache_pool[cell]=CDetailManager::Slot();}
    KichR4_DetailUnload();ps_r4_detail_threads=saved_threads;
}
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
    // P0-S5: past the shadow radius (r__detail_radius) grass casts no shadow,
    // even a cached part the camera still draws (its caster faded out first).
    manager.cache_pool[0].vis.sphere.P.z=61;manager.cache_pool[1].vis.sphere.P.z=-61;
    manager.m_visibles[2][0].push_back(&part.r_items[2]);KichR4_DetailPrepare(manager);
    Require(frame.instances.size()==1 && frame.shadow_instances.empty());
    // Leaving the camera list must not leave a stale pointer/pose override.
    manager.cache_pool[0].vis.sphere.P.z=20;KichR4_DetailPrepare(manager);
    Require(frame.instances.empty() && frame.shadow_instances.size()==1 && frame.shadow_groups[0].variant==0);
    Require(std::fabs(frame.shadow_instances[0].row[1][1]-(1.f-399.f/3599))<1.e-6f);
    KichR4_DetailUnload();ps_r__Detail_radius=saved_radius;r_ssaDISCARD=saved_discard;
}
// P0-S8: flat clutter (local height < 10 cm) stays visible but never becomes a
// shadow candidate: no bounds, BVH, selection or upload for any shadow view.
void FlatClutterCastsNothing(){
    CDetail::fvfVertexIn vertices[3]={{{0,0,0},0,0},{{1,.03f,0},1,1},{{0,0,1},0,.5f}};uint16_t indices[3]={0,1,2};
    CDetail mesh={vertices,indices,3,3,{{0,0,0},{1,.03f,1}},NULL,{1}};CDetailManager manager;
    manager.objects.push_back(&mesh);manager.swing_current={10,20,.1f,.2f,1};for(unsigned i=0;i<3;++i)manager.m_visibles[i].resize(1);
    CDetailManager::SlotItem item={1,{1,0,0,0,0,1,0,0,0,0,1,0,1,0,1,1},.5f,.5f,1,0};
    CDetailManager::Slot& cell=manager.cache_pool[4];cell.empty=false;cell.G[0].id=0;cell.G[0].items.push_back(&item);cell.G[0].r_items[0].push_back(&item);
    KichR4_DetailLoad(manager);auto& frame=KichR4_DetailFrame();
    manager.m_visibles[0][0].push_back(&cell.G[0].r_items[0]);Device.dwFrame=900;KichR4_DetailPrepare(manager);
    Require(frame.instances.size()==1 && frame.shadow_instances.empty() && frame.shadow_groups.empty() && frame.shadow_cells[4].ready);
    KichR4_DetailUnload();
}
int main(){try{
    FlatClutterCastsNothing();
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
    // A recycled slot (new coordinates within the shadow square, P0-S5) keeps its cell ID.
    r_ssaDISCARD=old_discard;manager.cache_pool[0].sx=17;manager.cache_pool[0].sz=-11;
    KichR4_DetailPrepare(manager);Require(frame.shadow_cells[0].sx==17 && frame.shadow_cells[0].sz==-11 && frame.shadow_instance_cells[0]==0);
    // P0-S8: slots outside the shadow square are not ready and give no casters.
    manager.cache_pool[0].sx=37;KichR4_DetailPrepare(manager);Require(!frame.shadow_cells[0].ready && frame.shadow_instances.empty());
    manager.cache_pool[0].sx=17;
    manager.cache_pool[0].empty=true;KichR4_DetailPrepare(manager);Require(frame.shadow_instances.empty() && frame.shadow_groups.empty());
    bool rejected=false;try{auto bad=item;bad.mRotY._11=std::numeric_limits<float>::quiet_NaN();items[0]=&bad;manager.m_visibles[0][0].push_back(&items);KichR4_DetailPrepare(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    const auto generation=frame.generation;KichR4_DetailUnload();Require(frame.meshes.empty() && frame.instances.empty() && frame.shadow_cells.empty() && frame.shadow_instance_cells.empty() && frame.generation==generation+1);
    mesh.bv_bb.max.y=0;mesh.bv_bb.min.y=0;KichR4_DetailLoad(manager);Require(frame.meshes[0].vertices[2].misc[2]==0);
    indices[2]=99;rejected=false;try{KichR4_DetailLoad(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    CoherentCachedPose();ParallelMatchesRenderThread();ThreadsMatchSerial();FiniteBitsMatchFinite();printf("G3b real detail frontend: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
