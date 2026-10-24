#include "../../xr_3da/KichR4RosExposure.h"
#include "../../xr_3da/KichR4RosBudget.h"
#include "../../xr_3da/KichR4Lighting.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
typedef uint32_t u32;
struct Fvector {
    float x,y,z;
    void set(float a,float b,float c){x=a;y=b;z=c;}
    void add(const Fvector& a,const Fvector& b){set(a.x+b.x,a.y+b.y,a.z+b.z);}
    void sub(const Fvector& a,const Fvector& b){set(a.x-b.x,a.y-b.y,a.z-b.z);}
    void mul(float k){x*=k;y*=k;z*=k;}
    void div(float k){mul(1/k);}
    float square_magnitude()const{return x*x+y*y+z*z;}
    float magnitude()const{return std::sqrt(square_magnitude());}
    void normalize(){div(magnitude());}
    void invert(const Fvector& a){set(-a.x,-a.y,-a.z);}
};
struct Fmatrix {
    Fvector i,j,k,c;
    void transform_tiny(Fvector& p,const Fvector& in)const{
        const Fvector out={in.x*i.x+in.y*j.x+in.z*k.x+c.x,in.x*i.y+in.y*j.y+in.z*k.y+c.y,in.x*i.z+in.y*j.z+in.z*k.z+c.z};p=out;
    }
    void transform_tiny(Fvector& p)const{transform_tiny(p,p);}
};
struct vis_data {struct Sphere{Fvector P;float R;}sphere;struct Box{Fvector min,max;}box;};
struct Visual {vis_data vis;};
struct IRenderable {struct Data {Fmatrix xform;Visual* visual;}renderable;};
struct IRender_ObjectSpecific {};
struct IRender_Light {enum{SPOT=2};};
struct CEnvDescriptor {Fvector ambient,hemi_color,sun_dir,sun_color;};
struct Weather {CEnvDescriptor CurrentEnv;};
struct Persistent {Weather weather;Weather& Environment(){return weather;}} persistent;
Persistent* g_pGamePersistent=&persistent;
namespace collide {enum{rqtStatic=0};}
struct Level {
    struct Space {
        struct Ray {Fvector point,direction;float length;};
        std::vector<Ray> rays;bool blocked=false;
        bool RayTest(const Fvector& p,const Fvector& d,float length,int,void*,void*){rays.push_back({p,d,length});return blocked;}
    } ObjectSpace;
    bool bReady=true;IRenderable* actor=NULL;IRenderable* CurrentViewEntity(){return actor;}
} level;
Level* g_pGameLevel=&level;
struct DeviceState {u32 dwFrame,dwTimeGlobal;float fTimeDelta;} Device={0,0,.016f};
float ps_r2_lt_smooth=1;
const float EPS_L=1e-5f;
float _sqrt(float x){return std::sqrt(x);}float _abs(float x){return std::fabs(x);}
float clampr(float x,float low,float high){return (std::max)(low,(std::min)(x,high));}
kich_r4_ros::Budget native_ros_budget;
KichR4LightingFrame local_lights;unsigned light_queries=0;
const KichR4LightingFrame& NativeRosLights(){++light_queries;return local_lights;}
#include "ros_under_test.h"
namespace {
unsigned checks=0;
void Require(bool good,const char* message){++checks;if(!good)throw std::runtime_error(message);}
bool Near(float a,float b){return std::fabs(a-b)<1e-5f;}
void History(){
    using kich_r4_ros::VisualExposure;VisualExposure v;const float p[3]={0,0,0};v.Begin(p,1,0);
    bool seen[26]={};
    for(unsigned i=0;i<26;++i){unsigned next=v.NextDirection();Require(!seen[next],"rolling probe visits every hemisphere direction before reuse");seen[next]=true;
        const float* d=kich_r4_ros::HemisphereDirection(next);Require(d[1]>=0,"all probe directions remain in the sky hemisphere");
        Require(std::fabs(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]-1)<.0001f,"reference probe directions have unit length");v.Sample(false);}
    v.Commit(false);Require(v.hemi==0 && v.sun==0,"first dark exposure has no bright interpolation flash");
    for(unsigned i=0;i<5;++i)v.Sample(true);v.Commit(true);
    v.Smooth(1,.016f,1);Require(Near(v.hemi,5.f/26*.016f) && Near(v.sun,.016f),"raw binary sun and rolling hemi are smoothed independently");
    const float previous=v.sun;v.Smooth(1,1,1);Require(v.sun==previous,"multiple submeshes do not advance smoothing twice in a frame");
    v.Smooth(2,.016f,1);Require(v.sun>previous && v.sun<1,"cached exposure advances between ray refreshes");
    const unsigned next=v.NextDirection();const float moved[3]={.8f,0,0};v.Begin(moved,1,200);Require(v.count==0 && v.initialized,"moving to another sample volume drops history but retains smoothing");
    Require(v.NextDirection()==next,"ordinary motion does not repeatedly restart all probes at the zenith");
    for(unsigned i=0;i<5;++i)v.Sample(false);v.Commit(false);Require(v.sun>0,"ordinary movement does not snap the model dark");
    const float teleported[3]={50,0,0};v.Begin(teleported,1,400);for(unsigned i=0;i<5;++i)v.Sample(false);v.Commit(false);
    Require(v.sun==0 && v.hemi==0,"teleport initializes from fresh local probes rather than carrying previous-room light");
    v.Begin(teleported,1,3000);for(unsigned i=0;i<5;++i)v.Sample(true);v.Commit(true);
    Require(v.sun==1 && v.hemi==1,"long inactive gap initializes without stale exposure");
    v.Smooth(3,-1,1);Require(v.sun==1,"invalid negative frame time cannot reverse smoothing");
    Require(Near(kich_r4_ros::RaisedSampleY(0,1,-1,1),.3f),"standing probe matches R2 sphere offset");
    Require(Near(kich_r4_ros::RaisedSampleY(0,1,-.1f,.1f),.05f),"flat ragdoll probe remains within current body height");
}
void Production(){
    persistent.weather.CurrentEnv={{.1f,.1f,.1f},{.2f,.2f,.2f},{0,-1,0},{.3f,.3f,.3f}};
    Visual mesh={{{{0,0,0},1},{{-1,-1,-1},{1,1,1}}}};
    IRenderable object={{{{1,0,0},{0,1,0},{0,0,1},{0,0,0}},&mesh}};
    local_lights.count=1;local_lights.lights.resize(1);local_lights.lights[0]={};
    local_lights.lights[0].type=1;local_lights.lights[0].position[0]=4;local_lights.lights[0].range=10;
    for(unsigned i=0;i<3;++i)local_lights.lights[0].color[i]=.3f;
    R4ROS ros(&object);level.ObjectSpace.blocked=true;float hemi,sun;ros.Exposure(hemi,sun);
    Require(hemi==0 && sun==0 && level.ObjectSpace.rays.size()==6,"production first exposure uses exactly five hemi rays and one sun ray");
    Require(light_queries==0,"visual exposure does not enumerate or ray-test local lights");
    Require(Near(level.ObjectSpace.rays[0].point.y,.3f),"production standing ray uses raised probe");
    level.ObjectSpace.blocked=false;
    for(unsigned i=1;i<=13;++i){Device.dwFrame=i;Device.dwTimeGlobal=i*16;ros.Exposure(hemi,sun);}
    Require(level.ObjectSpace.rays.size()==12 && sun>0 && sun<.1f,"production admitted refresh smooths instead of assigning binary visibility");
    const float before=sun;ros.Exposure(hemi,sun);Require(sun==before && level.ObjectSpace.rays.size()==12,"same-frame submesh calls reuse sample and smoothing");
    const float ai=ros.get_luminocity();Require(Near(ai,.708f),"AI retains original unsmoothed ambient/hemi/sun/local-light formula");
    Require(light_queries==1 && level.ObjectSpace.rays.size()==19,"AI query performs its independent cached observation on demand");
    Require(Near(level.ObjectSpace.rays[12].point.y,0),"AI keeps its original observation point");
    ros.get_luminocity_hemi();ros.get_luminocity();Require(level.ObjectSpace.rays.size()==19,"AI getters share the existing 200 ms cache");
    ++Device.dwFrame;Device.dwTimeGlobal+=16;ros.Exposure(hemi,sun);Require(sun>before && level.ObjectSpace.rays.size()==19,"intermediate frame smooths with no additional ray tests");
    mesh.vis.box.min.y=-.1f;mesh.vis.box.max.y=.1f;ros.force_mode(0);ros.Exposure(hemi,sun);
    Require(Near(level.ObjectSpace.rays[19].point.y,.05f),"production ragdoll probe respects flattened bounds");
}
}
int main(){try{History();Production();printf("E1.5 visual ROS and isolated AI luminance: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
