#include "raster_fixture.h"
#include "../../xr_3da/xrRender/occRasterizer.h"
#include "../../xr_3da/KichR4Occlusion.h"
#include <cstdio>
#include <limits>
MemoryFixture Memory;RendererFixture RImplementation;
unsigned checks=0;
void Check(bool value){++checks;assert(value);}
void Wall(occTri& a,occTri& b,float left,float top,float right,float bottom,float depth){
    a.raster[0]={left,top,depth};a.raster[1]={right,top,depth};a.raster[2]={left,bottom,depth};
    b.raster[0]={left,bottom,depth};b.raster[1]={right,top,depth};b.raster[2]={right,bottom,depth};
    for(unsigned i=0;i<3;++i){a.adjacent[i]=&b;b.adjacent[i]=&a;}
    Check(Raster.rasterize(&a)>0);Check(Raster.rasterize(&b)>0);Raster.propagade();
}
int main(){
    Raster.clear();Raster.propagade();Check(Raster.test(.4f,.4f,.6f,.6f,.8f)!=FALSE);
    occTri a={},b={};Wall(a,b,16,16,48,48,.5f);
    Check(Raster.test(.4f,.4f,.6f,.6f,.8f)==FALSE);
    Check(Raster.test(.4f,.4f,.6f,.6f,.3f)!=FALSE);
    Check(Raster.test(.1f,.4f,.6f,.6f,.8f)!=FALSE);
    Check(Raster.test(.9f,.4f,.95f,.6f,.8f)!=FALSE);
    // Same center hidden, but an exposed part of the complete light range
    // keeps illumination alive. Testing only the lamp origin would fail here.
    using namespace kich_r4_occlusion;
    const float near_z=.2f,far_z=100.f;
    float vp[16]={1,0,0,0,0,1,0,0,0,0,far_z/(far_z-near_z),1,0,0,-near_z*far_z/(far_z-near_z),0};
    const float camera[3]={0,0,0},small_position[3]={0,0,10};float low[3],high[3];Rect rect;
    Check(RangeBounds(small_position,1,low,high));Check(ProjectBox(low,high,camera,vp,rect));
    Check(rect.left<.45f && rect.right>.55f);Check(Raster.test(rect.left,rect.top,rect.right,rect.bottom,rect.depth)==FALSE);
    Check(RangeBounds(small_position,5,low,high));Check(ProjectBox(low,high,camera,vp,rect));
    Check(Raster.test(rect.left,rect.top,rect.right,rect.bottom,rect.depth)!=FALSE);
    Check(RangeBounds(small_position,11,low,high));Check(!ProjectBox(low,high,camera,vp,rect));
    const float crossing[3]={2,0,.3f};Check(RangeBounds(crossing,.5f,low,high));Check(!ProjectBox(low,high,camera,vp,rect));
    Check(!RangeBounds(small_position,-1,low,high));
    float nan_position[3]={std::numeric_limits<float>::quiet_NaN(),0,10};Check(!RangeBounds(nan_position,1,low,high));
    Check(RangeBounds(small_position,1,low,high));vp[0]=std::numeric_limits<float>::infinity();Check(!ProjectBox(low,high,camera,vp,rect));
    vp[0]=1;low[0]=high[0]+1;Check(!ProjectBox(low,high,camera,vp,rect));
    Raster.clear();Raster.propagade();Check(Raster.test(.4f,.4f,.6f,.6f,.8f)!=FALSE);
    Check(RImplementation.HOM.syncs>0);
    std::printf("E1.3 production R2 CPU raster + conservative light-volume projection: %u checks passed\n",checks);
}
