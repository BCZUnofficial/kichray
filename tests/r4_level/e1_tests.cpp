#include "recording.h"
#include "../../xr_3da/KichR4OpticalRules.h"
#include "../../xr_3da/KichR4SurfaceState.h"
#include "../../xr_3da/KichR4StreamSession.h"
namespace {
int32_t ReleaseTexture(KichR4Handle,KichR4Handle handle){Require(f->objects.erase(handle)==1,"eviction releases live handle once");++f->released;return 0;}
void Recovery(){Fixture fixture;f=&fixture;KichR4LevelSession l;KichR4FrameSession s={};Configure(l,s);l.bridge.resources.release_resource=ReleaseTexture;
    Require(s.Start(reinterpret_cast<void*>(1),640,480)==0 && l.Placeholder(s,false,&l.white)==0 && l.Placeholder(s,true,&l.checker)==0,"recovery setup");
    const auto old=l.Texture(s,"base");Require(old!=l.checker && l.Texture(s,"BASE.DDS")==old,"normalized aliases reuse resident handle");
    l.texture_registry.budget=l.texture_registry.bytes;
    Require(l.Texture(s,"leaf")==l.checker && f->objects.count(old),"current submission pins cannot be evicted by another material");
    l.texture_registry.Begin();l.texture_registry.Begin();const auto leaf=l.Texture(s,"leaf");
    Require(leaf!=l.checker && !f->objects.count(old) && l.texture_registry.evictions==1,"budget failure retries, evicts unpinned LRU and becomes ready");
    l.texture_registry.Begin();const auto restored=l.Texture(s,"base");Require(restored!=l.checker && restored!=old && !f->objects.count(leaf),"evicted source resolves a new live handle");
    Require(l.Texture(s,"late")==l.checker,"missing source initially uses explicit checker");f->files["$game_textures$/late.dds"]=fixture::Dds();
    for(unsigned i=0;i<300;++i)l.texture_registry.Begin();
    Require(l.Texture(s,"late")!=l.checker,"missing source can recover without restarting scene");
    l.texture_registry.Begin();l.texture_registry.budget=128;f->files["$game_textures$/mipped.dds"]=fixture::Dds(8,8,4);
    Require(l.Texture(s,"mipped")!=l.checker && l.texture_registry.entries["mipped"].width==4 && l.texture_registry.bytes==84,"pressure selects only existing lower mips with exact accounting");
    f->files["$game_textures$/single.dds"]=fixture::Dds();Require(l.Texture(s,"single")==l.checker && l.texture_registry.entries["single"].reason==kich_r4_texture::BudgetPressure,"oversized no-mip image is recoverable pressure, not invalid DDS");
    KichR4GeometryReport report={};Require(l.bridge.Stop(s,&report)==0 && f->objects.empty() && f->opens==f->closes,"recovery balances reader and GPU ownership");
}
void UIAdmission(){Fixture fixture;f=&fixture;KichR4LevelSession l;KichR4FrameSession s={};Configure(l,s);l.bridge.resources.release_resource=ReleaseTexture;
    Require(s.Start(reinterpret_cast<void*>(1),640,480)==0 && l.Placeholder(s,false,&l.white)==0 && l.Placeholder(s,true,&l.checker)==0,"UI admission setup");
    for(const char* name:{"a_icon","z_console","world0","world1","world2","world3","world4","device_fail"})f->files[std::string("$game_textures$/")+name+".dds"]=fixture::Dds();
    l.texture_registry.budget=3*256;
    const auto console=l.Texture(s,"z_console"),world=l.Texture(s,"world0");l.Texture(s,"world1");
    Require(l.Texture(s,"a_icon")==l.checker && l.texture_registry.entries["a_icon"].reason==kich_r4_texture::BudgetPressure,"reproduce full pinned world denying late UI atlas");
    kich_r4_capture::Batch b;kich_r4_capture::Style style;
    b.commands.push_back({0,3,2,"a_icon",style});b.commands.push_back({3,3,2,"z_console",style});b.commands.back().console=true;
    b.commands.push_back({6,3,2,"a_icon",style});b.commands.push_back({9,3,0,"world0",style});
    l.texture_registry.Begin();KichR4StreamSession stream;
    Require(stream.PrepareUI(s,l,b)==0 && stream.ui_texture_sources==2 && stream.ui_texture_ready==2 && !stream.ui_texture_fallbacks,"UI priority retries budget failure immediately and deduplicates sources");
    const auto icon=l.Texture(s,"a_icon");
    Require(f->objects.count(console) && l.Texture(s,"z_console")==console && !f->objects.count(world),"two-phase admission preserves later console atlas while evicting old world entry");
    for(unsigned frame=2;frame<5;++frame){
        l.texture_registry.Begin();const auto calls=f->texture_calls;
        Require(stream.PrepareUI(s,l,b)==0 && f->texture_calls==calls,"resident UI preflight performs no new DDS upload");
        const std::string name=std::string("world")+char('0'+frame);
        Require(l.Texture(s,name)!=l.checker && l.Texture(s,"a_icon")==icon && l.Texture(s,"z_console")==console,"world churn preserves both UI atlases and shared-source handles");
        Require(l.texture_registry.bytes<=l.texture_registry.budget,"UI priority honors total residency budget");
    }
    l.texture_override=[](void* context,const std::string& key,bool& handled)->KichR4Handle{handled=key=="r4_runtime__1";return handled?static_cast<KichR4LevelSession*>(context)->white:0;};l.texture_context=&l;
    b.commands.push_back({12,3,2,"r4_runtime__1",style});l.texture_registry.Begin();
    Require(stream.PrepareUI(s,l,b)==0 && stream.ui_texture_ready==3 && !l.texture_registry.entries.count("r4_runtime__1"),"runtime UI remains routed through override and outside DDS residency");
    b.commands.push_back({15,3,2,"missing_ui",style});l.texture_registry.Begin();
    Require(stream.PrepareUI(s,l,b)==0 && stream.ui_texture_fallbacks==1 && l.texture_registry.entries["missing_ui"].reason==kich_r4_texture::SourceUnavailable,"genuinely missing UI keeps explicit checker and failure diagnosis");
    const auto retry=l.texture_registry.entries["missing_ui"].retry;l.texture_registry.Begin();
    Require(stream.PrepareUI(s,l,b)==0 && l.texture_registry.entries["missing_ui"].retry==retry,"UI priority does not bypass missing-source retry cooldown");
    l.texture_registry.budget+=256;b.commands.push_back({18,3,2,"device_fail",style});l.texture_registry.Begin();f->fail=f->calls+1;
    Require(stream.PrepareUI(s,l,b)==error && s.failure==error,"fatal device loss during early UI upload reaches frame failure");
    KichR4GeometryReport report={};Require(l.bridge.Stop(s,&report)==0 && f->objects.empty() && f->opens==f->closes,"UI admission balances readers and GPU ownership");
}
void Layouts(){using namespace kich_r4_texture;
    Require(Layout(3,2,16,28) && !Layout(3,2,16,27) && !Layout(3,2,11,100) && !Layout(16385,1,65540,65540),"runtime pitched rows and overflow boundaries");
    auto d=fixture::Dds();fixture::Set(d,8,0x100f);fixture::Set(d,12,2);fixture::Set(d,16,1);fixture::Set(d,20,4);fixture::Set(d,28,1);
    fixture::Set(d,80,0x40);fixture::Set(d,88,24);fixture::Set(d,92,0xff0000);fixture::Set(d,96,0xff00);fixture::Set(d,100,0xff);fixture::Set(d,104,0);d.resize(136);
    kich_r4_dds::Image image={};Require(kich_r4_dds::Parse(d.data(),d.size(),16384,image) && image.mips[0].row_pitch==4 && kich_r4_dds::UploadBytes(image)==8,"DWORD-padded RGB24 DDS has exact GPU payload");
    Require(!kich_r4_dds::Parse(d.data(),135,16384,image),"truncated padded row rejected");
    auto large=fixture::Dds();fixture::Set(large,12,4096);fixture::Set(large,16,4096);fixture::Set(large,20,16384);fixture::Set(large,28,1);large.resize(128+4096u*4096u*4);
    Require(kich_r4_dds::Parse(large.data(),large.size(),16384,image) && image.width==4096,"valid DDS above former 64MiB file cap and 2048 dimension cap");
    Require(!kich_r4_dds::Parse(large.data(),large.size(),2048,image),"no mip chain never invents a lower-resolution image");
}
void StatesAndGlow(){auto s=KichR4DefaultSurface();Require(KichR4ValidSurface(s),"default surface valid");
    s=KichR4ReadSurface([](uint32_t key,uint32_t fallback){return key==58 || key==59?UINT32_MAX:fallback;});Require(KichR4ValidSurface(s) && s.stencil_read==255,"D3D9 all-bits stencil masks normalize to D24S8 width");
    KichR4ReceiverState(s,true);Require(s.stencil_write==1 && s.stencil_pass==3 && s.stencil_func==8,"opaque receiver replaces only bit zero");
    KichR4ReceiverState(s,false);Require(s.stencil_write==0 && s.stencil_func==3 && s.stencil_ref==1,"wallmark only reads receiver bit");
    s.samplers[0]=KichR4ReadSampler([](uint32_t key,uint32_t fallback){return key==1?3u:fallback;});Require(KichR4ValidSurface(s) && s.samplers[0].u==3 && s.samplers[0].v==1,"sampler U and V remain independent");
    s.samplers[0].anisotropy=17;Require(!KichR4ValidSurface(s),"unsupported sampler cannot reach native API");
    s.samplers[0].anisotropy=1;s.samplers[0].lod_bias_bits=0x7fc00000;Require(!KichR4ValidSurface(s),"NaN mip bias rejected before D3D state creation");
    kich_r4_optics::GlowVisibility glow;float eye[3]={};const float first=glow.Step(true,100,eye);Require(first>0 && first<1,"glow appears gradually");
    Require(glow.Step(true,200,eye)>first,"visibility rises with bounded time");const float previous=glow.value;Require(glow.Step(false,216,eye)<previous && glow.value>0,"occlusion fades rather than toggles");
    Require(glow.Step(true,10000,eye)<.2f,"pause resets stale visibility");eye[0]=100;Require(glow.Step(false,10016,eye)==0,"camera teleport resets history");
    glow.Reset();Require(!glow.known && !glow.value,"source disable clears ghost");
}
}
int main(){try{(void)&Scenario;Recovery();UIAdmission();Layouts();StatesAndGlow();printf("E1 recovery/UI admission/layout/state/glow: %d checks passed\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
