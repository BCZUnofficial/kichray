#include "detail_engine.h"
#include "../../xr_3da/KichR4DetailData.h"
#include <limits>
DetailDevice Device={};
#include "frontend_under_test.cpp"
static unsigned checks=0;
void Require(bool good){++checks;if(!good)throw std::runtime_error("detail frontend check failed");}
int main(){try{
    CDetail::fvfVertexIn vertices[3]={{{0,0,0},-.1f,0},{{1,2,0},1,1},{{0,1,1},0,.5f}};uint16_t indices[3]={0,1,2};
    Texture texture={"grass\\test"};VS vs={"deffer_detail_w_flat"};STextureList textures={{0,&texture}};SPass pass={&vs,&textures};Element e;e.passes.push_back(&pass);Shader shader={{&e,&e}};
    CDetail mesh={vertices,indices,3,3,{{0,0,0},{1,2,1}},&shader};CDetailManager manager;manager.objects.push_back(&mesh);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i].resize(1);
    manager.swing_current={10,20,.1f,.2f,1};KichR4_DetailLoad(manager);auto& frame=KichR4_DetailFrame();
    Require(frame.meshes.size()==1 && frame.meshes[0].vertices.size()==3 && frame.meshes[0].indices.size()==3);
    Require(frame.meshes[0].texture==texture.cName && frame.meshes[0].shader==vs.cName);
    Require(frame.meshes[0].vertices[0].misc[0]==std::floor(-.1f*16384.f)/16384.f);
    Require(frame.meshes[0].vertices[1].misc[2]==1);
    CDetailManager::SlotItem item={2,{1,0,0,0,0,1,0,0,0,0,1,0,4,5,6,1},.25f,.75f};
    CDetailManager::SlotItemVec items;items.push_back(&item);
    for(unsigned i=0;i<3;++i)manager.m_visibles[i][0].push_back(&items);
    Device.dwFrame=123;Device.fTimeGlobal=17;KichR4_DetailPrepare(manager);
    Require(frame.engine_frame==123 && frame.instances.size()==3 && frame.groups.size()==3);
    for(unsigned i=0;i<3;++i){Require(frame.variants[i]==1 && frame.groups[i].first==i && frame.groups[i].variant==i);
        Require(frame.instances[i].row[0][0]==2 && frame.instances[i].row[0][3]==4 && frame.instances[i].row[1][3]==5);
        Require(frame.instances[i].lighting[0]==.25f && frame.instances[i].lighting[1]==.75f);
        Require(manager.m_visibles[i][0].empty());}
    Require(frame.wave[0][0]==0 && frame.wind[0][0]==0);
    Require(std::fabs(frame.wave[1][0]-(1.f/5)/PI_MUL_2)<1.e-8f && std::fabs(frame.wave[2][0]-(1.f/3)/PI_MUL_2)<1.e-8f);
    Require(std::fabs(frame.wind[1][0]-std::sin(PI_MUL_2*17/10)*.1f)<1.e-7f);
    Require(std::fabs(frame.wind[2][2]-std::cos(PI_MUL_2*17/20)*.2f)<1.e-7f);
    KichR4_DetailPrepare(manager);Require(frame.instances.empty() && frame.groups.empty());
    bool rejected=false;try{auto bad=item;bad.mRotY._11=std::numeric_limits<float>::quiet_NaN();items[0]=&bad;manager.m_visibles[0][0].push_back(&items);KichR4_DetailPrepare(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    const auto generation=frame.generation;KichR4_DetailUnload();Require(frame.meshes.empty() && frame.instances.empty() && frame.generation==generation+1);
    mesh.bv_bb.max.y=0;mesh.bv_bb.min.y=0;KichR4_DetailLoad(manager);Require(frame.meshes[0].vertices[2].misc[2]==0);
    indices[2]=99;rejected=false;try{KichR4_DetailLoad(manager);}catch(const std::exception&){rejected=true;}Require(rejected);
    printf("G3a real frontend: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
