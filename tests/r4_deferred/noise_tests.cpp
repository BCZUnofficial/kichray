#include "recording.h"
#include "../../xr_3da/KichR4TexturePreflight.h"
#include <limits>
namespace {
void ClockAndUV(){
    using namespace kich_r4_noise;
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    Require(Amount(0)==0 && Amount(-1)==0 && Amount(nan)==0 && Amount(1)==1,"noise strength endpoints and invalid values");
    Require(std::fabs(Amount(.5f)-128.f/255)<1e-6f,"R2 byte-quantized noise alpha");
    Cadence clock;Require(clock.Advance(10,.01f,25)==1,"first timed frame selects a noise pair");
    Require(clock.Advance(10,.01f,25)==1 && clock.Advance(11,.01f,25)==1,"same frame and subperiod retain pair");
    Require(clock.Advance(12,.021f,25)==2,"noise changes at requested cadence");
    const float before=clock.Advance(13,1000000.f,25);Require(before==3 && clock.remaining>=0 && clock.remaining<=.04,"long frame catches up in constant time and chooses one pair");
    Require(clock.Advance(14,.1f,0)==before && clock.Advance(15,.1f,inf)==before && clock.Advance(16,nan,25)==before,"invalid FPS/delta never divides or spins");
    Require(clock.Advance(1,.001f,25)==before+1,"frame rewind resets timer");
    for(unsigned rate:{30u,60u,144u}){Cadence c;unsigned changes=0;float previous=0;
        for(unsigned i=0;i<rate*4;++i){const float now=c.Advance(i,1.f/rate,25);changes+=now!=previous;previous=now;}
        Require(changes>=100 && changes<=101,"25 Hz is independent of display frame rate");}
    float uv[4]={};Require(Coordinates(1920,1080,64,32,.5f,17,uv),"rectangular texture scaled using real dimensions");
    Require(uv[2]==61 && uv[3]==68,"R2 integer tile counts plus one tile on both axes");
    Require(uv[0]>0 && uv[0]<1 && uv[1]>0 && uv[1]<1,"independent half-texel origins lie inside noise tile");
    Require(std::fabs(uv[0]*32-.5f-std::floor(uv[0]*32))<1e-6f && std::fabs(uv[1]*16-.5f-std::floor(uv[1]*16))<1e-6f,"origins are texel centers, not continuous diagonal drift");
    float same[4]={};Coordinates(1920,1080,64,32,.5f,17,same);Require(!memcmp(uv,same,sizeof(uv)),"fixed cadence token has no per-frame motion");
    Coordinates(1920,1080,64,32,.5f,18,same);Require(memcmp(uv,same,8)!=0,"next cadence token changes noise phase");
    unsigned equal=0;for(unsigned i=0;i<256;++i){Coordinates(960,540,64,64,1,float(i),same);equal+=same[0]==same[1];}
    Require(equal<16,"U and V do not share one random shift");
    Require(!Coordinates(960,540,64,64,0,0,uv) && !Coordinates(960,540,64,64,inf,0,uv) && !Coordinates(960,540,0,64,1,0,uv),"invalid scale or dimensions disable noise safely");
}
void Submission(bool available){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session game(DeferredAPIs(),fs,"l01_escape",NULL);
    if(available)f->files["$game_textures$/fx\\fx_noise2.dds"]=fixture::Dds(64,32);
    auto input=DeferredInput();Require(game.Render(input)==1,"postprocess resource setup");
    kich_r4_capture::Batch batch;batch.Begin(input.width,input.height,0);
    game.level.texture_registry.Begin();
    Require(kich_r4_texture::Preflight(game.frame,game.level,batch,input,game.deferred.details,true)==0,"zero noise preflight");
    Require(game.level.texture_registry.entries.count(kich_r4_noise::Texture())==0,"zero noise does not request texture");
    input.postprocess[7]=.5f;input.postprocess[8]=17;input.postprocess[9]=.5f;
    Require(kich_r4_texture::Preflight(game.frame,game.level,batch,input,game.deferred.details,false)==0 && game.level.texture_registry.entries.count(kich_r4_noise::Texture())==0,"menu preflight does not request scene noise");
    Require(kich_r4_texture::Preflight(game.frame,game.level,batch,input,game.deferred.details,true)==0,"active noise joins preflight residency");
    const auto& texture=game.level.texture_registry.entries.at(kich_r4_noise::Texture());
    Require(texture.wanted==game.level.texture_registry.epoch && texture.role==kich_r4_texture::EFFECT,"noise dependency participates in full-frame residency plan");
    const auto uploads=f->texture_calls;const auto opens=f->opens;
    const KichR4FrameColor clear={sizeof(clear),{0,0,0,1}};Require(game.frame.BeginClear(clear)==0,"noise isolated frame");
    Require(game.deferred.Process(game.frame,game.level,input)==0,"production postprocess submission");
    const auto& draw=draw_descriptions.back();const auto& constants=draw_constants.back();
    Require(draw_entries.back()=="PSPostprocess" && draw.textures[0]==game.deferred.final_color,"postprocess reads completed scene");
    Require(!draw.state.clamp_texture && draw.state.surface.samplers[0].u==3 && draw.state.surface.samplers[1].u==1,"scene clamps while noise wraps despite backend global clamp override");
    Require(draw.state.surface.samplers[1].min_filter==2 && draw.state.surface.samplers[1].mag_filter==2 && draw.state.surface.samplers[1].mip_filter==2,"noise uses linear wrap sampler like donor R4");
    if(available){Require(draw.textures[1]==texture.handle && texture.handle && texture.handle!=game.level.checker,"real noise DDS is bound, never checker");
        Require(std::fabs(constants.light_position[3]-128.f/255)<1e-6f && constants.light_direction[2]==31 && constants.light_direction[3]==34,"actual texture dimensions and quantized amount reach shader");}
    else Require(!draw.textures[1] && constants.light_position[3]==0,"missing noise is neutral and never samples fallback debug texture");
    Require(f->texture_calls==uploads && f->opens==opens,"postprocess performs no late IO or GPU upload");
    Require(game.deferred.Process(game.frame,game.level,input,true)==0 && draw_constants.back().light_position[3]==0 && !draw_descriptions.back().textures[1],"menu explicitly disables and unbinds noise");
    Require(game.frame.Present()==0,"noise frame completes");KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty(),"noise texture uses existing resource ownership and cleanup");
}
}
int main(){try{(void)&Scenario;ClockAndUV();Submission(true);Submission(false);printf("E1.4 R2 texture noise cadence/UV/residency/samplers: %d checks passed (no GPU rasterization)\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
