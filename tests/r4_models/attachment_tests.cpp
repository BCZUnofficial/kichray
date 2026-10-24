#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <cstdint>

typedef unsigned u32;
#define TRUE 1
#define FALSE 0
namespace {unsigned checks=0;void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}}
float _sqrt(float value){return std::sqrt(value);}
bool _valid(float value){return std::isfinite(value);}
struct Fvector {
    float x=0,y=0,z=0;
    float square_magnitude()const{return x*x+y*y+z*z;}
    float distance_to_sqr(const Fvector& p)const{return (x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z);}
};
struct Fmatrix {
    Fvector i,j,k,c;
    Fmatrix(){i.x=j.y=k.z=1;}
    void transform_tiny(Fvector& out,const Fvector& p)const{out=p;out.x+=c.x;out.y+=c.y;out.z+=c.z;}
};
struct {Fvector vCameraPosition;u32 dwTimeGlobal=0;} Device;
enum {MT_NORMAL,MT_HIERRARHY,MT_LOD,MT_SKELETON_ANIM,MT_PARTICLE_GROUP};
struct CKinematics;
struct IRender_Visual {
    u32 Type=MT_NORMAL;
    struct {struct {Fvector P;float R=1;} sphere;} vis;
    virtual ~IRender_Visual(){}
    virtual CKinematics* dcast_PKinematics(){return NULL;}
};
struct FHierrarhyVisual:IRender_Visual {std::vector<IRender_Visual*> children;FHierrarhyVisual(){Type=MT_HIERRARHY;}};
struct Item {IRender_Visual visual;Fmatrix world;float offset=0;};
struct CKinematics:FHierrarhyVisual {
    u32 calculated_at=UINT32_MAX,evaluations=0,callbacks=0,wallmarks=0;
    float bone=0,next_bone=0,owner_position=0;std::vector<Item*> attached;
    CKinematics(){Type=MT_SKELETON_ANIM;}
    CKinematics* dcast_PKinematics(){return this;}
    void CalculateBones(bool exact){
        Require(exact,"render pose requests retain R2 exactness");
        // Match SkeletonRigid's same-time guard, then a controlled bone result
        // and attachment callback. Bounds change with the evaluated pose too.
        if(calculated_at==Device.dwTimeGlobal)return;
        calculated_at=Device.dwTimeGlobal;++evaluations;bone=next_bone;vis.sphere.P.x=bone;
        ++callbacks;for(Item* item:attached)item->world.c.x=owner_position+bone+item->offset;
    }
    void CalculateWallmarks(){++wallmarks;}
};
struct ROS {virtual ~ROS(){}};
struct R4ROS:ROS {unsigned calls=0;void Exposure(float& hemi,float& sun){++calls;hemi=sun=1;}};
struct IRenderable {ROS* ros=NULL;ROS* renderable_ROS(){return ros;}};
struct CRender {
    struct Queued {IRender_Visual* visual;Fmatrix world;bool hud;float distance;bool light_valid;float hemi,sun;};
    std::vector<Queued> queue;Fmatrix transform;bool invisible=false;int val_bHUD=FALSE;IRenderable* object=NULL;
    struct {bool visible=true;bool testSphere_dirty(const Fvector&,float){return visible;}} ViewBase;
    void add_Visual(IRender_Visual*);
    void SubmitPose(IRender_Visual*,CKinematics*);
};
#include "attachment_under_test.h"

void Attachments(bool hierarchy,bool gameplay_updated,bool shadow_only){
    CRender renderer;R4ROS ros;IRenderable owner;owner.ros=&ros;renderer.object=&owner;
    renderer.ViewBase.visible=!shadow_only;
    CKinematics npc;FHierrarhyVisual wrapper;wrapper.children.push_back(&npc);
    Item items[5]; // Radio, bread, sausage, bottle and guitar share this contract.
    for(unsigned i=0;i<5;++i){items[i].offset=float(i)*.125f;npc.attached.push_back(&items[i]);}
    IRender_Visual* visual=hierarchy?static_cast<IRender_Visual*>(&wrapper):&npc;
    const u32 times[]={100,116,149,158,208};
    for(unsigned frame=0;frame<5;++frame){
        Device.dwTimeGlobal=times[frame];renderer.queue.clear();
        npc.next_bone=float(frame)*.5f-.25f;npc.owner_position=float(frame)*2;
        if(gameplay_updated)npc.CalculateBones(TRUE);
        renderer.transform.c.x=npc.owner_position;renderer.add_Visual(visual);
        Require(npc.evaluations==frame+1 && npc.callbacks==frame+1,"parent pose and attachment callbacks finish before child enqueue");
        Require(npc.wallmarks==frame,"wallmarks remain deferred until Submit");
        if(!hierarchy)Require(renderer.queue[0].distance==(npc.owner_position+npc.bone)*(npc.owner_position+npc.bone),"enqueue uses refreshed pose bounds");
        for(Item& item:items){renderer.transform=item.world;renderer.add_Visual(&item.visual);}
        renderer.SubmitPose(&npc,NULL);
        Require(npc.wallmarks==frame+1,"Submit still owns wallmarks");
        Require(npc.evaluations==frame+1 && npc.callbacks==frame+1,"deferred safety check does not evaluate pose twice");
        for(unsigned i=0;i<5;++i){
            Require(renderer.queue[i+1].world.c.x==npc.owner_position+npc.bone+items[i].offset,"held item and body use the same frame pose");
            Require(renderer.queue[i+1].hud==false,"world attachment does not become HUD geometry");
        }
    }
    Require(shadow_only?ros.calls==0:ros.calls>0,"off-camera attachment preparation does not add irradiance work");
}
void AdmissionAndNestedSkeletons(){
    Device.dwTimeGlobal=1000;CKinematics outer,inner;Item held;inner.attached.push_back(&held);
    FHierrarhyVisual lod;lod.Type=MT_LOD;lod.children.push_back(&inner);outer.children.push_back(&lod);
    inner.next_bone=4;inner.owner_position=3;CRender renderer;
    renderer.invisible=true;renderer.add_Visual(&outer);renderer.add_Visual(NULL);
    Require(outer.evaluations==0 && inner.evaluations==0 && renderer.queue.empty(),"rejected/null visuals perform no pose work");
    renderer.invisible=false;renderer.val_bHUD=TRUE;renderer.add_Visual(&outer);
    Require(outer.evaluations==1 && inner.evaluations==1 && held.world.c.x==7,"nested skeleton in hierarchy/LOD prepares attachments too");
    Require(renderer.queue.size()==1 && renderer.queue[0].hud,"preparation does not flatten or reclassify queued geometry");
    renderer.add_Visual(&outer);
    Require(outer.evaluations==1 && inner.evaluations==1,"repeated admission in same time slice reuses poses");
    IRender_Visual plain;renderer.add_Visual(&plain);
    Require(renderer.queue.size()==3,"ordinary visuals still enqueue normally");
}
int main(){try{
    for(unsigned hierarchy=0;hierarchy<2;++hierarchy)
        for(unsigned gameplay=0;gameplay<2;++gameplay)
            for(unsigned shadow=0;shadow<2;++shadow)Attachments(hierarchy!=0,gameplay!=0,shadow!=0);
    AdmissionAndNestedSkeletons();
    std::printf("E1.6 attachment ordering: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
