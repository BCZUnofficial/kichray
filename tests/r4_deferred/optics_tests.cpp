#include "recording.h"
#include <limits>
namespace {
std::vector<KichR4PassDesc> draw_passes;
int32_t OpticalDraw(KichR4Handle device,const KichR4DeferredDrawDesc* draw){
    const int32_t result=DDraw(device,draw);if(!result)draw_passes.push_back(active_pass);return result;
}
void Facing(){
    using kich_r4_optics::GlowFacing;
    const float zero[3]={},front[3]={0,0,2},back[3]={0,0,-2},side[3]={2,0,0},angle[3]={2,0,2};
    Require(GlowFacing(zero,front)==1 && GlowFacing(zero,back)==1,"static zero-direction glow stays omnidirectional");
    Require(GlowFacing(front,front)==1 && GlowFacing(front,back)==0 && GlowFacing(front,side)==0,"directed glow rejects rear and side observers");
    Require(fabsf(GlowFacing(front,angle)-.70710678f)<1.e-6f,"non-unit direction has normalized angular attenuation");
    Require(GlowFacing(front,zero)==0,"coincident directed emitter cannot divide by zero");
    const float bad[3]={std::numeric_limits<float>::quiet_NaN(),0,0};
    Require(GlowFacing(bad,front)==0,"invalid direction cannot emit NaN intensity");
    float clip[4]={.2f,-.3f,.5f,2};kich_r4_optics::SkyDepth(clip);
    Require(clip[0]==.2f && clip[1]==-.3f && clip[2]==clip[3]*.5f,"source keeps XY/W away from far clip boundary");
    kich_r4_capture::Style style;kich_r4_optics::SkyStyle(style);
    Require(!style.depth && !style.write,"sky coverage uses shader depth sample, never interpolated depth comparison");
}
kich_r4_capture::Batch MakeOpticalBatch(){
    using namespace kich_r4_capture;Batch batch;batch.Begin(960,540,7);batch.renderer_seen=batch.ui_seen=true;
    // Reverse capture order deliberately: ordinary glow, source, clouds, sky.
    const uint32_t effects[]={1,kich_r4_optics::CelestialSource,3,2};
    for(unsigned j=0;j<4;++j){Style style;style.blend=1;style.src=style.dst=2;style.write=0;
        if(j==1){style.depth=0;style.min_z=.1f;style.max_z=.8f;} // planner must enforce sky depth state
        Command c(j*3,3,0,"base",style);c.effect=effects[j];batch.commands.push_back(c);
        for(unsigned i=0;i<3;++i){Vertex v={};v.p[0]=i==0?-.1f:.1f;v.p[1]=i==1?.1f:-.1f;v.p[2]=.5f;v.p[3]=1;v.color=0xffffffffu;
            if(j==1)kich_r4_optics::SkyDepth(v.p);
            batch.indices.push_back(static_cast<uint32_t>(batch.vertices.size()));batch.vertices.push_back(v);}}
    return batch;
}
void Frame(){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();draw_passes.clear();auto apis=DeferredAPIs();apis.deferred.draw=OpticalDraw;
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(apis,files,"l01_escape",NULL);game.deferred.native_mode=true;
    auto input=DeferredInput();input.engine_frame=7;
    // Exercise local-light additive path: source must not be repeated there.
    input.lighting.count=1;auto& point=input.lighting.lights[0];point.type=1;point.range=15;point.position[1]=2;point.color[0]=1;
    auto batch=MakeOpticalBatch();Require(game.Render(input,&batch)==1,"native optical frame presents");
    size_t source_at=0,combine_at=0,glow_at=0,sky_at=0,clouds_at=0;unsigned sources=0,glows=0;
    for(size_t i=0;i<draw_entries.size();++i){const auto& d=draw_descriptions[i];
        if(draw_entries[i]=="PSSky")sky_at=i;
        if(draw_entries[i]=="PSClouds")clouds_at=i;
        if(draw_entries[i]=="PSCombine")combine_at=i;
        if(d.state.draw.vertex_buffer!=game.stream.vb)continue;
        if(d.state.draw.start_index==3){++sources;source_at=i;
            Require(draw_entries[i]=="PSCelestial" && draw_constants[i].alpha[3]==0,"source uses dedicated unlit mask shader");
            Require(draw_passes[i].colors[0]==game.deferred.scene_color && !draw_passes[i].depth && d.textures[1]==game.deferred.depth,"source samples completed depth without DSV hazard");
            Require(!(draw_passes[i].clear_flags&KICH_R4_CLEAR_DEPTH),"sky source never clears opaque geometry mask");
            Require(!d.state.draw.depth_mode && !d.state.depth_write && d.state.min_depth==0 && d.state.max_depth==1,"source disables fixed-function depth despite captured state");
            Require(d.state.blend_enable && d.state.source_blend==2 && d.state.dest_blend==2,"source preserves original blend contract");
        }else if(d.state.draw.start_index==0){++glows;glow_at=i;Require(draw_entries[i]=="PSPlain","ordinary glow retains existing forward shader");}}
    Require(sources==1 && glows==1,"sun source and ordinary glow submitted exactly once");
    Require(sky_at<source_at && source_at<combine_at && combine_at<clouds_at && clouds_at<glow_at,"sky -> source -> combine -> clouds -> ordinary glow ordering");
    for(unsigned mode=6;mode<kich_r4_optics::DebugViewCount;++mode){
        draw_entries.clear();draw_descriptions.clear();draw_constants.clear();draw_passes.clear();
        const KichR4FrameColor clear={sizeof(clear),{0,0,0,1}};Require(game.frame.BeginClear(clear)==0,"begin diagnostic frame");
        Require(game.deferred.Draw(game.frame,game.level,game.stream,input,&batch,mode)==0,"diagnostic pass submits");
        unsigned isolated=0;
        for(size_t i=0;i<draw_entries.size();++i){
            Require(draw_entries[i]!="PSPostprocess","diagnostic bypasses postprocess");
            if(draw_entries[i]=="PSCelestial"){++isolated;
                Require(mode>=10 && draw_passes[i].colors[0]==game.deferred.final_color,"isolated source bypasses scene composition");
                Require(draw_constants[i].screen[2]==mode && !draw_passes[i].depth,"mask/triangle diagnostic selected explicitly");
                if(mode==12)Require(!draw_descriptions[i].state.blend_enable,"triangle colors cannot be altered by blend");}}
        Require(isolated==(mode>=10?1u:0u),"source submitted once only in source diagnostic modes");
        Require(game.frame.Present()==0,"finish diagnostic frame");
    }
    batch.Begin(960,540,8);batch.renderer_seen=batch.ui_seen=true;input.engine_frame=8;
    draw_entries.clear();draw_descriptions.clear();draw_constants.clear();draw_passes.clear();
    Require(game.Render(input,&batch)==1,"empty next frame presents without stale source");
    for(const auto& d:draw_descriptions)Require(d.state.draw.vertex_buffer!=game.stream.vb,"empty next frame cannot replay captured optical draws");
    KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty(),"optical fixture releases all resources");
}
}
int main(){try{(void)&Scenario;Facing();Frame();printf("G3e glow and sampled celestial mask/order/debug: %d checks passed (no GPU rasterization)\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
