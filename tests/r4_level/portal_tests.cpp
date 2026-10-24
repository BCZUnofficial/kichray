#include "fixture.h"
#include "../../xr_3da/KichR4LevelVisibility.h"
#include <stdio.h>
using namespace kich_r4_level;
namespace {
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
fixture::Bytes SectorBytes(uint32_t root,const std::vector<uint16_t>& links){fixture::Bytes s,r,p;fixture::U32(r,root);
    for(size_t i=0;i<links.size();++i)fixture::U16(p,links[i]);
    fixture::Chunk(s,1,p);fixture::Chunk(s,2,r);return s;}
fixture::Bytes PortalBytes(uint16_t front,uint16_t back){fixture::Bytes p;fixture::U16(p,front);fixture::U16(p,back);
    const float vertices[4][3]={{-1,0,-1},{-1,0,1},{1,0,1},{1,0,-1}};
    for(unsigned i=0;i<4;++i)for(unsigned a=0;a<3;++a)fixture::Float(p,vertices[i][a]);
    // Unused svector slots are unspecified bytes, including NaN bit patterns.
    for(unsigned i=0;i<6;++i)fixture::U32(p,0xffffffffu);
    fixture::U32(p,4);return p;}
fixture::Level WithTopology(){fixture::Level f;fixture::Bytes output,visuals;
    const Span original=View(f.level),table=Chunk(original,3);
    for(uint32_t i=0;i<6;++i){const Span v=Chunk(table,i);fixture::Bytes node(v.p,v.p+v.n);
        if(i>=4){fixture::Bytes children;fixture::U32(children,2);fixture::U32(children,i==4?0:2);fixture::U32(children,i==4?1:3);fixture::Chunk(node,10,children);}
        fixture::Chunk(visuals,i,node);}
    for(uint32_t i=1;i<=3;++i){const Span chunk=Chunk(original,i);fixture::Chunk(output,i,i==3?visuals:fixture::Bytes(chunk.p,chunk.p+chunk.n));}
    fixture::Bytes sectors;fixture::Chunk(sectors,0,SectorBytes(4,{0}));fixture::Chunk(sectors,1,SectorBytes(5,{0}));
    fixture::Chunk(output,8,sectors);fixture::Chunk(output,4,PortalBytes(0,1));f.level=output;return f;}
void ParseTopology(){fixture::Level f=WithTopology();Scene scene;Parse(View(f.level),View(f.geometry),scene);
    Require(scene.topology.valid && scene.topology.sectors.size()==2 && scene.topology.portals.size()==1,"SoC portal/sector chunks decoded");
    Require(scene.topology.sectors[0].draws==std::vector<uint32_t>({0,1}) && scene.topology.sectors[1].draws==std::vector<uint32_t>({2,3}),"sector roots resolve hierarchy/LOD children to flat draw IDs");
    Require(scene.sector_owned==std::vector<unsigned char>({1,1,1,1}),"all linked leaves are sector owned");
    Require(scene.topology.portals[0].vertices.size()==4 && scene.topology.portals[0].normal.y>.99f,"svector count follows six slots; unused bytes ignored");
    for(unsigned damage=0;damage<4;++damage){fixture::Level bad=WithTopology();const Span portals=Chunk(View(bad.level),4);
        const size_t at=static_cast<size_t>(portals.p-bad.level.data());
        if(damage==0)fixture::Set(bad.level,static_cast<unsigned>(at+76),7);
        if(damage==1){bad.level[at]=99;bad.level[at+1]=0;}
        if(damage==2)fixture::Set(bad.level,static_cast<unsigned>(at+4),0x7fc00000u);
        if(damage==3){const Span sectors=Chunk(View(bad.level),8),sector=Chunk(sectors,0),links=Chunk(sector,1);
            const size_t link=static_cast<size_t>(links.p-bad.level.data());bad.level[link]=4;}
        Scene loaded;Parse(View(bad.level),View(bad.geometry),loaded);
        Require(!loaded.topology.valid && loaded.draws.size()==4,"malformed optional portal data leaves valid geometry usable");}
    for(unsigned damage=0;damage<3;++damage){Scene bad=scene;
        if(damage==0)bad.nodes[4].children.push_back(4);
        if(damage==1)bad.nodes[4].children.push_back(100000);
        if(damage==2)bad.topology.sectors[0].root=100000;
        MapSectorDraws(bad);Require(!bad.topology.valid,"cycles/bad hierarchy or sector indices disable portal pruning");}
    Scene shared=scene;shared.nodes[5].children.push_back(0);shared.nodes[4].children.erase(shared.nodes[4].children.begin()+1);MapSectorDraws(shared);
    Require(shared.topology.valid && shared.topology.sectors[1].draws.back()==0 && !shared.sector_owned[1],"shared visual ownership and unmapped leaves are preserved");
    fixture::Level legacy;Scene fallback;Parse(View(legacy.level),View(legacy.geometry),fallback);
    Require(!fallback.topology.valid && fallback.draws.size()==4,"old maps/viewer fixtures without topology fail open");
    Topology output=scene.topology;Require(!LoadTopology(Span(),output) && !output.valid && output.sectors.empty(),"failed reload clears previously valid topology");
}
Portal VerticalPortal(uint32_t front,uint32_t back,float x,float z=.5f){Portal p;p.front=front;p.back=back;
    p.vertices={{x-.1f,-.1f,z},{x+.1f,-.1f,z},{x+.1f,.1f,z},{x-.1f,.1f,z}};p.normal={0,0,1};p.distance=-z;return p;}
Scene SelectionScene(){Scene s;s.materials.resize(1);s.topology.valid=true;s.topology.sectors.resize(3);s.sector_owned={1,1,1,0};
    for(uint32_t i=0;i<4;++i){Draw d={};d.low={-.3f,-.3f,.2f};d.high={.3f,.3f,.8f};s.draws.push_back(d);if(i<3)s.topology.sectors[i].draws.push_back(i);}
    s.topology.portals.push_back(VerticalPortal(0,1,0));s.topology.portals.push_back(VerticalPortal(1,2,4));
    s.topology.sectors[0].portals={0};s.topology.sectors[1].portals={0,1};s.topology.sectors[2].portals={1};return s;}
void Selection(){Scene s=SelectionScene();const Matrix vp=kich_r4_scene::Identity();StaticVisibility camera,light;
    SelectStatic(s,vp,0,0,0,camera);Require(camera.portal_used && camera.sectors==2 && camera.portal_tests==2 && camera.sector_rejected==1,"camera traverses only original-view portals");
    Require(camera.draws==std::vector<unsigned char>({1,1,0,1}),"disconnected sector hidden but unowned draw retained");
    SelectStatic(s,vp,2,0,0,light);Require(light.draws==std::vector<unsigned char>({0,0,1,1}),"shadow light sector is independent of camera sector");
    SelectStatic(s,vp,UINT32_MAX,0,0,light);Require(!light.portal_used && light.draws==std::vector<unsigned char>({1,1,1,1}),"sun/unknown sector uses full frustum only");
    s.topology.portals[0]=VerticalPortal(0,1,0,-.01f);SelectStatic(s,vp,0,0,0,camera);
    Require(camera.draws[1]!=0,"doorway before near plane still exposes farther geometry");
    s.topology.portals.push_back(VerticalPortal(0,2,.2f));s.topology.sectors[0].portals.push_back(2);s.topology.sectors[2].portals.push_back(2);
    s.topology.portals[1]=VerticalPortal(1,2,.2f);SelectStatic(s,vp,0,0,0,camera);
    Require(camera.sectors==3 && camera.portal_tests==3 && camera.draws[2],"cycle and second entrance terminate without first-path clipping holes");
    s.topology.valid=false;s.draws[1].low.x=3;s.draws[1].high.x=4;SelectStatic(s,vp,0,0,0,camera);
    Require(!camera.portal_used && !camera.draws[1] && camera.frustum_rejected==1,"fallback keeps ordinary frustum rejection");
    s.draws[1].tree=true;s.draws[1].world=kich_r4_scene::Identity();s.vertices.resize(1);s.vertices[0].tree_fraction_max=1;s.materials[0].descriptor.tree_wave=true;
    SelectStatic(s,vp,0,10,0,camera);Require(camera.draws[1],"tree wind expansion shared by residency and drawing");
    s.topology=Topology();SelectStatic(s,vp,0,0,0,camera);Require(!camera.portal_used,"cleared level never reuses old sector mask");
}
void SectorRays(){fixture::Level f=WithTopology();Topology topology;Require(LoadTopology(View(f.level),topology),"ray fixture topology valid");
    float distance=500;uint32_t sector=PortalRaySector(topology,{0,2,0},{0,-1,0},distance);
    Require(sector==0 && fabsf(distance-2)<.0001f,"down ray returns front side of nearest portal");
    distance=500;sector=PortalRaySector(topology,{0,-2,0},{0,1,0},distance);
    Require(sector==1 && fabsf(distance-2)<.0001f,"up ray returns back side of nearest portal");
    distance=1;sector=PortalRaySector(topology,{0,2,0},{0,-1,0},distance);
    Require(sector==UINT32_MAX && distance==1,"portal beyond nearer CFORM distance cannot override it");
    distance=500;sector=PortalRaySector(topology,{3,2,0},{0,-1,0},distance);
    Require(sector==UINT32_MAX && distance==500,"outside-polygon ray misses portal");
    distance=500;sector=PortalRaySector(topology,{0,.01f,0},{0,1,0},distance);
    Require(sector==UINT32_MAX && distance==0,"origin on doorway explicitly ambiguous instead of taking a farther sector");
    topology.valid=false;distance=500;sector=PortalRaySector(topology,{0,2,0},{0,-1,0},distance);
    Require(sector==UINT32_MAX && distance==500,"invalid topology cannot identify a sector");
}
}
int main(){try{ParseTopology();Selection();SectorRays();printf("E1.2 portal visibility: %u checks passed\n",checks);return 0;}
    catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
