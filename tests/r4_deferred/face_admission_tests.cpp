// P0-S11 LIT01 shadow face admission history (KichR4FaceAdmission.h).
#include "KichR4FaceAdmission.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace kich_r4_face_admission;
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
static KichR4LocalLight Point(float x,float y,float z,float range){
    KichR4LocalLight l={};l.position[0]=x;l.position[1]=y;l.position[2]=z;l.range=range;l.type=1;l.color[0]=l.color[1]=l.color[2]=1;return l;}
static KichR4LocalLight Spot(float dx,float dy,float dz){KichR4LocalLight l=Point(0,2,0,10);l.type=2;l.direction[0]=dx;l.direction[1]=dy;l.direction[2]=dz;l.cone=1.0472f;return l;}
// One planning frame for one light: reasons in, admitted mask out (Final with no budget cut).
struct Run {Policy policy;uint32_t frame=0,now=1000;
    uint32_t Step(const KichR4LocalLight& l,const uint8_t* raw,uint8_t* reasons,uint32_t dt=16,uint64_t key=7,uint32_t hold=DefaultHoldMs,uint32_t final_cut=0){
        ++frame;now+=dt;policy.Begin(frame,now,hold);memcpy(reasons,raw,6);int32_t ticket=-1;
        const uint32_t mask=policy.Admit(key,l,reasons,ticket);policy.Final(ticket,mask&~final_cut,256,reasons);policy.Commit();return mask&~final_cut;}};
static uint8_t All(uint8_t r,unsigned faces,uint8_t* out){for(unsigned f=0;f<6;++f)out[f]=f<faces?r:kich_r4_local_shadow::FaceNone;return r;}
int main(){try{
    using namespace kich_r4_local_shadow;
    const KichR4LocalLight lamp=Point(0,2,10,6);uint8_t visible[6],occluded[6],frustum[6],r[6];
    All(FaceVisible,6,visible);All(FaceOccluded,6,occluded);All(FaceFrustum,6,frustum);
    // A short HOM false on a visible face: held, rendered, then released after 100 ms and two frames.
    {Run run;Require(run.Step(lamp,visible,r)==63,"visible faces admitted");
        Require(run.Step(lamp,occluded,r)==63 && r[0]==FaceHeld && run.policy.last.held==6,"one occluded frame keeps every face (HELD)");
        Require(run.Step(lamp,visible,r)==63 && run.policy.last.transitions==0,"a one-frame HOM false causes no face transition");
        uint32_t mask=63;unsigned frames=0;uint32_t elapsed=0;
        while(mask){mask=run.Step(lamp,occluded,r);++frames;elapsed+=16;if(frames>20)break;}
        Require(frames==8 && r[0]==FaceOccluded,"continuous occlusion releases the face once 100 ms have passed (8 frames at 16 ms)");
        Require(run.policy.last.transitions==6 && run.policy.event_count>=6 && run.policy.events[0].from==FaceHeld && run.policy.events[0].to==FaceOccluded,"the release is recorded per face");
        Require(run.Step(lamp,occluded,r)==0,"a released face stays out while occluded");
        Require(run.Step(lamp,visible,r)==63,"a visible face returns at once");}
    // Two frames are needed even at a long frame time.
    {Run run;run.Step(lamp,visible,r);Require(run.Step(lamp,occluded,r,200)==63,"first occluded frame is held whatever its length");
        Require(run.Step(lamp,occluded,r,200)==0,"second frame past 100 ms releases");}
    // Cold occlusion is not admitted; frustum exits are never held.
    {Run run;Require(run.Step(lamp,occluded,r)==0 && r[0]==FaceOccluded,"a face occluded on first sight is not admitted");}
    {Run run;run.Step(lamp,visible,r);Require(run.Step(lamp,frustum,r)==0 && r[0]==FaceFrustum && run.policy.last.frustum==6,"outside the frustum leaves at once");
        Require(run.Step(lamp,occluded,r)==0,"a face that left the frustum is not revived by an occlusion result");}
    // Mixed faces keep their own history.
    {Run run;run.Step(lamp,visible,r);uint8_t mixed[6];memcpy(mixed,visible,6);mixed[2]=FaceOccluded;mixed[5]=FaceFrustum;
        Require(run.Step(lamp,mixed,r)==(63u&~32u) && r[2]==FaceHeld && r[5]==FaceFrustum && r[0]==FaceVisible,"faces decided independently");}
    // Hold 0 is the P0-S7 behaviour.
    {Run run;run.Step(lamp,visible,r,16,7,0);Require(run.Step(lamp,occluded,r,16,7,0)==0,"r4_lamp_face_hold 0 drops an occluded face at once");}
    // Gaps and jumps restart history.
    {Run run;run.Step(lamp,visible,r);Require(run.Step(lamp,occluded,r,300)==0,"an observation gap over 250 ms restarts history (cold)");}
    {Run run;run.Step(lamp,visible,r);Require(run.Step(Point(5,2,10,6),occluded,r)==0 && run.policy.last.restarts==1,"a teleported light restarts history");}
    {Run run;run.Step(lamp,visible,r);Require(run.Step(Point(.05f,2.02f,10,6),occluded,r)==63 && run.policy.last.restarts==0,"a swinging lamp keeps history");}
    {Run run;uint8_t v1[6],o1[6];All(FaceVisible,1,v1);All(FaceOccluded,1,o1);run.Step(Spot(0,-1,0),v1,r);
        Require(run.Step(Spot(.05f,-1,0),o1,r)==1,"a spot re-aimed by 3 degrees keeps history");
        Require(run.Step(Spot(1,-1,0),o1,r)==0 && run.policy.last.restarts==1,"a spot re-aimed by 45 degrees restarts history");}
    // Forget and other sources.
    {Run run;run.Step(lamp,visible,r);run.policy.Forget(7);Require(run.Step(lamp,occluded,r)==0,"a forgotten source loses its history");}
    {Run run;run.Step(lamp,visible,r,16,7);Require(run.Step(lamp,occluded,r,16,8)==0,"history is per source");}
    // Budget: an admitted face cut by FitBudget is BUDGET, never held, and is a transition.
    {Run run;run.Step(lamp,visible,r);Require(run.Step(lamp,visible,r,16,7,DefaultHoldMs,63)==0 && r[0]==FaceBudget && run.policy.last.budget==6 && run.policy.last.transitions==6,"budget drops are reported as BUDGET transitions");}
    // Size class changes are recorded.
    {Policy p;uint8_t reasons[6];int32_t t;
        p.Begin(1,1000);memcpy(reasons,visible,6);p.Final((p.Admit(7,lamp,reasons,t),t),63,256,reasons);p.Commit();
        p.Begin(2,1016);memcpy(reasons,visible,6);p.Final((p.Admit(7,lamp,reasons,t),t),63,512,reasons);p.Commit();
        Require(p.last.size_changes==1 && p.events[p.event_count-1].face==255 && p.events[p.event_count-1].size_to==512,"size class change recorded");}
    // FaceTest reasons agree with FaceMask.
    {using namespace kich_r4_lighting;const float camera[3]={0,0,0};
        const Matrix vp=kich_r4_scene::Multiply(StableView(Vec3{0,0,0},Vec3{0,0,1}),Projection(1.57079632679f,.2f,300));
        uint8_t reasons[6];const KichR4LocalLight side=Point(12,0,10,6);const uint32_t mask=FaceTest(side,camera,vp.m,NULL,reasons);
        Require(mask==FaceMask(side,camera,vp.m,NULL) && reasons[0]==FaceFrustum && reasons[1]==FaceVisible,"FaceTest names the culled face OUTSIDE_FRUSTUM");
        for(unsigned f=0;f<6;++f)Require(FaceAdmitted(reasons[f])==((mask>>f)&1u),"admitted reasons are the mask");
        const KichR4LocalLight behind=Point(0,0,-10,9);FaceTest(behind,camera,vp.m,NULL,reasons);Require(reasons[4]==FaceHud,"HUD box reason");
        uint8_t spot[6];FaceTest(Spot(0,-1,0),NULL,NULL,NULL,spot);Require(spot[0]==FaceUnbounded && spot[1]==FaceNone,"no camera: unbounded, spot has one face");
        Require(!strcmp(FaceReasonName(FaceHeld),"HOM_OCCLUDED_HELD") && !strcmp(FaceReasonName(200),"INVALID"),"reason names");}
    printf("P0-S11 shadow face admission: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
