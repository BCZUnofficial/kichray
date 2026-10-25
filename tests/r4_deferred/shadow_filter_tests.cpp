// Production HLSL PCF/helpers are inserted by run_shadow_tests.py. The depth
// oracle below is an independent analytic plane, not a copy of the PCF code.
#include "../../xr_3da/KichR4LightingMath.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <map>
#include <cstring>
using std::isfinite;
float max(float a,float b){return std::max(a,b);}
float min(float a,float b){return std::min(a,b);}
using std::abs;
float saturate(float a){return std::max(0.f,std::min(1.f,a));}
struct float2 {float x,y;float2(float a=0,float b=0):x(a),y(b){} };
struct float3 {float x,y,z;float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
struct float4 {float x,y,z,w;float4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){} float4(float2 a,float c,float d):x(a.x),y(a.y),z(c),w(d){}
    float4(float3 a,float d):x(a.x),y(a.y),z(a.z),w(d){} };
float2 operator+(float2 a,float2 b){return float2(a.x+b.x,a.y+b.y);}
float2 operator+(float2 a,float b){return a+float2(b,b);}
float2 operator-(float2 a,float2 b){return float2(a.x-b.x,a.y-b.y);}
float2 operator-(float2 a,float b){return float2(a.x-b,a.y-b);}
float2 operator*(float2 a,float b){return float2(a.x*b,a.y*b);}
float2 operator*(float a,float2 b){return b*a;}
float2& operator*=(float2& a,float b){a=a*b;return a;}
float2 operator/(float2 a,float b){return a*(1.f/b);}
float3 operator-(float3 a,float3 b){return float3(a.x-b.x,a.y-b.y,a.z-b.z);}
float3 operator+(float3 a,float3 b){return float3(a.x+b.x,a.y+b.y,a.z+b.z);}
float3 operator*(float3 a,float b){return float3(a.x*b,a.y*b,a.z*b);}
float3 operator/(float3 a,float b){return a*(1.f/b);}
float3 operator-(float3 a){return a*-1.f;}
float3& operator*=(float3& a,float b){a=a*b;return a;}
float3& operator+=(float3& a,float3 b){a=a+b;return a;}
float rsqrt(float a){return 1.f/std::sqrt(a);}
float length(float3 a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
float length(float2 a){return std::sqrt(a.x*a.x+a.y*a.y);}
bool operator<(float2 a,float b){return a.x<b || a.y<b;}
bool operator>(float2 a,float b){return a.x>b || a.y>b;}
bool any(bool v){return v;}
bool all(bool v){return v;}
bool isfinite(float2 v){return std::isfinite(v.x) && std::isfinite(v.y);}
float2 floor(float2 v){return float2(std::floor(v.x),std::floor(v.y));}
float dot(float2 a,float2 b){return a.x*b.x+a.y*b.y;}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float3 cross(float3 a,float3 b){return float3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
float3 normalize(float3 a){return a*(1.f/std::sqrt(dot(a,a)));}
float3 xyz(float4 v){return float3(v.x,v.y,v.z);}
float lerp(float a,float b,float t){return a+(b-a)*t;}
float4 screen,camera,sun_color,sun_direction;int shadow_surface=0,surface=0;
kich_r4_scene::Matrix shadow[6];
kich_r4_scene::Matrix inverse_vp,receiver_inverse;
float4 mul(float4 p,const kich_r4_scene::Matrix& m);
unsigned checks=0,comparisons=0,gathers=0;
typedef unsigned uint;
// P0-S7: a map's own size for GetDimensions; zero = the cbuffer screen.w.
unsigned dimension_override=0;
float Dim(){return dimension_override?float(dimension_override):screen.w;}
void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
struct Texture2D;std::map<const Texture2D*,unsigned> texture_uses; // the runners pass maps to PCF by reference
struct Texture2D {
    // P0-S8: bend adds a second plane beyond u=crease (a mesh edge between two facets).
    double a,b,c;bool edge;double bend,crease;
    double Depth(double u,double v)const{return edge?(u<.5?.4:.6):a*u+b*v+c+(bend && u>crease?bend*(u-crease):0);}
    void GetDimensions(uint& width,uint& height)const{width=height=uint(Dim());}
    float SampleCmpLevelZero(int,float2 uv,float compare)const{
        ++comparisons;++texture_uses[this];
        Require(std::fabs(uv.x*Dim()-.5-std::round(uv.x*Dim()-.5))<1e-4,"point comparison must hit an exact texel centre");
        Require(uv.x>=0 && uv.x<=1 && uv.y>=0 && uv.y<=1,"comparison must not read the white border");
        const double stored=Depth(uv.x,uv.y);
        return double(compare)<=stored?1.f:0.f;
    }
    // D3D11 Gather at a texel corner: x,y,z,w = lower-left, lower-right,
    // upper-right, upper-left texel of the 2x2 quad (v grows downwards).
    float4 Gather(int,float2 uv)const{
        ++gathers;++texture_uses[this];
        const double u=double(uv.x)*Dim(),v=double(uv.y)*Dim();
        Require(std::fabs(u-std::round(u))<1e-4 && std::fabs(v-std::round(v))<1e-4,"Gather must sit on a texel corner");
        const int i=int(std::lround(u))-1,j=int(std::lround(v))-1;
        return float4(Stored(i,j+1),Stored(i+1,j+1),Stored(i+1,j),Stored(i,j));
    }
    // Outside the map a quad reads 0 (a blocker): the filter must give it no weight.
    float Stored(int x,int y)const{
        if(x<0 || y<0 || x>=int(Dim()) || y>=int(Dim()))return 0;
        const double u=(x+.5)/Dim(),v=(y+.5)/Dim();
        return float(Depth(u,v));
    }
};
// P0-S5 cascade maps: contact (t7), 128 m (t2), 512 m (t3); P0-S7 lamp cube faces t2..t7.
Texture2D shadow0,shadow1,shadow2,shadow3,shadow4,shadow5;
// PRODUCTION_SHADOW_HELPERS
// P0-S7 PCFWide takes a map index (t2..t7); bind a fixture map at shadow0.
float Wide(const Texture2D& map,float4 projected,float4 dx,float4 dy,float2 crease=float2(0,1)){shadow0=map;return PCFWide(0,projected,dx,dy,crease);}
float4 Transform(float3 p,float w,const kich_r4_scene::Matrix& m){
    return float4(p.x*m.m[0]+p.y*m.m[4]+p.z*m.m[8]+w*m.m[12],
        p.x*m.m[1]+p.y*m.m[5]+p.z*m.m[9]+w*m.m[13],
        p.x*m.m[2]+p.y*m.m[6]+p.z*m.m[10]+w*m.m[14],
        p.x*m.m[3]+p.y*m.m[7]+p.z*m.m[11]+w*m.m[15]);
}
float4 mul(float4 p,const kich_r4_scene::Matrix& m){return Transform(xyz(p),p.w,m);}
// Independent plane/clip intersection: solve X/Z directly at a fixed world Y,
// rather than copying the production 4x4 inverse/reconstruction algorithm.
float PlaneDepth(float2 pixel,double ground,const kich_r4_scene::Matrix& vp){
    const double nx=double(pixel.x)*2/screen.x-1,ny=1-double(pixel.y)*2/screen.y;
    const double ax=vp.m[0]-nx*vp.m[3],az=vp.m[8]-nx*vp.m[11];
    const double bx=vp.m[1]-ny*vp.m[3],bz=vp.m[9]-ny*vp.m[11];
    const double ac=ground*(vp.m[4]-nx*vp.m[7])+vp.m[12]-nx*vp.m[15];
    const double bc=ground*(vp.m[5]-ny*vp.m[7])+vp.m[13]-ny*vp.m[15];
    const double determinant=ax*bz-az*bx,x=(az*bc-ac*bz)/determinant,z=(ac*bx-ax*bc)/determinant;
    const double depth=(x*vp.m[2]+ground*vp.m[6]+z*vp.m[10]+vp.m[14])/(x*vp.m[3]+ground*vp.m[7]+z*vp.m[11]+vp.m[15]);
    return float(std::round(depth*16777215)/16777215); // actual D24 UNORM screen depth
}
void ReceiverReconstruction(){using namespace kich_r4_lighting;
    const float direction[3]={-.898962140083313f,-.3120878040790558f,-.3073570132255554f};
    const float3 sun(direction[0],direction[1],direction[2]);
    for(float pitch:{-.03f,.0119f,.04f})for(float near_plane:{.2f,.01f})for(float translation:{0.f,1.f,4.f}){
        const Vec3 forward=Normalize(Vec3{-.954f,pitch,-.2994f});
        const float origin[3]={-238.471f*translation,-18.532f*translation,-149.789f*translation};
        camera=float4(origin[0],origin[1],origin[2],0);screen=float4(1920,1080,0,2048);
        Matrix projection=Projection(67.5f*3.14159265358979323846f/180,near_plane,150);projection.m[0]*=1080.f/1920;
        const Matrix vp=Multiply(StableView(Vec3{origin[0],origin[1],origin[2]},forward),projection);
        Require(Inverse(vp,inverse_vp,origin),"relative inverse exists at both actual engine near planes");
        const double ground=double(origin[1])-1.7;
        const Matrix sm=SunShadow(origin,direction,128,2048);Matrix sm_inverse;Require(Inverse(sm,sm_inverse),"shadow oracle inverse");
        const double a=-2*double(sm_inverse.m[1])/sm_inverse.m[9],b=2*double(sm_inverse.m[5])/sm_inverse.m[9];
        const double c=(ground+sm_inverse.m[1]-sm_inverse.m[5]-sm_inverse.m[13])/sm_inverse.m[9];
        Texture2D plane={a,b,c,false};
        for(unsigned y=566;y<1000;y+=13)for(unsigned x=700;x<1300;x+=71){
            float3 relative[4];
            for(unsigned k=0;k<4;++k){const float2 pixel(float(x)+.5f+float(k&1),float(y)+.5f+float(k>>1));
                relative[k]=RelativePosition(pixel,PlaneDepth(pixel,ground,vp));}
            const float3 p=relative[0]+xyz(camera),dx=relative[1]-relative[0],dy=relative[2]-relative[0];
            const float3 mixed=relative[3]-relative[1]-relative[2]+relative[0];
            const float distance_squared=dot(relative[0],relative[0]);
            if(distance_squared>150*150 || dot(relative[0],float3(forward.x,forward.y,forward.z))<=0)continue;
            Require(std::fabs(double(relative[0].y)+origin[1]-ground)<.002,"relative reconstruction keeps planar height within2mm including near0.01 and translated cameras");
            Require(ShadowPlaneStable(distance_squared,max(dot(dx,dx),dot(dy,dy)),ShadowPlaneErrorSquared(dx,dy,mixed)),"continuous quantized ground quad retains its receiver plane");
            const float4 q=Transform(p-sun*.01f,1,sm);if(std::max(std::fabs(q.x),std::fabs(q.y))>=.9f)continue;
            Require(PCF(plane,q,Transform(dx,0,sm),Transform(dy,0,sm))>.9999f,"full D24-relative-reconstruction-to-PCF chain must not shadow unobstructed ground");
        }
    }
}
void PlaneCases(){using namespace kich_r4_lighting;
    for(unsigned size:{16u,256u,512u,2048u})for(float degrees:{0.f,35.f,60.f,80.f})for(bool perspective:{false,true}){
        const float sx=std::tan(degrees*3.14159265f/180),sy=sx*.27f,d=10;
        Matrix projection=Identity();Texture2D surface={};
        if(perspective){KichR4LocalLight light={};light.type=1;light.range=40;projection=LocalShadow(light,4,size);
            const double A=projection.m[10],B=projection.m[14],s=projection.m[0];
            surface={-2*B*sx/(s*d),2*B*sy/(s*d),A+B/d*(1+sx/s-sy/s),false};
        }else{const float camera[3]={0,0,0},direction[3]={0,0,1};projection=SunShadow(camera,direction,512,size);
            surface={sx*512/1024.,-sy*512/1024.,(512+d-sx*256+sy*256)/1024.,false};}
        const float3 tangent_x(.01f,0,.01f*sx),tangent_y(0,.01f,.01f*sy);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        for(float x:{-.01f,0.f,.012f}){
            float3 position(x,.017f,d+sx*x+sy*.017f);
            position=ShadowNumericalOffset(position,tangent_x,tangent_y,perspective?-position/length(position):float3(0,0,-1));
            const float precision=perspective?.003f:.01f;
            if(perspective){const float length=std::sqrt(position.x*position.x+position.y*position.y+position.z*position.z);position=position*(1-precision/length);}
            else position.z-=precision;
            const float4 projected=Transform(position,1,projection);screen.w=float(size);comparisons=0;
            const float lit=PCF(surface,projected,dx,dy);
            Require(lit>.9999f,"inclined receiver must not shadow itself across the full filter footprint");
            Require(comparisons==9,"ordinary pixel retains exactly nine comparisons");
            // A parallel blocker 2 cm from the receiver must still shadow it,
            // including the grazing 80 degree plane. No large bias may erase it.
            const double blocker_d=d-.02*std::sqrt(1+sx*sx+sy*sy);Texture2D blocker=surface;
            if(perspective){const double A=projection.m[10],B=projection.m[14],s=projection.m[0];
                blocker={-2*B*sx/(s*blocker_d),2*B*sy/(s*blocker_d),A+B/blocker_d*(1+sx/s-sy/s),false};}
            else blocker.c+=(blocker_d-d)/1024.;
            Require(PCF(blocker,projected,dx,dy)<.0001f,"nearby contact blocker must remain shadowed");
        }
    }
}
void Continuity(){
    screen.w=512;Texture2D edge={0,0,0,true};float previous=-1;
    for(unsigned step=0;step<=2000;++step){const float u=.495f+.01f*step/2000;
        const float current=PCF(edge,float4(u*2-1,0,.5f,1),float4(.01f,0,0,0),float4(0,.01f,0,0));
        Require(current>=0 && current<=1,"normalized filter stays in visibility range");
        if(previous>=0)Require(std::fabs(current-previous)<.004f,"visibility must remain continuous across texel/window boundaries");
        previous=current;
    }
    for(unsigned size:{16u,256u,512u})for(float u:{0.f,.001f,.5f,.999f,1.f}){
        screen.w=float(size);Texture2D shadow={0,0,.4,false},light={0,0,.6,false};
        Require(PCF(shadow,float4(u*2-1,0,.5,1),float4(),float4())==0,"map edges must not introduce a lit border");
        Require(std::fabs(PCF(light,float4(u*2-1,0,.5,1),float4(),float4())-1)<1e-6,"edge normalization preserves full light");
    }
}
void CubeGuard(){using namespace kich_r4_lighting;KichR4LocalLight light={};light.type=1;light.range=30;
    for(unsigned size:{16u,32u,64u,128u,256u,512u})for(unsigned face=0;face<6;++face){const Matrix m=LocalShadow(light,face,size);
        for(int first:{-1,1})for(int second:{-1,1}){float3 p(5*first,5*second,5);const unsigned axis=face/2;
            float values[3]={5.f*first,5.f*second,5};values[axis]=face%2?-5.f:5.f;
            if(axis==0){values[1]=5.f*first;values[2]=5.f*second;}if(axis==1){values[0]=5.f*first;values[2]=5.f*second;}
            p=float3(values[0],values[1],values[2]);const float4 q=Transform(p,1,m);
            const float u=(q.x/q.w*.5f+.5f)*size,v=(.5f-q.y/q.w*.5f)*size;
            // P0-S7 PCFWide reads texels floor(u-2.5) .. +5 on both axes.
            Require(std::floor(u-2.5f)>=0 && std::floor(u-2.5f)+5<size && std::floor(v-2.5f)>=0 && std::floor(v-2.5f)+5<size,"every cube corner has a complete rendered PCFWide guard at the actual resolution");
        }
    }
}
void GrazingReceiver(){using namespace kich_r4_lighting;
    const float camera_position[3]={-238.4709930419922f,-18.531999588012695f,-149.78900146484375f};
    const float direction[3]={-.898962140083313f,-.3120878040790558f,-.3073570132255554f};
    const Matrix projection=SunShadow(camera_position,direction,128,2048);
    const float3 p(-332.83056640625f,-20.23200035095215f,-157.55050659179688f);
    const float3 dx(-.034210205078125f,0,.1090087890625f),dy(5.940216064453125f,0,.4883270263671875f);
    const float3 relative=p-float3(camera_position[0],camera_position[1],camera_position[2]);
    Require(ShadowPlaneStable(dot(relative,relative),max(dot(dx,dx),dot(dy,dy)),ShadowPlaneErrorSquared(dx,dy,float3(.01f,0,.02f))),
        "continuous grazing ground stays a valid plane despite a six-metre pixel step");
    Texture2D plane={8.287288059063833e-10,-.38052325836786677,.695527551319455,false};screen.w=2048;
    const float3 sun(direction[0],direction[1],direction[2]);
    Require(PCF(plane,Transform(p-sun*.01f,1,projection),Transform(dx,0,projection),Transform(dy,0,projection))>.9999f,
        "logged-camera grazing-plane counterexample remains fully lit");
    // A noncoplanar background corner cannot masquerade as the same receiver.
    const float3 small_x(.01f,0,0),small_y(0,.01f,0),jump(0,0,2);
    Require(!ShadowPlaneStable(100,.0001f,ShadowPlaneErrorSquared(small_x,small_y,jump)),"depth-discontinuous quad is rejected");
    comparisons=0;Texture2D blocker={0,0,.4,false};
    Require(PCF(blocker,float4(0,0,.5f,1),float4(),float4())==0 && comparisons==1,
        "invalid-plane fallback preserves a blocker without nine-texel extrapolation");
}
// P0-S3: a pixel whose screen quad is not coplanar has zero derivatives. The
// shading normal must restore the nine-tap plane-corrected filter without
// self-shadowing, keep a 4 cm blocker, and leave grazing receivers alone.
void NormalPlaneFallback(){using namespace kich_r4_lighting;
    const float origin[3]={0,0,0},direction[3]={0,0,1};const float3 towards(0,0,-1);
    for(unsigned size:{2048u,8192u})for(float degrees:{0.f,35.f,60.f,75.f})for(float tilt:{0.f,8.f}){
        if(tilt>0 && size!=8192)continue; // bump tilt: 128 m cascade texel (6.25 cm)
        const float sx=std::tan(degrees*3.14159265f/180),sy=sx*.27f,d=10;
        const Matrix projection=SunShadow(origin,direction,512,size);screen.w=float(size);
        const Texture2D surface={sx*512/1024.,-sy*512/1024.,(512+d-sx*256+sy*256)/1024.,false};
        const float t=std::tan(tilt*3.14159265f/180);
        const float3 normal=normalize(float3(sx+t,sy,-1));
        for(float x:{-.01f,0.f,.012f}){
            float3 p(x,.017f,d+sx*x+sy*.017f),dx,dy;
            ShadowNormalPlane(normal,towards,p,dx,dy);
            Require(dot(dx,dx)>0 && dot(dy,dy)>0,"zero derivatives take the normal plane");
            p=ShadowNumericalOffset(p,dx,dy,towards);p.z-=.01f; // SunVisibility receiver offset
            comparisons=0;const float lit=PCF(surface,Transform(p,1,projection),Transform(dx,0,projection),Transform(dy,0,projection));
            Require(lit>(tilt>0?.99f:.9999f),"normal-plane fallback must not shadow its own receiver");
            Require(comparisons==9,"normal-plane fallback filters nine texels, not one square");
            Texture2D blocker=surface;blocker.c+=-.04*std::sqrt(1+sx*sx+sy*sy)/1024.;
            Require(PCF(blocker,Transform(p,1,projection),Transform(dx,0,projection),Transform(dy,0,projection))<.0001f,
                "a 4 cm blocker still shadows a fallback receiver");
        }
    }
    // Grazing receiver (|N.L| = cos 80 deg < .2) keeps the single comparison.
    const float sx=std::tan(80*3.14159265f/180);float3 p(0,0,10),dx,dy;
    ShadowNormalPlane(normalize(float3(sx,0,-1)),towards,p,dx,dy);
    Require(dot(dx,dx)==0 && dot(dy,dy)==0 && p.z==10,"grazing fallback keeps zero derivatives and position");
    // A valid screen plane is never replaced.
    dx=float3(.01f,0,0);dy=float3(0,.01f,0);ShadowNormalPlane(float3(0,0,-1),towards,p,dx,dy);
    Require(dx.x==.01f && dy.y==.01f && p.z==10,"screen-derived plane wins over the shading normal");
}
// P0-S5 production cascade selection and coverage: the finest map containing
// the receiver is read, edge blends are continuous, and beyond the 512 m
// cascade the receiver fades to lit instead of the PCF out-of-map shadow.
void CascadeSelection(){using namespace kich_r4_lighting;
    const float origin[3]={0,0,0},direction[3]={-.3f,-.9f,-.3f};
    shadow[0]=SunShadow(origin,direction,128,2048);shadow[1]=SunShadow(origin,direction,512,2048);shadow[2]=SunShadow(origin,direction,40,2048);
    screen.w=2048;camera=float4(0,0,0,0);sun_color=float4(1,1,1,1);
    const Vec3 d=Normalize(Vec3{direction[0],direction[1],direction[2]});sun_direction=float4(d.x,d.y,d.z,0);
    shadow5={0,0,0,false};shadow0={0,0,1,false};shadow1={0,0,0,false}; // contact dark, near lit, far dark
    const float3 dx(.01f,0,0),dy(0,0,.01f);
    auto at=[&](float x){texture_uses.clear();return SunVisibility(float3(x,-2,x*.37f),dx,dy);};
    Require(at(1)<.0001f && texture_uses[&shadow5]>0 && !texture_uses[&shadow0] && !texture_uses[&shadow1],"contact cascade owns the first metres");
    Require(at(40)>.9999f && texture_uses[&shadow0]>0 && !texture_uses[&shadow5] && !texture_uses[&shadow1],"128 m cascade beyond the contact sphere");
    Require(at(120)<.0001f && texture_uses[&shadow1]>0 && !texture_uses[&shadow0],"512 m cascade beyond 128 m");
    Require(at(2000)>.9999f && texture_uses.empty(),"outside every map is lit, not the PCF border shadow");
    float previous=at(0),largest=0;
    for(float x=.05f;x<=400;x+=.05f){const float v=at(x);Require(v>=0 && v<=1,"visibility range");
        for(const auto& use:texture_uses)Require(use.second<=9,"each cascade is filtered at most once per pixel");
        largest=std::max(largest,std::fabs(v-previous));previous=v;}
    Require(largest<.05f,"contact, 128/512 m and far-coverage transitions are continuous");
    // Depth edge: walk a receiver along the sun ray out of the far map's +-512 m
    // depth range (beyond 128 m XY so the far map owns it); no step to lit.
    previous=-1;largest=0;
    for(float t=380;t<=560;t+=.05f){const float3 p(120+d.x*t,-2+d.y*t,44.4f+d.z*t);texture_uses.clear();
        const float v=SunVisibility(p,dx,dy);Require(v>=0 && v<=1,"depth-edge visibility range");
        if(previous>=0)largest=std::max(largest,std::fabs(v-previous));previous=v;}
    Require(previous>.9999f && largest<.05f,"far cascade depth edge fades continuously to lit");
    // P0-S9: inside the contact XY square the contact map is read at any depth
    // (matching the XY-only cascade hole): beyond its far bound the receiver is
    // behind the stored contact casters, before its near bound in front of them.
    for(float t:{600.f,900.f}){const float3 behind(1+d.x*t,-2+d.y*t,.37f+d.z*t);texture_uses.clear();
        Require(SunVisibility(behind,dx,dy)<.0001f && texture_uses[&shadow5]>0 && !texture_uses[&shadow0] && !texture_uses[&shadow1],"contact XY owns receivers beyond its depth window");
        const float3 ahead(1-d.x*t,-2-d.y*t,.37f-d.z*t);texture_uses.clear();
        Require(SunVisibility(ahead,dx,dy)>.9999f && texture_uses[&shadow5]>0 && !texture_uses[&shadow0],"before the contact depth window nothing occludes");}
    shadow[2].m[15]=0;Require(at(1)>.9999f && texture_uses[&shadow0]>0 && !texture_uses[&shadow5],"invalid contact matrix falls back to the E1.9 pair");
    std::memset(shadow,0,sizeof(shadow));
}
// P0-S5 PCFWide (sun maps): the stretched B-spline keeps the E1.9 contracts.
void WideCases(){using namespace kich_r4_lighting;
    // 8192: the P0-S7 far map is larger than screen.w; PCFWide must use its own size.
    for(unsigned size:{256u,2048u,4096u,8192u})for(float degrees:{0.f,35.f,60.f,80.f}){
        dimension_override=size==8192?size:0;
        const float sx=std::tan(degrees*3.14159265f/180),sy=sx*.27f,d=10;
        const float origin[3]={0,0,0},direction[3]={0,0,1};const Matrix projection=SunShadow(origin,direction,512,size);
        const Texture2D surface={sx*512/1024.,-sy*512/1024.,(512+d-sx*256+sy*256)/1024.,false};
        const float3 tangent_x(.01f,0,.01f*sx),tangent_y(0,.01f,.01f*sy);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        for(float x:{-.01f,0.f,.012f,.37f}){
            float3 position(x,.017f,d+sx*x+sy*.017f);
            position=ShadowNumericalOffset(position,tangent_x,tangent_y,float3(0,0,-1));position.z-=.01f;
            const float4 projected=Transform(position,1,projection);screen.w=float(size==8192?4096:size);comparisons=0;gathers=0;
            Require(Wide(surface,projected,dx,dy)>.9999f,"wide filter: inclined receiver never shadows itself");
            Require(gathers==9 && comparisons==0,"wide filter reads the 6x6 support with nine Gathers");
            Texture2D blocker=surface;blocker.c+=-.02*std::sqrt(1+sx*sx+sy*sy)/1024.;
            Require(Wide(blocker,projected,dx,dy)<.0001f,"wide filter: 2 cm contact blocker still shadows");
        }
    }
    dimension_override=0;
    // P0-S7 lamps: perspective cube faces at every planned size (64..1024; the
    // plan never goes below 64, where +-3 texels span metres) through the same filter.
    for(unsigned size:{64u,128u,256u,512u,1024u})for(float degrees:{0.f,35.f,60.f,80.f}){
        const float sx=std::tan(degrees*3.14159265f/180),sy=sx*.27f,d=10;
        KichR4LocalLight light={};light.type=1;light.range=40;const Matrix projection=LocalShadow(light,4,size);
        const double A=projection.m[10],B=projection.m[14],s=projection.m[0];
        const Texture2D surface={-2*B*sx/(s*d),2*B*sy/(s*d),A+B/d*(1+sx/s-sy/s),false};
        const float3 tangent_x(.01f,0,.01f*sx),tangent_y(0,.01f,.01f*sy);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        for(float x:{-.01f,0.f,.012f}){
            float3 position(x,.017f,d+sx*x+sy*.017f);
            position=ShadowNumericalOffset(position,tangent_x,tangent_y,-position/length(position));
            {const float distance=std::sqrt(dot(position,position));position=position*(1-.003f/distance);} // LocalVisibility 3 mm
            const float4 projected=Transform(position,1,projection);screen.w=float(size);gathers=0;
            Require(Wide(surface,projected,dx,dy)>.9999f,"lamp wide filter: inclined receiver never shadows itself");
            Require(gathers==9,"lamp wide filter reads nine Gathers");
            const double blocker_d=d-.02*std::sqrt(1+sx*sx+sy*sy);
            const Texture2D blocker={-2*B*sx/(s*blocker_d),2*B*sy/(s*blocker_d),A+B/blocker_d*(1+sx/s-sy/s),false};
            Require(Wide(blocker,projected,dx,dy)<.0001f,"lamp wide filter: 2 cm blocker still shadows");
        }
    }
    // A culled lamp face binds a 16x16 map at depth 1: every receiver reads lit.
    {screen.w=512;dimension_override=16;const Texture2D lit={0,0,1,false};
        for(float u:{.1f,.5f,.93f})Require(std::fabs(Wide(lit,float4(u*2-1,.2f,.7f,1),float4(.01f,0,0,0),float4(0,.01f,0,0))-1)<1e-6,"culled face map reads lit");
        dimension_override=0;}
    screen.w=512;Texture2D edge={0,0,0,true};float previous=-1;
    for(unsigned step=0;step<=2000;++step){const float u=.49f+.02f*step/2000;
        const float current=Wide(edge,float4(u*2-1,0,.5f,1),float4(.01f,0,0,0),float4(0,.01f,0,0));
        Require(current>=0 && current<=1,"wide filter range");
        if(previous>=0)Require(std::fabs(current-previous)<.004f,"wide filter is continuous across texel boundaries");
        previous=current;}
    // Map edges: Gather quads reaching outside the map carry no weight.
    for(unsigned size:{16u,256u,4096u})for(float u:{0.f,.001f,.5f,.999f,1.f})for(float v:{0.f,.5f,1.f}){
        screen.w=float(size);Texture2D shadow={0,0,.4,false},light={0,0,.6,false};
        const float4 projected(u*2-1,1-v*2,.5f,1),dx(.01f,0,0,0),dy(0,.01f,0,0);
        Require(Wide(shadow,projected,dx,dy)==0,"wide filter: map edges must not introduce a lit border");
        Require(std::fabs(Wide(light,projected,dx,dy)-1)<1e-6,"wide filter: edge normalization preserves full light");
    }
}
// P0-S8 night stripes: a concave mesh edge (two facets bent by B toward the
// light) under the moon at 13 degrees. The receiver plane of one facet,
// extended across the 6x6 support, ran under the next facet. The production
// crease allowance (ShadowCrease) must clear edges up to 3 degrees at every
// elevation, while 5-10 cm contact blockers keep their shadow.
float CreaseMinimum(unsigned size,float elevation,float bend,bool allowance){using namespace kich_r4_lighting;
    dimension_override=size;screen.w=4096;
    const float tilt=90-elevation,sx=std::tan(tilt*3.14159265f/180),sx2=std::tan((tilt-bend)*3.14159265f/180),d=10,x0=.3f;
    const float origin[3]={0,0,0},direction[3]={0,0,1};const Matrix projection=SunShadow(origin,direction,512,size);
    const Texture2D surface={sx*512/1024.,0,(512+d-sx*256)/1024.,false,(sx2-sx)*512/1024.,(x0+256)/512.};
    float worst=1;
    for(float x=x0-1.5f;x<=x0+1.5f;x+=.01f){
        const float slope=x>x0?sx2:sx;const float3 tangent_x(.01f,0,.01f*slope),tangent_y(0,.01f,0);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        float3 position(x,.017f,d+sx*x+(x>x0?(sx2-sx)*(x-x0):0));
        const float2 crease=allowance?ShadowCrease(tangent_x,tangent_y,float3(0,0,-1),ShadowRiseLimit(2,512.f/size)):float2(0,1);
        position=ShadowNumericalOffset(position,tangent_x,tangent_y,float3(0,0,-1));position.z-=.01f;
        worst=std::min(worst,Wide(surface,Transform(position,1,projection),dx,dy,crease));}
    dimension_override=0;return worst;
}
void CreaseCases(){using namespace kich_r4_lighting;
    Require(CreaseMinimum(4096,13,2,false)<.7f && CreaseMinimum(8192,13,1,false)<.7f,"crease fixture reproduces the night stripes without the allowance");
    for(unsigned size:{4096u,8192u})for(float elevation:{10.f,13.f,20.f,31.f,45.f,60.f,80.f})for(float bend:{.5f,1.f,2.f,3.f})
        Require(CreaseMinimum(size,elevation,bend,true)>.99f,"concave mesh edges up to 3 degrees never shadow their own surface");
    for(unsigned size:{4096u,8192u})for(float elevation:{10.f,13.f,20.f,31.f,45.f,60.f,90.f}){
        dimension_override=size;screen.w=4096;
        const float tilt=90-elevation,sx=std::tan(tilt*3.14159265f/180),d=10;
        const float origin[3]={0,0,0},direction[3]={0,0,1};const Matrix projection=SunShadow(origin,direction,512,size);
        const Texture2D surface={sx*512/1024.,0,(512+d-sx*256)/1024.,false};
        const float3 tangent_x(.01f,0,.01f*sx),tangent_y(0,.01f,0);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        const float2 crease=ShadowCrease(tangent_x,tangent_y,float3(0,0,-1),ShadowRiseLimit(2,512.f/size));
        Require(crease.x>=0 && crease.x<=.5f && crease.y==1,"crease allowance is bounded; no grazing clamp up to 80 degrees");
        for(float x:{-.01f,.1f,.37f}){float3 position(x,.017f,d+sx*x);
            position=ShadowNumericalOffset(position,tangent_x,tangent_y,float3(0,0,-1));position.z-=.01f;
            const float4 projected=Transform(position,1,projection);
            Require(Wide(surface,projected,dx,dy,crease)>.9999f,"crease allowance: flat receiver stays lit");
            auto blocked=[&](float h){Texture2D blocker=surface;blocker.c+=-h*std::sqrt(1+sx*sx)/1024.;return Wide(blocker,projected,dx,dy,crease);};
            Require(blocked(.1f)<.05f && blocked(.2f)<.0001f,"crease allowance: 10-20 cm contact blockers keep their shadow");
            if(elevation>=20)Require(blocked(.05f)<.05f,"crease allowance: 5 cm blockers keep their shadow above 20 degrees");
            if(size==8192 && elevation>=31)Require(blocked(.02f)<.05f,"crease allowance: 2 cm blockers keep their shadow on the 8192 map by day");}
    }
    dimension_override=0;
}
// P0-S9 grazing faces: a model triangle almost parallel to the sun under a
// roof (2.5 m up the light ray) must not see past it. Before the 87 degree
// plane limit the +-3 texel footprint rose metres and read ~40% lit at 89.5.
void GrazingLeak(){using namespace kich_r4_lighting;
    for(unsigned size:{2048u,4096u,8192u})for(float tilt:{80.f,85.f,87.f,88.f,89.f,89.5f,89.9f}){
        dimension_override=size;screen.w=4096;
        const float sx=std::tan(tilt*3.14159265f/180),d=10;
        const float origin[3]={0,0,0},direction[3]={0,0,1};const Matrix projection=SunShadow(origin,direction,128,size);
        const Texture2D surface={sx*128/1024.,0,(512+d-sx*64)/1024.,false};
        const float3 tangent_x(.01f,0,.01f*sx),tangent_y(0,.01f,0);
        const float4 dx=Transform(tangent_x,0,projection),dy=Transform(tangent_y,0,projection);
        const float2 crease=ShadowCrease(tangent_x,tangent_y,float3(0,0,-1),ShadowRiseLimit(.6f,128.f/size));
        const float limit_degrees=std::atan(ShadowRiseLimit(.6f,128.f/size))*180/3.14159265f;
        Require(crease.y>0 && crease.y<=1 && (tilt>limit_degrees-.01f || crease.y==1),"plane limit applies past the rise limit only (0.6 m per texel, at most 87 degrees)");
        for(float x:{-.03f,.1f,.37f}){float3 position(x,.017f,d+sx*x);
            position=ShadowNumericalOffset(position,tangent_x,tangent_y,float3(0,0,-1));position.z-=.01f;
            const float4 projected=Transform(position,1,projection);
            const Texture2D roof={0,0,(512+d+sx*x-2.5f)/1024.,false};
            Require(Wide(roof,projected,dx,dy,crease)<.05f,"a grazing face never sees past a roof 2.5 m above it");
            if(crease.y==1)Require(Wide(surface,projected,dx,dy,crease)>.9999f,"faces within the rise limit keep their exact plane: no self-shadow");}
    }
    dimension_override=0;
}
#include "receiver_raster_tests.h"
int main(){try{
    CreaseCases();GrazingLeak();WideCases();CascadeSelection();NormalPlaneFallback();PlaneCases();Continuity();CubeGuard();GrazingReceiver();ReceiverReconstruction();ReceiverRasterization();
    float2 gradient;Require(!ShadowGradient(float3(0,0,1),float3(0,0,2),gradient),"singular UV Jacobian requests bounded fallback");
    Require(!ShadowGradient(float3(1,0,std::numeric_limits<float>::infinity()),float3(0,1,1),gradient),"nonfinite slope requests bounded fallback");
    Require(ShadowPlaneStable(4,.01f,0) && !ShadowPlaneStable(4,10000,10000),"noncoplanar jump does not become a receiver plane");
    Require(!ShadowPlaneStable(1,std::numeric_limits<float>::infinity(),0),"nonfinite derivative rejected");
    std::printf("E1.5 production PCF plane/contact/continuity/cube guard: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}