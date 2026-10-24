#include "recording.h"
#include <limits>
namespace {
void Near(float got,float expected,const char* why,float tolerance=.00001f){Require(std::isfinite(got) && fabsf(got-expected)<tolerance,why);}
void RangeAndPlane(){
    using namespace kich_r4_lighting;
    KichR4LightingFrame environment;environment.fog_valid=1;environment.fog_near=20;environment.fog_far=120;
    const Vec3 eye={530,12,-780},forward={0,0,1};
    const Matrix view=StableView(eye,forward),projection=Projection(1.1f,.1f,250),vp=Multiply(view,projection);
    kich_r4_lighting::Constants constants={};FogConstants(environment,vp,constants);
    Near(constants.fog_params[0],-.2f,"radial offset uses mixed weather near");
    Near(constants.fog_params[1],.01f,"radial reciprocal uses mixed weather interval");
    Require(constants.fog_params[2]==0 && constants.fog_params[3]==1,"valid forward plane remains disabled until an authored world draw opts in");
    for(unsigned i=0;i<3;++i){const float distance=20+50.f*i;
        Near(distance*constants.fog_params[1]+constants.fog_params[0],.5f*i,"near/mid/far radial fog matches R2 range");}
    // R2's view plane is offset by n*f/(2*f-n), unlike radial distance.
    // Independent analytic oracle for a translated +Z perspective camera.
    const float near_clip=.1f,far_clip=250,plane_shift=near_clip*far_clip/(2*far_clip-near_clip);
    for(float z:{0.f,20.f,70.f,120.f}){
        const float point[3]={eye.x+41,eye.y-8,eye.z+z};float actual=constants.fog_plane[3];
        for(unsigned j=0;j<3;++j)actual+=point[j]*constants.fog_plane[j];
        Near(actual,(120-z+plane_shift)/100,"forward fog is the R2 normalized view plane, including translated camera",.00003f);
    }
    const float quiet_nan=std::numeric_limits<float>::quiet_NaN();
    const float invalid[][2]={{120,20},{20,20},{quiet_nan,120},{20,quiet_nan}};
    for(const auto& range:invalid){environment.fog_near=range[0];environment.fog_far=range[1];FogConstants(environment,vp,constants);
        for(unsigned j=0;j<4;++j)Require(constants.fog_params[j]==0 && constants.fog_plane[j]==0,"bad environment fog cannot propagate NaNs or stale coefficients");}
    environment.fog_near=20;environment.fog_far=120;environment.fog_valid=0;FogConstants(environment,vp,constants);
    Require(constants.fog_params[1]==0 && constants.fog_params[3]==0,"missing environment has no invented fog distance");
    environment.fog_valid=1;Matrix singular={};FogConstants(environment,singular,constants);
    Require(constants.fog_params[1]>.0f && constants.fog_params[3]==0,"invalid forward plane disables that path independently of radial fog");
}
kich_r4_capture::Batch Batch(){
    using namespace kich_r4_capture;kich_r4_capture::Batch batch;batch.Begin(960,540,7);batch.renderer_seen=batch.ui_seen=true;
    // Full sky, normal world alpha, additive world, unfogged world, HUD, UI,
    // particle/glow, clouds, water, and celestial source. All but #3 explicitly
    // carry source fog=true, proving layer/effect exclusions independently.
    const uint32_t effects[]={2,0,0,0,0,0,1,3,4,kich_r4_optics::CelestialSource};
    for(unsigned j=0;j<10;++j){Style style;style.blend=1;style.write=0;style.fog_enabled=j!=3;
        if(j==0){style.depth=style.write=1;} // captured sky depth must be corrected
        if(j==5)style.depth=0;
        if(j==2)style.src=style.dst=2;
        Command command(j*3,3,j==4?1u:j==5?2u:0u,"base",style);command.effect=effects[j];batch.commands.push_back(command);
        for(unsigned i=0;i<3;++i){Vertex vertex={};vertex.p[0]=i==0?-.1f:.1f;vertex.p[1]=i==1?.1f:-.1f;vertex.p[2]=.5f;vertex.p[3]=1;vertex.color=0xffffffffu;
            vertex.world[2]=.5f;batch.indices.push_back(static_cast<uint32_t>(batch.vertices.size()));batch.vertices.push_back(vertex);}}
    return batch;
}
void DrawContracts(){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();const auto apis=DeferredAPIs();
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(apis,files,"l01_escape",NULL);game.deferred.native_mode=true;
    auto input=DeferredInput();input.engine_frame=7;input.lighting.fog_valid=1;input.lighting.fog_near=20;input.lighting.fog_far=120;
    input.lighting.count=1;auto& point=input.lighting.lights[0];point.type=1;point.range=15;point.position[1]=2;point.color[0]=1;
    auto batch=Batch();Require(game.Render(input,&batch)==1,"fog-enabled native frame presents");
    unsigned sky=0,combine=0,seen[10]={};
    for(size_t i=0;i<draw_entries.size();++i){const auto& draw=draw_descriptions[i];const auto& constant=draw_constants[i];
        if(draw_entries[i]=="PSCombine"){++combine;Require(draw.state.blend_enable && draw.state.source_blend==6 && draw.state.dest_blend==5,"normal combine uses inverse-alpha/source-alpha over complete sky");}
        if(draw.state.draw.vertex_buffer!=game.stream.vb)continue;
        const unsigned index=draw.state.draw.start_index/3;++seen[index];
        const float expected=index==1 || index==8?1.f:index==2?2.f:0.f;
        Near(constant.fog_params[2],expected,"only explicitly fogged world surfaces opt in; HUD/UI/optical effects excluded");
        Near(constant.fog_params[1],.01f,"all draws share the selected frame's mixed fog interval");
        if(index==0){++sky;Require(draw_entries[i]=="PSSky" && !draw.state.draw.depth_mode && !draw.state.depth_write,"sky fills behind all opaque geometry without clearing the celestial depth mask");}
    }
    Require(sky==1 && combine==1,"single complete sky and normal combine");
    for(unsigned j=0;j<10;++j)Require(seen[j]>=1,"all excluded and opted-in source types exercised");
    Require(seen[1]==2 && seen[2]==2,"base and additive local light both keep the same source fog scope");
    draw_entries.clear();draw_descriptions.clear();draw_constants.clear();
    const KichR4FrameColor clear={sizeof(clear),{0,0,0,1}};Require(game.frame.BeginClear(clear)==0,"begin diagnostic frame");
    Require(game.deferred.Draw(game.frame,game.level,game.stream,input,&batch,2)==0,"normal diagnostic submits");
    for(size_t i=0;i<draw_entries.size();++i)if(draw_entries[i]=="PSCombine")Require(!draw_descriptions[i].state.blend_enable,"debug combine remains opaque instead of blending diagnostic colors with sky");
    Require(game.frame.Present()==0,"finish diagnostic frame");
    KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty(),"fog fixture releases all resources");
}
}
int main(){try{(void)&Scenario;RangeAndPlane();DrawContracts();printf("E1.8 fog ranges, R2 plane, sky blend and scoped forward draws: %d checks passed (no GPU rasterization)\n",checks);return 0;}
catch(const std::exception& error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
