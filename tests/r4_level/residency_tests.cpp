#include "recording.h"
#include "../../xr_3da/KichR4TexturePreflight.h"
namespace {
int32_t ReleaseTexture(KichR4Handle,KichR4Handle handle){Require(f->objects.erase(handle)==1,"residency releases live handle once");++f->released;return 0;}
void Setup(KichR4LevelSession& l,KichR4FrameSession& s){Configure(l,s);l.bridge.resources.release_resource=ReleaseTexture;
    Require(s.Start(reinterpret_cast<void*>(1),640,480)==0 && l.Placeholder(s,false,&l.white)==0 && l.Placeholder(s,true,&l.checker)==0,"residency setup");}
void Stop(KichR4LevelSession& l,KichR4FrameSession& s){KichR4GeometryReport report={};Require(l.bridge.Stop(s,&report)==0 && f->objects.empty() && f->opens==f->closes,"residency balances all GPU/file ownership");}
void Transaction(){kich_r4_texture::Registry registry;registry.budget=300;registry.bytes=300;registry.epoch=2;
    auto& old=registry.entries["old"];old.handle=10;old.bytes=100;old.touched=1;
    auto& pinned=registry.entries["pinned"];pinned.handle=11;pinned.bytes=200;pinned.touched=2;
    unsigned released=0;auto release=[&](uint64_t){++released;return 0;};
    Require(!registry.MakeRoom(150,release) && !released && old.handle==10 && registry.bytes==300,"rejected large request is capacity-transactional, retaining old resident mip");
    Require(registry.MakeRoom(80,release) && released==1 && registry.bytes==200,"subsequent smaller request evicts only when whole payload fits");}
void Saturation(){Fixture fixture;f=&fixture;KichR4LevelSession l;KichR4FrameSession s={};Setup(l,s);
    for(const char* name:{"ui","console","hud","glow","geometry","normal","hemi","shadow","new"})f->files[std::string("$game_textures$/")+name+".dds"]=fixture::Dds(8,8,4);
    l.texture_registry.budget=5*340;
    const char* names[]={"ui","console","hud","glow","geometry","normal","hemi","shadow"};
    const unsigned roles[]={kich_r4_texture::UI,kich_r4_texture::UI,kich_r4_texture::HUD,kich_r4_texture::EFFECT,kich_r4_texture::BASE,kich_r4_texture::NORMAL,kich_r4_texture::LIGHTING,kich_r4_texture::SHADOW_ALPHA};
    for(unsigned n=0;n<8;++n)l.NeedTexture(names[n],roles[n]);
    Require(l.CommitTexturePlan(s)==0 && l.texture_registry.planned_downgrades && !l.texture_registry.planned_denied,"over-budget frame chooses legal source mips before upload, no omitted dependency");
    std::map<std::string,KichR4Handle> handles;for(unsigned n=0;n<8;++n){handles[names[n]]=l.Texture(s,names[n]);Require(handles[names[n]]!=l.checker,"all frame texture dependencies admitted");}
    Require(l.texture_registry.entries["ui"].width==8 && l.texture_registry.entries["console"].width==8 && l.texture_registry.entries["hud"].width==8,"UI console and HUD preserve full resolution ahead of optional maps");
    const auto calls=f->texture_calls;const auto opens=f->opens;const auto evictions=l.texture_registry.evictions;
    for(unsigned frame=0;frame<200;++frame){l.texture_registry.Begin();
        for(int n=7;n>=0;--n)l.NeedTexture(names[n],roles[n]);
        Require(l.CommitTexturePlan(s)==0,"stationary working set admitted");
        for(unsigned n=0;n<8;++n)Require(l.Texture(s,names[n])==handles[names[n]],"stationary draw retains same live handle regardless of request order");
        Require(l.texture_registry.bytes<=l.texture_registry.budget,"stationary frame stays within budget");}
    Require(f->texture_calls==calls && f->opens==opens && l.texture_registry.evictions==evictions,"200 saturated frames cause zero DDS rereads, uploads or evictions");
    l.texture_registry.Begin();for(unsigned n=0;n<8;++n)l.NeedTexture(names[n],roles[n]);l.NeedTexture("new",kich_r4_texture::BASE);
    Require(l.CommitTexturePlan(s)==0 && l.Texture(s,"new")!=l.checker && l.Texture(s,"glow")==handles["glow"],"entering geometry preserves late glow and important sources while rebalancing mips");
    const auto after=f->texture_calls;l.texture_registry.Begin();for(unsigned n=0;n<8;++n)l.NeedTexture(names[n],roles[n]);
    Require(l.CommitTexturePlan(s)==0 && f->texture_calls==after,"leaving geometry does not re-upgrade every lower mip and cause oscillation");
    Stop(l,s);}
void Cubemap(){Fixture fixture;f=&fixture;KichR4LevelSession l;KichR4FrameSession s={};Setup(l,s);
    auto face=fixture::Dds(8,8,4);auto cube=face;fixture::Set(cube,112,0xfe00);
    for(unsigned i=1;i<6;++i)cube.insert(cube.end(),face.begin()+128,face.end());
    f->files["$game_textures$/cube.dds"]=cube;l.texture_registry.budget=600;
    l.NeedTexture("cube",kich_r4_texture::EFFECT);
    Require(l.texture_registry.entries["cube"].levels[0].bytes==2040 && l.texture_registry.entries["cube"].levels[1].bytes==504,"cubemap working set includes six faces at every retained mip");
    Require(l.CommitTexturePlan(s)==0 && l.Texture(s,"cube")!=l.checker && l.texture_registry.bytes==504 && l.texture_registry.entries["cube"].width==4,"cubemap planned mip and actual DDS upload payload agree");Stop(l,s);}
std::map<KichR4Handle,uint32_t> cube_faces;
int32_t CubeUpload(KichR4Handle device,const KichR4DdsDesc* desc,const void* data,uint32_t bytes,KichR4Handle* handle){
    const int32_t hr=Texture(device,desc,data,bytes,handle);
    if(hr==0){kich_r4_dds::Image image={};Require(kich_r4_dds::Parse(data,bytes,desc->max_dimension,image),"cube fallback DDS validates");cube_faces[*handle]=image.faces;}
    return hr;
}
void CubeFallback(){Fixture fixture;f=&fixture;cube_faces.clear();KichR4LevelSession l;KichR4FrameSession s={};Setup(l,s);l.api.create_texture_dds=CubeUpload;
    const auto fallback=l.CubeTexture(s,"missing_sky");Require(fallback && fallback!=l.white && fallback!=l.checker && cube_faces[fallback]==6,"missing sky resolves six-face cube, never 2D checker");
    const auto calls=f->texture_calls;Require(l.CubeTexture(s,"")==fallback && l.CubeTexture(s,"base")==fallback && f->texture_calls==calls,"empty or wrong-dimension source reuses persistent cube without uploading invalid 2D input");
    auto face=fixture::Dds(8,8,4);auto cube=face;fixture::Set(cube,112,0xfe00);for(unsigned i=1;i<6;++i)cube.insert(cube.end(),face.begin()+128,face.end());f->files["$game_textures$/sky.dds"]=cube;
    l.texture_registry.Begin();l.texture_registry.budget=0;l.NeedTexture("sky",kich_r4_texture::EFFECT);
    Require(l.CommitTexturePlan(s)==0 && l.texture_registry.planned_denied==1 && l.CubeTexture(s,"sky")==fallback && f->texture_calls==calls,"denied cubemap uses same type-correct cube fallback with no repeated upload");
    l.texture_registry.Begin();l.texture_registry.budget=2100;l.NeedTexture("sky",kich_r4_texture::EFFECT);
    Require(l.CommitTexturePlan(s)==0 && l.CubeTexture(s,"sky")!=fallback && cube_faces[l.CubeTexture(s,"sky")]==6,"cubemap recovers from pressure to original source");Stop(l,s);
    Fixture lost;f=&lost;KichR4LevelSession broken;KichR4FrameSession device={};Setup(broken,device);f->fail=f->calls+1;
    Require(broken.CubeTexture(device,"")==0 && device.failure==error,"fatal fallback allocation reports device loss");Stop(broken,device);
}
void NativeLoading(){Fixture fixture;f=&fixture;KichR4LevelSession l;KichR4FrameSession s={};Setup(l,s);
    l.texture_override=[](void*,const std::string&,bool& handled)->KichR4Handle{handled=false;return 0;};
    const auto white=l.white,checker=l.checker;const auto calls=f->texture_calls;
    Require(l.Start(s,"l01_escape")==0 && l.white==white && l.checker==checker,"native level startup reuses existing placeholders");
    Require(f->texture_calls==calls && l.texture_registry.entries.empty(),"native startup does not preload off-camera level DDS materials");Stop(l,s);
}
void Preflight(){Fixture fixture;f=&fixture;KichR4LevelSession l;KichR4FrameSession s={};Setup(l,s);
    const char* names[]={"static","bump","aux","lightmap","hemi","detail","model","model_bump","model_aux","hud","glow","console","sky","sky2","caster","opaque_offscreen"};
    for(const char* name:names)f->files[std::string("$game_textures$/")+name+".dds"]=fixture::Dds(8,8,4);
    l.texture_registry.budget=7*340;
    l.scene.materials.push_back(kich_r4_level::Material("deffer","static"));auto& material=l.scene.materials.back().descriptor;
    material.semantic=kich_r4_material::MATERIAL_OPAQUE;material.passes=kich_r4_material::PASS_GEOMETRY|kich_r4_material::PASS_SHADOW;
    material.base="static";material.bump="bump";material.bump_aux="aux";material.lightmap="lightmap";material.hemi="hemi";material.detail="detail";
    kich_r4_level::Draw draw={};draw.count=3;draw.world=kich_r4_scene::Identity();draw.low={-.1f,-.1f,.1f};draw.high={.1f,.1f,.2f};l.scene.draws.push_back(draw);
    l.scene.materials.push_back(kich_r4_level::Material("aref","caster",true));l.scene.materials.back().descriptor.base="caster";
    draw.material=1;draw.low={10,-.1f,.1f};draw.high={11,.1f,.2f};l.scene.draws.push_back(draw);
    l.scene.materials.push_back(kich_r4_level::Material("deffer","opaque_offscreen"));draw.material=2;l.scene.draws.push_back(draw);
    kich_r4_capture::Batch batch;kich_r4_capture::Style style;
    batch.commands.push_back({0,3,2,"console",style});batch.commands.push_back({0,3,0,"glow",style});batch.commands.back().effect=1;
    batch.commands.push_back({0,3,0,"sky",style});batch.commands.back().effect=2;batch.commands.back().secondary="sky2";
    kich_r4_capture::NativeModelCommand model;model.count=3;model.texture="model";model.has_material=true;model.material=l.scene.materials[0].descriptor;model.material.base="model";model.material.bump="model_bump";model.material.bump_aux="model_aux";batch.native_models.push_back(model);
    model.layer=1;model.texture="hud";model.has_material=false;batch.native_models.push_back(model);
    KichR4GameFrame input={};const auto identity=kich_r4_scene::Identity();memcpy(input.view_projection,identity.m,sizeof(input.view_projection));input.lighting.sun_direction[1]=-1;input.lighting.sun_color[0]=1;
    KichR4DetailSession details;
    Require(kich_r4_texture::Preflight(s,l,batch,input,details,true)==0,"production preflight plans static/model/all material slots and captured UI/effects");
    for(const char* name:names)if(std::string(name)!="opaque_offscreen")Require(l.Texture(s,name)!=l.checker,"preflight dependency ready before any shadow or world draw");
    Require(!l.texture_registry.entries.count("opaque_offscreen"),"opaque off-camera shadow caster does not reserve base texture");
    Require(l.texture_registry.entries["caster"].role==kich_r4_texture::SHADOW_ALPHA,"off-camera cutout caster alpha is included in shadow union");
    const auto calls=f->texture_calls;const auto opens=f->opens;
    for(unsigned frame=0;frame<30;++frame){l.texture_registry.Begin();Require(kich_r4_texture::Preflight(s,l,batch,input,details,true)==0,"stationary production preflight repeats");}
    Require(f->texture_calls==calls && f->opens==opens,"production preflight is stable under saturation");Stop(l,s);}
}
int main(){try{(void)&Scenario;Transaction();Saturation();Cubemap();CubeFallback();NativeLoading();Preflight();printf("E1.2 transactional/frame working-set residency: %d checks passed\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
