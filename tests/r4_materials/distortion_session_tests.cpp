#include "../r4_deferred/recording.h"
#include "../../xr_3da/xrRender_R4/R4ModelRules.h"

namespace {
struct Submission {
    std::string pixel;KichR4PassDesc pass;KichR4DeferredDrawDesc draw;bool model;
};
std::vector<Submission> submissions;
std::map<KichR4Handle,KichR4Handle> model_pixel;

void Observe(const KichR4DeferredDrawDesc& d,const std::string& pixel,bool model){
    for(unsigned i=0;i<8;++i)if(d.textures[i]){
        Require(d.textures[i]!=active_pass.depth,"distortion source never aliases active depth");
        for(unsigned j=0;j<active_pass.color_count;++j)Require(d.textures[i]!=active_pass.colors[j],"distortion source never aliases active color target");
    }
    Submission record={pixel,active_pass,d,model};submissions.push_back(record);
}
int32_t RecordDraw(KichR4Handle device,const KichR4DeferredDrawDesc* draw){
    const int32_t result=DDraw(device,draw);
    if(!result)Observe(*draw,shader_entries.at(native_pipelines.at(draw->state.draw.pipeline).pixel_shader),false);
    return result;
}
int32_t ModelPipeline(KichR4Handle,const KichR4ModelPipelineDesc* d,KichR4Handle* out){
    Require(d->struct_size==sizeof(*d) && f->Find(d->vs).kind==KICH_R4_VERTEX_SHADER && f->Find(d->ps).kind==KICH_R4_PIXEL_SHADER,"native model shader pair valid");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);model_pixel[*out]=d->ps;return 0;
}
int32_t ModelPose(KichR4Handle,KichR4Handle* out){*out=f->Add(KICH_R4_CONSTANT_BUFFER);f->Find(*out).bytes.resize(sizeof(KichR4ModelPose));return 0;}
int32_t ModelUpload(KichR4Handle,KichR4Handle pose,const KichR4ModelPose* data){
    Require(ValidModelPose(*data),"native pose follows production ABI rules");memcpy(f->Find(pose).bytes.data(),data,sizeof(*data));return 0;
}
int32_t ModelDraw(KichR4Handle,const KichR4ModelDrawDesc* d){
    Require(d->struct_size==sizeof(*d) && ValidDeferredDraw(d->surface) && pass_active,"native model draw inside valid pass");
    const KichR4DrawIndexedDesc& draw=d->surface.state.draw;
    const Object& vb=f->Find(draw.vertex_buffer);const Object& ib=f->Find(draw.index_buffer);
    Require(vb.stride==sizeof(KichR4ModelVertex) && ib.stride==2 && ValidModelRange(draw.start_index,draw.index_count,static_cast<uint32_t>(ib.bytes.size()/2)),"native draw retains immutable model ranges");
    Require(f->Find(d->pose_buffer).bytes.size()==sizeof(KichR4ModelPose),"native model has explicit pose");
    Observe(d->surface,shader_entries.at(model_pixel.at(draw.pipeline)),true);return 0;
}
kich_r4_capture::Batch SourceBatch(){
    using namespace kich_r4_capture;Batch batch;batch.Begin(960,540,7);batch.renderer_seen=batch.ui_seen=true;
    for(unsigned triangle=0;triangle<3;++triangle){const uint32_t first=static_cast<uint32_t>(batch.indices.size());
        for(unsigned i=0;i<3;++i){Vertex v={};v.p[0]=i==0?-.1f:i==1?0:.1f;v.p[1]=i==1?.1f:-.1f;v.p[2]=.5f;v.p[3]=1;
            v.uv[0]=i==2?1.f:0.f;v.uv[1]=i==1?1.f:0.f;v.color=0xffffffff;v.normal[1]=v.normal[3]=1;
            v.world[0]=v.p[0];v.world[1]=v.p[1];v.world[2]=triangle==2?20.f:2.f;
            batch.indices.push_back(static_cast<uint32_t>(batch.vertices.size()));batch.vertices.push_back(v);}
        Style style;style.blend=1;style.write=0;style.clamp=triangle==2?1:0;
        Command c(first,3,0,triangle?"pfx\\pfx_distortion":"pfx\\pfx_ani-fire01",style);
        c.effect=triangle?5:1;c.shader=triangle?"particle_distort":"particle";c.shader_element=triangle?4:0;c.shader_pass=0;batch.commands.push_back(c);
    }
    std::shared_ptr<Mesh> mesh(new Mesh);mesh->texture="pfx\\pfx_ani-fire01";mesh->shader="original_model_shader";
    for(unsigned i=0;i<3;++i){SourceVertex v={};v.p[0]=i==2?.1f:-.1f;v.p[1]=i==1?.1f:-.1f;v.p[2]=2;
        v.uv[0]=i==2?1.f:0.f;v.uv[1]=i==1?1.f:0.f;v.normal=0x7fff7fff;mesh->vertices.push_back(v);mesh->indices.push_back(static_cast<uint16_t>(i));}
    NativeModelCommand model;model.mesh=mesh;model.vertex_count=3;model.count=3;model.world=model.vp=kich_r4_scene::Identity();
    model.world.m[14]=10; // Between generated streams at world Z=2 and Z=20.
    model.effect=5;model.shader="particle_distort";model.texture="pfx\\pfx_distortion";model.shader_element=4;model.shader_pass=0;
    model.style.clamp=0;batch.native_models.push_back(model);
    // This real opaque model exercises shadow and GBuffer calls in the same frame.
    model.effect=0;model.shader="models\\model";model.texture="base";model.shader_element=0;batch.native_models.push_back(model);
    return batch;
}
void CompleteFrame(){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();submissions.clear();model_pixel.clear();
    f->files["$game_textures$/pfx\\pfx_distortion.dds"]=fixture::Dds();
    f->files["$game_textures$/pfx\\pfx_ani-fire01.dds"]=fixture::Dds();
    kich_r4_game::APIs apis=DeferredAPIs();apis.deferred.draw=RecordDraw;
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(apis,files,"l01_escape",NULL);
    game.deferred.native_mode=true;
    game.deferred.native_models.api={sizeof(KichR4ModelAPI),KICH_R4_MODEL_ABI,sizeof(void*),0,ModelPipeline,ModelPose,ModelUpload,ModelDraw};
    KichR4GameFrame input=DeferredInput();input.engine_frame=7;
    input.lighting.count=1;KichR4LocalLight& point=input.lighting.lights[0];point.type=1;point.range=15;point.position[1]=2;point.color[0]=1;
    const kich_r4_capture::Batch batch=SourceBatch();
    Require(game.Render(input,&batch)==1,"native frame with ordinary fire and distortion completes");
    const KichR4Handle mask=game.deferred.distortion_map,warped=game.deferred.final_color,unwarped=game.deferred.scene_color;
    Require(mask && warped && unwarped && mask!=warped && warped!=unwarped,"mask and ping-pong images are distinct");
    Require(targets.at(mask).format==KICH_R4_RGBA16F,"mask preserves exact neutral 0.5");
    unsigned mask_passes=0;
    for(const KichR4PassDesc& pass:passes)if(pass.color_count==1 && pass.colors[0]==mask){++mask_passes;
        Require(pass.depth==game.deferred.depth && pass.clear_flags==KICH_R4_CLEAR_COLOR,"mask reads geometry depth without clearing it");
        Require(pass.color[0]==.5f && pass.color[1]==.5f && pass.color[2]==0 && pass.color[3]==0,"mask clears displacement to neutral");}
    Require(mask_passes==1,"one dedicated distortion mask pass per frame");
    unsigned stream_masks=0,model_masks=0,composites=0,posts=0,flames=0;size_t composite_at=0,first_mask=submissions.size(),last_flame=0;
    std::vector<uint32_t> stream_starts,distortion_order;
    for(size_t i=0;i<submissions.size();++i){const Submission& record=submissions[i];const KichR4DeferredDrawDesc& draw=record.draw;
        if(record.pixel=="PSParticleDistortionMask"){
            if(first_mask==submissions.size())first_mask=i;
            distortion_order.push_back(record.model?UINT32_MAX:draw.state.draw.start_index);
            if(record.model)++model_masks;else{++stream_masks;stream_starts.push_back(draw.state.draw.start_index);}
            Require(record.pass.color_count==1 && record.pass.colors[0]==mask,"effect 5 writes only distortion mask");
            Require(draw.textures[0]==game.level.Texture(game.frame,"pfx\\pfx_distortion"),"mask binds source distortion texture, never preceding flame");
            for(unsigned j=1;j<8;++j)Require(!draw.textures[j],"mask has no scene/depth shader inputs");
            Require(draw.state.blend_enable==1 && draw.state.source_blend==5 && draw.state.dest_blend==6 && !draw.state.depth_write,"mask alpha blends field against neutral and preserves depth");
            if(!record.model){const unsigned triangle=draw.state.draw.start_index/3;
                Require(draw.state.clamp_texture==batch.commands[triangle].style.clamp,"mask preserves source sampler mode");
                const Object& vb=f->Find(draw.state.draw.vertex_buffer);
                for(unsigned j=0;j<3;++j){kich_r4_capture::Vertex vertex;memcpy(&vertex,vb.bytes.data()+(triangle*3+j)*sizeof(vertex),sizeof(vertex));
                    Require(vertex.uv[0]==batch.vertices[triangle*3+j].uv[0] && vertex.uv[1]==batch.vertices[triangle*3+j].uv[1],"uploaded distortion UVs unchanged");}}
        }else if(record.pixel=="PSDistortionComposite"){
            ++composites;composite_at=i;Require(record.pass.colors[0]==warped && draw.textures[0]==unwarped && draw.textures[1]==mask,"composite samples completed forward scene and mask into separate image");
        }else if(record.pixel=="PSPostprocess"){
            ++posts;Require(i>composite_at && draw.textures[0]==warped,"postprocess consumes swapped distortion output");
        }else if(draw.textures[0] && draw.textures[0]==game.level.Texture(game.frame,"pfx\\pfx_ani-fire01")){
            ++flames;last_flame=i;Require(record.pixel=="PSPlain" && record.pass.colors[0]==unwarped,"ordinary fire remains in regular forward scene");
        }
        if(draw.textures[0]==game.level.Texture(game.frame,"pfx\\pfx_distortion"))Require(record.pixel=="PSParticleDistortionMask","effect 5 excluded from shadows, GBuffer and ordinary forward");
    }
    Require(stream_masks==2 && model_masks==1 && composites==1 && posts==1 && flames==1,"each source route submitted once in correct stage");
    Require(last_flame<first_mask && first_mask<composite_at,"ordinary forward completes before mask and composite");
    Require(stream_starts.size()==2 && stream_starts[0]==6 && stream_starts[1]==3,"stream distortion sorted back to front");
    Require(distortion_order.size()==3 && distortion_order[0]==6 && distortion_order[1]==UINT32_MAX && distortion_order[2]==3,
        "one global distortion order interleaves far stream, native model, near stream");
    Require(game.deferred.native_models.draw_calls[KichR4ModelSession::Distortion]==1,"native distortion model reaches dedicated pass");

    // A following empty source frame cannot replay distortion from prior batches.
    kich_r4_capture::Batch empty;empty.Begin(960,540,8);empty.renderer_seen=empty.ui_seen=true;input.engine_frame=8;
    submissions.clear();Require(game.Render(input,&empty)==1,"empty following frame remains valid");
    for(const Submission& record:submissions)Require(record.pixel!="PSParticleDistortionMask" && record.pixel!="PSDistortionComposite","empty frame cannot replay stale mask");
    Require(game.deferred.final_color==warped,"no distortion means no unnecessary ping-pong swap");

    input.width=1280;input.height=720;submissions.clear();Require(game.Render(input,&empty)==1,"native target resize remains valid");
    Require(game.deferred.distortion_map!=mask && !f->objects.count(mask),"resize retires old distortion target");
    KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->created==f->released && f->objects.empty(),"distortion and model resources clean up with device");
}
}
int main(){try{(void)&Scenario;CompleteFrame();printf("PASS: G3b distortion frame orchestration, selected textures, UV/sampler retention, no target alias, ordering/resize/empty-frame (%d checks)\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
