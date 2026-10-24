#include "fixture.h"
#include "../../xr_3da/KichR4LevelData.h"
#include "../../xr_3da/KichR4RosBudget.h"
#include <stdio.h>
#include <limits>
#include <cmath>
using namespace kich_r4_level;
namespace {
unsigned checks=0;
void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
fixture::Bytes Replace(const fixture::Bytes& input,uint32_t id,const fixture::Bytes& value){
    fixture::Bytes out;Reader r(View(input));bool found=false;
    while(r.Left()){const uint32_t key=r.U32(),bytes=r.U32();const unsigned char* p=r.Take(bytes);
        fixture::Chunk(out,key,key==id?value:fixture::Bytes(p,p+bytes));found=found || key==id;}
    if(!found)fixture::Chunk(out,id,value);
    return out;
}
fixture::Bytes Windows(){
    fixture::Bytes w(16,0);fixture::U32(w,2);
    fixture::U32(w,0);fixture::U16(w,2);fixture::U16(w,3);
    fixture::U32(w,3);fixture::U16(w,1);fixture::U16(w,2);return w;
}
fixture::Level Progressive(){
    fixture::Level f;fixture::Bytes table;const Span original=Chunk(View(f.level),3);Reader r(original);
    while(r.Left()){const uint32_t id=r.U32(),size=r.U32();const unsigned char* p=r.Take(size);fixture::Bytes visual(p,p+size);
        if(id==1)visual=Replace(visual,6,Windows());
        fixture::Chunk(table,id,visual);}
    f.level=Replace(f.level,3,table);
    fixture::Bytes tree;fixture::U32(tree,1);const fixture::Bytes w=Windows();tree.insert(tree.end(),w.begin(),w.end());f.geometry=Replace(f.geometry,11,tree);
    Span ib=Chunk(View(f.geometry),10);const size_t offset=static_cast<size_t>(ib.p-f.geometry.data())+8;
    // The max range needs vertex 2; the lower SWI only references prefix [0,2).
    f.geometry[offset+4*2]=2;f.geometry[offset+9*2]=1;
    f.geometry[offset+10*2]=2;f.geometry[offset+15*2]=1;
    return f;
}
void Ranges(){
    fixture::Level f=Progressive();Scene s;Parse(View(f.level),View(f.geometry),s);
    Require(s.lods.size()==2 && s.draws[1].lod && s.draws[3].lod,"progressive static and tree tables survive parsing");
    Require(s.indices[0].size()==18,"overlapping SWI indices are stored as one union per visual");
    Require(s.triangles==6,"baseline triangle total still describes highest-detail geometry");
    for(unsigned i=1;i<4;i+=2){const Draw& d=s.draws[i];const DrawRange high=SelectDrawRange(s,d,1),low=SelectDrawRange(s,d,0);
        Require(high.first==d.first && high.count==6 && low.first==d.first+3 && low.count==3,"original SWI offsets/counts selected exactly");
        Require(SelectDrawRange(s,d,std::numeric_limits<float>::quiet_NaN()).count==6,"nonfinite LOD preserves maximum detail");}
    Require(SelectDrawRange(s,s.draws[0],0).count==3,"ordinary static geometry remains unchanged");
    Scene attributes;Materials(View(f.level),attributes);Vertices(View(f.geometry),attributes);
    for(size_t i=0;i<attributes.vertices[0].vertices.size();++i)attributes.vertices[0].vertices[i].attributes=7;
    attributes.vertices[0].vertices[6].attributes=1;
    Visuals(View(f.level),Indices(View(f.geometry)),TreeWindows(View(f.geometry)),attributes);
    Require(SelectDrawRange(attributes,attributes.draws[1],1).available_attributes==1 &&
        SelectDrawRange(attributes,attributes.draws[1],0).available_attributes==7,"attributes follow the selected range, not unrelated high-LOD vertices");

    fixture::Level bad=f;Span ib=Chunk(View(bad.geometry),10);const size_t offset=static_cast<size_t>(ib.p-bad.geometry.data())+8;
    bad.geometry[offset+9*2]=2;bool rejected=false;try{Scene out;Parse(View(bad.level),View(bad.geometry),out);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"a valid full VB cannot hide an invalid lower progressive prefix");
    fixture::Bytes malformed=Windows();fixture::Set(malformed,20+8,UINT32_MAX);rejected=false;
    try{fixture::Bytes only,visual=Replace(fixture::Visual(2,1,4,4,6),6,malformed);fixture::Chunk(only,0,visual);
        fixture::Level broken=f;broken.level=Replace(broken.level,3,only);Scene out;Parse(View(broken.level),View(broken.geometry),out);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"overflowed later-window range rejected before buffer access");
    Scene bounded;Materials(View(f.level),bounded);Vertices(View(f.geometry),bounded);bounded.cpu_bytes=192u*1024u*1024u-24;
    fixture::Bytes only;fixture::Chunk(only,0,Replace(fixture::Visual(2,1,4,4,6),6,Windows()));
    fixture::Bytes level=Replace(f.level,3,only);
    Visuals(View(level),Indices(View(f.geometry)),TreeWindows(View(f.geometry)),bounded);
    Require(bounded.draws.size()==1 && bounded.draws[0].count==6 && !bounded.draws[0].lod && bounded.lod_budget_fallbacks==1,"optional LOD storage cannot make the original maximum-detail slice exceed its budget");
    Scene baseline;baseline.keep_lods=false;Parse(View(f.level),View(f.geometry),baseline);
    Scene tight;tight.geometry_budget=baseline.cpu_bytes;Parse(View(f.level),View(f.geometry),tight);
    Require(tight.lods.empty() && !tight.keep_lods && tight.cpu_bytes==baseline.cpu_bytes && tight.indices==baseline.indices && tight.lod_budget_fallbacks==2,"earlier optional LOD storage cannot starve later mandatory visual slices");
}
void Policy(){
    using namespace kich_r4_lod;
    const Parameters p=CameraParameters(1920,1080,90,1);
    Require(Geometry(1,1,p)==1 && Geometry(1,1.e9,p)==0,"near and distant model choose existing detail endpoints");
    float previous=1;
    for(unsigned d=1;d<1000;++d){const float value=Geometry(1,double(d)*d,p);Require(value<=previous && value>=0 && value<=1,"camera detail decreases monotonically with distance");previous=value;}
    const Parameters small=CameraParameters(960,540,90,1),large=CameraParameters(1920,1080,60,1);
    Require(Geometry(1,1000,p)>=Geometry(1,1000,small) && Geometry(1,1000,large)>=Geometry(1,1000,p),"resolution and zoom retain more detail");
    Require(Geometry(1,-1,p)==1 && Geometry(std::numeric_limits<float>::quiet_NaN(),100,p)==1,"invalid model inputs fail toward full detail");
    Require(kich_r4_lod::Window(1,5)==0 && kich_r4_lod::Window(0,5)==4 && kich_r4_lod::Window(.5f,5)==2 && kich_r4_lod::Window(-1,5)==4,"SWI rounding matches FProgressive::Render");
    kich_r4_scene::Matrix identity=kich_r4_scene::Identity();float low[]={-.02f,-.02f,.3f},high[]={.02f,.02f,.5f};
    Require(Projected(low,high,identity,256,256)==0,"small shadow footprint permits lower existing detail");
    low[0]=-1;high[0]=1;Require(Projected(low,high,identity,256,256)==1,"large shadow footprint preserves full detail");
    low[2]=-.01f;Require(Projected(low,high,identity,256,256)==1,"near-plane crossing preserves full detail");
}
void Scheduling(){
    using namespace kich_r4_ros;kich_r4_ros::Budget budget;std::vector<Entry> entries(128);std::vector<unsigned> updates(128,0);
    for(size_t i=0;i<entries.size();++i){Entry& e=entries[i];e.initialized=e.seen=true;e.updated=0;e.requested=0;budget.Add(e);}
    for(uint32_t frame=1;frame<=16;++frame){const uint32_t now=250+frame*16;unsigned count=0;
        for(size_t i=0;i<entries.size();++i)if(budget.Request(entries[i],frame,now)){++count;++updates[i];entries[i].updated=now;}
        Require(count<=8,"render-only recurring updates respect frame budget");}
    for(size_t i=0;i<updates.size();++i)Require(updates[i]==1,"round-robin avoids starvation in a stable draw order");
    Entry fresh;budget.Add(fresh);Require(budget.Request(fresh,17,550),"first exposure is initialized immediately");fresh.initialized=true;fresh.updated=550;
    Require(!budget.Request(fresh,18,600,true),"priority retains the established 200 ms cache");
    Require(budget.Request(fresh,19,800,true),"actor priority bypasses exhausted background admission");
    budget.Remove(entries[0]);budget.Remove(entries[64]);budget.Remove(entries[127]);budget.Remove(fresh);
    for(uint32_t frame=20;frame<40;++frame)for(size_t i=1;i<127;++i)if(i!=64 && budget.Request(entries[i],frame,1000+frame*16))entries[i].updated=1000+frame*16;
    Entry wrap;wrap.initialized=true;wrap.updated=UINT32_MAX-99;Require(wrap.Due(150),"millisecond counter wrap preserves elapsed-age test");
    // At low FPS render-only refresh is bounded by frames, not a promised
    // 200 ms wall-clock deadline. All objects still get a turn; AI is direct.
    kich_r4_ros::Budget slow;std::vector<Entry> crowded(500);std::vector<unsigned> serviced(500,0);
    for(size_t i=0;i<crowded.size();++i){crowded[i].initialized=crowded[i].seen=true;crowded[i].requested=0;slow.Add(crowded[i]);}
    for(uint32_t frame=1;frame<=63;++frame){unsigned count=0;for(size_t i=0;i<crowded.size();++i){
        if(slow.Request(crowded[i],frame,500+frame*100)){++count;++serviced[i];crowded[i].updated=500+frame*100;}}
        Require(count<=8,"10 FPS crowded render remains bounded per frame");}
    for(size_t i=0;i<serviced.size();++i)Require(serviced[i]>=1,"crowded low-FPS requests eventually refresh without starvation");
}
}
int main(){try{Ranges();Policy();Scheduling();printf("E1.3 progressive LOD and ROS scheduling: %u checks passed\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
