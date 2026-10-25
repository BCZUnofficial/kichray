#include "recording.h"
#include "../../xr_3da/KichR4TexturePreflight.h"
#include <fstream>
#include <sstream>
namespace {
void Terrain(unsigned mode){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();const KichR4LevelFiles fs={Open,Close};
    kich_r4_game::Session game(DeferredAPIs(),fs,"l01_escape",NULL);auto input=DeferredInput();
    Require(game.Render(input)==1,"terrain setup frame");game.deferred.native_mode=true;
    using namespace kich_r4_material;Source source;source.family=FAMILY_OPAQUE;
    auto& m=source.descriptor;m.special=SPECIAL_TERRAIN;m.base="base";m.shader="terrain_authored";
    m.detail_scale[0]=7;m.detail_scale[1]=11;m.detail_usage=3;m.terrain_detail=mode!=4;
    m.terrain_mask="terrain_mask";m.terrain_lmap="terrain_sun";
    for(unsigned i=0;i<4;++i){m.terrain_layers[i]="layer_"+std::to_string(i);m.terrain_bumps[i]=m.terrain_layers[i]+"_bump";
        f->files["$game_textures$/"+m.terrain_layers[i]+".dds"]=fixture::Dds(8,8);
        if(mode!=1 || i!=2)f->files["$game_textures$/"+m.terrain_bumps[i]+".dds"]=fixture::Dds(8,8);}
    f->files["$game_textures$/terrain_mask.dds"]=fixture::Dds(8,8);
    f->files["$game_textures$/terrain_sun.dds"]=fixture::Dds(8,8);
    if(mode==3){m.bump="authored_bump";m.bump_aux="authored_bump#";m.detail="authored_detail";
        for(const auto& name:{m.bump,m.bump_aux,m.detail})f->files["$game_textures$/"+name+".dds"]=fixture::Dds(8,8);}
    PassState state;state.surface.samplers[0].anisotropy=mode==0?4:1;const auto material=Build(source,state);
    for(auto& current:game.level.scene.materials){current.descriptor=material;current.kind=1;}
    for(auto& draw:game.level.scene.draws){draw.available_attributes=mode==2?ATTR_NORMAL:ATTR_NORMAL|ATTR_TANGENT|ATTR_BINORMAL;draw.tree=false;}
    ++game.level.revision;
    kich_r4_capture::Batch batch;batch.Begin(input.width,input.height,0);game.level.texture_registry.Begin();
    Require(kich_r4_texture::Preflight(game.frame,game.level,batch,input,game.deferred.details,true)==0,"all terrain dependencies join existing frame plan");
    const auto& registry=game.level.texture_registry;
    Require(registry.entries.at("terrain_sun").wanted==registry.epoch,"terrain source sun mask planned independently of generic UV1 maps");
    if(mode==3 || mode==4)Require(!registry.entries.count("terrain_mask") && !registry.entries.count("layer_0_bump"),"non-layer branches request no terrain layer assets");
    else Require(registry.entries.at("layer_0_bump").role==kich_r4_texture::NORMAL && registry.entries.at("layer_0").role==kich_r4_texture::DETAIL,"terrain normals have normal-map priority");
    const auto reads=f->opens;const auto uploads=f->texture_calls;
    const KichR4FrameColor clear={sizeof(clear),{0,0,0,1}};Require(game.frame.BeginClear(clear)==0,"terrain draw frame");
    const size_t first=draw_descriptions.size();
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(input.engine_frame);
    kich_r4_normal_probe::Global().BeginFrame(input.engine_frame);
    Require(game.deferred.Pass(game.frame,game.level,input.width,input.height,4,game.deferred.albedo,game.deferred.normal,game.deferred.depth,0,NULL,game.deferred.light,game.deferred.receiver_depth)==0 &&
        game.deferred.Static(game.frame,game.level,game.deferred.base.mvp,false)==0,"production static terrain submissions");
    Require(draw_descriptions.size()>first,"terrain geometry submitted");
    for(size_t i=first;i<draw_descriptions.size();++i){const auto& draw=draw_descriptions[i];const auto& c=draw_constants[i];const uint32_t flags=uint32_t(c.material[0]);
        Require((flags&SHADER_TERRAIN_HEMI) && (flags&SHADER_TERRAIN_SUN) && !(flags&(SHADER_LIGHTMAP|SHADER_HEMI|SHADER_VERTEX_LIGHT)),"terrain has base-alpha hemi and UV0 sun, not invented UV1 or baked RGB");
        Require(c.material[1]==7 && c.material[2]==11,"authored terrain scale reaches shader");
        Require(draw.textures[10] && draw.extra_samplers[2].u==1,"terrain sun mask uses extra slot10 with wrap sampler");
        if(mode==3){Require((flags&SHADER_BUMP) && !(flags&(SHADER_TERRAIN_LAYERS|SHADER_TERRAIN_NORMAL)),"authored base bump retains ordinary pair instead of forcing four layers");
            Require(draw.textures[1]==registry.entries.at("authored_bump").handle && draw.textures[2]==registry.entries.at("authored_bump#").handle && !draw.textures[8] && !draw.textures[9],"ordinary base normal slots survive terrain path");}
        else if(mode==4)Require(!(flags&(SHADER_TERRAIN_LAYERS|SHADER_TERRAIN_NORMAL|SHADER_BUMP)),"no authored detail usage remains geometric");
        else{Require(flags&SHADER_TERRAIN_LAYERS,"four authored diffuse layers remain active");
            Require(bool(flags&SHADER_TERRAIN_NORMAL)==(mode==0),"normal mapping requires all four source maps and valid basis");
            for(unsigned slot=1;slot<6;++slot)Require(draw.textures[slot]!=0 && draw.textures[slot]!=game.level.checker,"no diffuse layer checker sampled");
            // P0-S11: an invalid basis (mode 2) keeps the layer bumps for gloss only;
            // a missing layer normal map (mode 1) binds none of them.
            Require(bool(flags&SHADER_TERRAIN_GLOSS)==(mode==2),"layer gloss survives an invalid basis, without layer normal mapping");
            for(unsigned slot=6;slot<10;++slot)Require((draw.textures[slot]!=0)==(mode==0 || mode==2),"invalid normal maps produce explicit geometric fallback, not invented tangents");
            Require(draw.state.surface.samplers[2].anisotropy==state.surface.samplers[0].anisotropy,"terrain diffuse preserves captured filtering budget");
            if(mode==0)Require(draw.extra_samplers[0].u==1 && draw.extra_samplers[0].min_filter==2 && draw.extra_samplers[0].anisotropy==1 && draw.state.surface.samplers[2].min_filter==3,"layer-normal and diffuse samplers preserve R2 contracts");}
    }
    const auto& counts=kich_r4_normal_probe::Global().categories[kich_r4_normal_probe::Terrain];
    const kich_r4_normal_probe::Reason reasons[]={kich_r4_normal_probe::Mapped,kich_r4_normal_probe::MapsUnavailable,
        kich_r4_normal_probe::MissingBasis,kich_r4_normal_probe::Mapped,kich_r4_normal_probe::NoAuthoredBump};
    Require(counts.draws==draw_descriptions.size()-first && counts.reasons[reasons[mode]]==counts.draws,"opt-in terrain probe classifies actual successful source branches");
    Require(counts.mapped==((mode==0 || mode==3)?counts.draws:0),"terrain probe counts actual bound normal path");
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(input.engine_frame+1);
    Require(f->opens==reads && f->texture_calls==uploads,"terrain draw does not load or evict DDS after preflight");
    const size_t shadow=draw_descriptions.size();
    Require(game.deferred.Pass(game.frame,game.level,input.width,input.height,0,0,0,game.deferred.depth,0)==0 &&
        game.deferred.Static(game.frame,game.level,game.deferred.base.mvp,true)==0,"terrain retains shadow geometry in a depth-only pass");
    for(size_t i=shadow;i<draw_descriptions.size();++i){Require(!(uint32_t(draw_constants[i].material[0])&(SHADER_TERRAIN_HEMI|SHADER_TERRAIN_LAYERS|SHADER_TERRAIN_NORMAL)),"shadow shader does not execute optional terrain lighting");
        for(auto texture:draw_descriptions[i].textures)Require(!texture,"opaque terrain shadows bind no terrain maps");}
    Require(game.frame.Present()==0,"terrain frame complete");KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty(),"terrain assets obey existing resource lifetime");
}
}
int main(){try{(void)&Scenario;for(unsigned mode=0;mode<5;++mode)Terrain(mode);printf("E1.9 terrain source branches, preflight, actual bindings and fallback: %d checks passed\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
