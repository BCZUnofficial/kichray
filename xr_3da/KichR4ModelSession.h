#ifndef KICH_R4_MODEL_SESSION_H
#define KICH_R4_MODEL_SESSION_H
#include "KichR4Model.h"
#include "KichR4CaptureData.h"
#include "KichR4LevelSession.h"
#include "KichR4Visibility.h"
#include <stddef.h>
/* EXE-owned immutable-source cache. Device destruction remains the final owner
 * of every successfully created handle, including partial construction. Weak
 * source ownership lets visual deletion retire GPU storage without retaining
 * the decoded mesh; an expired pointer key is removed before it can be reused. */
struct KichR4ModelSession {
    typedef kich_r4_capture::NativeModelCommand Command;
    typedef kich_r4_capture::Mesh Source;
    enum Pass {Geometry,Shadow,Forward,ForwardAdd,HUD,Distortion,HUDAdd,PassCount};
    struct Mesh {std::weak_ptr<const Source> source;KichR4Handle vb,ib;uint64_t vertex_bytes,index_bytes;
        Mesh():vb(0),ib(0),vertex_bytes(0),index_bytes(0){} };
    KichR4ModelAPI api;
    KichR4Handle vs,pipelines[PassCount],rigid_pose,white,plain_pipeline;
    std::map<const Source*,Mesh> meshes;
    std::vector<KichR4Handle> poses,command_poses;
    const kich_r4_capture::Batch* prepared;bool rigid_ready;
    uint32_t frame,commands,pose_uploads,mesh_uploads,draw_calls[PassCount];
    uint64_t vertex_bytes,index_bytes,pose_upload_bytes,pose_logical_bytes,candidates[PassCount],culled[PassCount],selected[PassCount];
    bool phase_active;char phase_section[96];uint64_t phase_candidates,phase_culled,phase_selected;uint32_t phase_draws;
    KichR4ModelSession():vs(0),rigid_pose(0),white(0),plain_pipeline(0),prepared(NULL),rigid_ready(false),frame(UINT32_MAX),commands(0),pose_uploads(0),mesh_uploads(0),vertex_bytes(0),index_bytes(0),pose_upload_bytes(0),pose_logical_bytes(0){
        memset(&api,0,sizeof(api));memset(pipelines,0,sizeof(pipelines));ResetFrame();}
    void ResetFrame(){phase_active=false;phase_section[0]=0;phase_candidates=phase_culled=phase_selected=phase_draws=0;prepared=NULL;frame=UINT32_MAX;commands=pose_uploads=mesh_uploads=0;pose_upload_bytes=pose_logical_bytes=0;
        memset(draw_calls,0,sizeof(draw_calls));memset(candidates,0,sizeof(candidates));memset(culled,0,sizeof(culled));memset(selected,0,sizeof(selected));}
    static bool Finite(float value){uint32_t bits;memcpy(&bits,&value,4);return (bits&0x7f800000u)!=0x7f800000u;}
    static bool ValidMatrix(const kich_r4_scene::Matrix& m){for(unsigned i=0;i<16;++i)if(!Finite(m.m[i]))return false;return true;}
    static bool ValidSource(const Source& m){
        if(m.vertices.empty() || m.vertices.size()>65536 || m.indices.empty() || m.indices.size()%3 || m.indices.size()>3u*1024u*1024u)return false;
        if(m.skinned && (!m.bones || m.bones>256))return false;
        for(size_t i=0;i<m.vertices.size();++i){const kich_r4_capture::SourceVertex& v=m.vertices[i];
            for(unsigned a=0;a<3;++a)if(!Finite(v.p[a]))return false;
            if(!Finite(v.uv[0]) || !Finite(v.uv[1]))return false;
            if(m.skinned && (v.bone[0]>=m.bones || v.bone[1]>=m.bones || (v.bone[0]!=v.bone[1] && !Finite(v.weight))))return false;}
        for(size_t i=0;i<m.indices.size();++i)if(m.indices[i]>=m.vertices.size())return false;
        return true;
    }
    static bool ValidCommand(const Command& c){
        if(!c.mesh || c.layer>1 || !c.count || c.count%3 || c.first>c.mesh->indices.size() || c.count>c.mesh->indices.size()-c.first ||
            !c.vertex_count || c.vertex_count>c.mesh->vertices.size() || !ValidMatrix(c.world) || !ValidMatrix(c.vp))return false;
        if(c.mesh->skinned){if(c.bones.size()!=c.mesh->bones || (c.skin_mode!=KICH_R4_SKIN_SOFTWARE && c.skin_mode!=KICH_R4_SKIN_HARDWARE))return false;
            if(c.skin_mode==KICH_R4_SKIN_HARDWARE && !c.mesh->hardware_weights_safe)return false;
            for(size_t i=0;i<c.bones.size();++i)if(!ValidMatrix(c.bones[i]))return false;}
        /* Progressive meshes use an indexed prefix; reject a range referencing
         * beyond its advertised vertex prefix, without uploading a second mesh. */
        for(uint32_t i=0;i<c.count;++i)if(c.mesh->indices[c.first+i]>=c.vertex_count)return false;
        return true;
    }
    int32_t ReleaseMesh(KichR4FrameSession& s,KichR4LevelSession& l,Mesh& m){
        if(m.vb){if(l.Expect(s,"G3b retire model vertices",l.bridge.resources.release_resource(s.device,m.vb))<0)return s.failure;m.vb=0;vertex_bytes-=m.vertex_bytes;m.vertex_bytes=0;}
        if(m.ib){if(l.Expect(s,"G3b retire model indices",l.bridge.resources.release_resource(s.device,m.ib))<0)return s.failure;m.ib=0;index_bytes-=m.index_bytes;m.index_bytes=0;}return 0;
    }
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& l,const char* hlsl,KichR4Handle geometry_ps,KichR4Handle shadow_ps,
                  KichR4Handle forward_ps,KichR4Handle local_ps,KichR4Handle plain_ps,KichR4Handle distortion_ps,KichR4Handle white_texture){
        static_assert(sizeof(kich_r4_capture::SourceVertex)==sizeof(KichR4ModelVertex),"source model GPU stride");
        static_assert(offsetof(kich_r4_capture::SourceVertex,bone)==24 && offsetof(kich_r4_capture::SourceVertex,normal)==28,"source model GPU offsets");
        if(!api.create_pipeline || !api.create_pose || !api.upload_pose || !api.draw)return s.Record("G3b model API",KICH_R4_E_STATE);
        white=white_texture;if(vs && pipelines[PassCount-1] && plain_pipeline && rigid_ready)return 0;
        KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=KICH_R4_VERTEX_SHADER;
        memcpy(d.entry,"VSModelGPU",11);memcpy(d.name,"kich_g3b_models",15);
        if(!vs && l.Expect(s,"G3b VSModelGPU",l.bridge.resources.compile_shader(s.device,&d,hlsl,static_cast<uint32_t>(strlen(hlsl)),&vs,l.bridge.shader_error,sizeof(l.bridge.shader_error)))<0)return s.failure;
        const KichR4Handle ps[PassCount]={geometry_ps,shadow_ps,forward_ps,local_ps,forward_ps,distortion_ps,local_ps};
        for(unsigned i=0;i<PassCount;++i)if(!pipelines[i]){const KichR4ModelPipelineDesc p={sizeof(p),vs,ps[i]};
            if(l.Expect(s,"G3b model input layout",api.create_pipeline(s.device,&p,&pipelines[i]))<0)return s.failure;}
        if(!plain_pipeline){const KichR4ModelPipelineDesc p={sizeof(p),vs,plain_ps};
            if(l.Expect(s,"G3b unlit model layout",api.create_pipeline(s.device,&p,&plain_pipeline))<0)return s.failure;}
        if(!rigid_pose && l.Expect(s,"G3b rigid model constants",api.create_pose(s.device,&rigid_pose))<0)return s.failure;
        if(!rigid_ready){const KichR4ModelPose p={};if(l.Expect(s,"G3b rigid model pose",api.upload_pose(s.device,rigid_pose,&p))<0)return s.failure;rigid_ready=true;}
        return 0;
    }
    int32_t Prepare(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_capture::Batch& batch){
        ResetFrame();frame=batch.engine_frame;
        if(batch.native_models.size()>16384)return s.Record("G3b model command budget",KICH_R4_E_MEMORY);
        size_t skeletal=0;for(size_t i=0;i<batch.native_models.size();++i){const Command& c=batch.native_models[i];if(c.mesh && c.mesh->skinned)++skeletal;}
        if(skeletal>=KICH_R4_MAX_MODEL_POSE_SLOTS)return s.Record("G3b GPU model pose budget (16 MiB)",KICH_R4_E_MEMORY);
        for(std::map<const Source*,Mesh>::iterator i=meshes.begin();i!=meshes.end();){if(i->second.source.expired()){
                if(ReleaseMesh(s,l,i->second)<0)return s.failure;
                i=meshes.erase(i);}else ++i;}
        command_poses.clear();command_poses.reserve(batch.native_models.size());size_t pose_index=0;
        for(size_t i=0;i<batch.native_models.size();++i){const Command& c=batch.native_models[i];
            if(!ValidCommand(c))return s.Record("G3b model range or pose",KICH_R4_E_ARGUMENT);
            Mesh& gpu=meshes[c.mesh.get()];
            if(!gpu.vb || !gpu.ib){if(!ValidSource(*c.mesh))return s.Record("G3b invalid immutable model",KICH_R4_E_ARGUMENT);
                const uint64_t vb_bytes=c.mesh->vertices.size()*sizeof(kich_r4_capture::SourceVertex),ib_bytes=c.mesh->indices.size()*sizeof(uint16_t);
                if(vertex_bytes+index_bytes+vb_bytes+ib_bytes>256u*1024u*1024u)return s.Record("G3b GPU model mesh budget",KICH_R4_E_MEMORY);
                gpu.source=c.mesh;
                const KichR4BufferDesc vb={sizeof(vb),KICH_R4_VERTEX_BUFFER,static_cast<uint32_t>(vb_bytes),sizeof(KichR4ModelVertex)};
                const KichR4BufferDesc ib={sizeof(ib),KICH_R4_INDEX_BUFFER,static_cast<uint32_t>(ib_bytes),sizeof(uint16_t)};
                if(!gpu.vb){if(l.Expect(s,"G3b immutable model vertices",l.bridge.resources.create_buffer(s.device,&vb,c.mesh->vertices.data(),vb.byte_size,&gpu.vb))<0)return s.failure;gpu.vertex_bytes=vb_bytes;vertex_bytes+=vb_bytes;}
                if(!gpu.ib){if(l.Expect(s,"G3b immutable model indices",l.bridge.resources.create_buffer(s.device,&ib,c.mesh->indices.data(),ib.byte_size,&gpu.ib))<0)return s.failure;gpu.index_bytes=ib_bytes;index_bytes+=ib_bytes;}
                ++mesh_uploads;}
            KichR4Handle pose=rigid_pose;
            if(c.mesh->skinned){if(pose_index==poses.size())poses.push_back(0);
                if(!poses[pose_index] && l.Expect(s,"G3b skeletal pose buffer",api.create_pose(s.device,&poses[pose_index]))<0)return s.failure;
                pose=poses[pose_index++];KichR4ModelPose p={};p.bone_count=static_cast<uint32_t>(c.bones.size());p.skin_mode=c.skin_mode;p.skinned=1;
                for(size_t j=0;j<c.bones.size();++j)memcpy(p.bones[j],c.bones[j].m,64);
                if(l.Expect(s,"G3b skeletal pose WRITE_DISCARD",api.upload_pose(s.device,pose,&p))<0)return s.failure;
                ++pose_uploads;pose_upload_bytes+=sizeof(p);pose_logical_bytes+=c.bones.size()*64;}
            command_poses.push_back(pose);
        }
        /* Keep warm-frame palette capacity. Empty frames still retire geometry
         * through weak ownership and can never replay an earlier command. */
        commands=static_cast<uint32_t>(batch.native_models.size());prepared=&batch;return 0;
    }
    static bool Opaque(const Command& c){return !c.layer && !c.style.blend && c.style.depth && c.style.write && !c.effect;}
    static bool Routes(const Command& c,Pass pass){
        if(c.effect==5)return pass==Distortion;
        if(pass==Distortion)return false;
        if(c.layer==1)return pass==HUD || (pass==HUDAdd && (!c.effect || c.effect==6));
        if(pass==HUD || pass==HUDAdd)return false;
        if(pass==Geometry || pass==Shadow)return Opaque(c);
        if(pass==ForwardAdd && c.effect && c.effect!=6)return false;
        return !Opaque(c);
    }
    static bool Visible(const Command& c,const kich_r4_lighting::Constants& frame,Pass pass){
        if(!c.bounds_known || !c.bounds.valid || pass==HUD || pass==HUDAdd)return true;
        if(!kich_r4_visibility::Visible(c.bounds.min,c.bounds.max,frame.mvp))return false;
        if(pass==ForwardAdd && frame.light_position[3]>0){double distance=0;for(unsigned i=0;i<3;++i){
                const double p=frame.light_position[i],delta=p<c.bounds.min[i]?c.bounds.min[i]-p:p>c.bounds.max[i]?p-c.bounds.max[i]:0;distance+=delta*delta;}
            const double range=frame.light_position[3];if(distance>range*range)return false;}
        return true;
    }
    static double Distance(const Command& c,const kich_r4_lighting::Constants& frame){double distance=0;
        for(unsigned i=0;i<3;++i){const double center=c.bounds_known?(double(c.bounds.min[i])+c.bounds.max[i])*.5:c.world.m[12+i];const double d=center-frame.camera[i];distance+=d*d;}return distance;}
    static void Snapshot(const Command& c,uint32_t pass_mask){
        const char* semantic=c.layer?"HUD_MODEL":"DYNAMIC_MODEL";
        RenderSnapshot_Context(semantic,c.resource.empty()?"UNKNOWN":c.resource.c_str(),c.shader.empty()?"UNKNOWN":c.shader.c_str());
        if(c.provenance)RenderSnapshot_SetProvenance(c.provenance.get());
        else{char mesh[160];kich_r4_capture::SnapshotModelID(*c.mesh,mesh,sizeof(mesh),c.first,c.count);RenderSnapshot_Object(semantic,mesh,"NOT_AVAILABLE","IMMUTABLE_OGF_SOURCE",0);}
        RenderSnapshot_Queue(c.layer?"R4_NATIVE_HUD_MODEL":"R4_NATIVE_MODEL");RenderSnapshot_PassMask(pass_mask);
        RenderSnapshot_Texture(0,c.texture.c_str());
        RenderSnapshot_Skin(!c.mesh->skinned?"NONE":!c.mesh->two_weights?"GPU_1W":c.skin_mode==KICH_R4_SKIN_HARDWARE?"GPU_2W_HARDWARE_U8":"GPU_2W_SOFTWARE_RAW");
    }
    static void Note(const Command& c,const char* status,const char* reason){RenderSnapshotProvenance p={};RenderSnapshot_GetProvenance(&p);
        RenderSnapshot_Note(status,p.semantic_type[0]?p.semantic_type:c.layer?"HUD_MODEL":"DYNAMIC_MODEL",p.mesh_id[0]?p.mesh_id:"NOT_AVAILABLE",reason);}
    void BeginPhase(const char* phase){phase_active=true;phase_candidates=phase_culled=phase_selected=phase_draws=0;
        snprintf(phase_section,sizeof(phase_section),"MODEL_PASS %s",phase?phase:"unspecified");}
    void EndPhase(){if(!phase_active)return;
        if(g_render_snapshot_active){RenderSnapshot_Value(phase_section,"candidate_observations",static_cast<double>(phase_candidates));
            RenderSnapshot_Value(phase_section,"culled_observations",static_cast<double>(phase_culled));
            RenderSnapshot_Value(phase_section,"selected_observations",static_cast<double>(phase_selected));RenderSnapshot_Value(phase_section,"draw_calls",phase_draws);}
        phase_active=false;}
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle cb,const kich_r4_lighting::Constants& base,
                 const kich_r4_capture::Batch& batch,Pass pass,uint32_t pass_mask,const char* phase,const KichR4Handle* maps=NULL,KichR4Handle scene_color=0,uint32_t only_index=UINT32_MAX){
        if(pass>=PassCount || prepared!=&batch || frame!=batch.engine_frame || command_poses.size()!=batch.native_models.size())return s.Record("G3b unprepared model frame",KICH_R4_E_STATE);
        if(only_index!=UINT32_MAX && only_index>=batch.native_models.size())return s.Record("G3b model command index",KICH_R4_E_ARGUMENT);
        struct PhaseScope {KichR4ModelSession& session;bool automatic;
            PhaseScope(KichR4ModelSession& owner,const char* name):session(owner),automatic(!owner.phase_active){if(automatic)session.BeginPhase(name);}
            ~PhaseScope(){if(automatic)session.EndPhase();}
        } scope(*this,phase);
        std::vector<size_t> order;
        if(only_index==UINT32_MAX){order.resize(batch.native_models.size());for(size_t i=0;i<order.size();++i)order[i]=i;
            if(pass==Forward || pass==ForwardAdd || pass==Distortion)std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return Distance(batch.native_models[a],base)>Distance(batch.native_models[b],base);});}
        const char* reason=pass==Shadow?"SHADOW_FRUSTUM":(pass==HUD || pass==HUDAdd)?"HUD_EXPLICIT":pass==ForwardAdd?"CAMERA_FRUSTUM_AND_LIGHT_RANGE":"CAMERA_FRUSTUM";
        const size_t count=only_index==UINT32_MAX?order.size():1;
        for(size_t n=0;n<count;++n){const size_t index=only_index==UINT32_MAX?order[n]:only_index;const Command& c=batch.native_models[index];if(!Routes(c,pass))continue;
            ++candidates[pass];++phase_candidates;if(g_render_snapshot_active){Snapshot(c,pass_mask);Note(c,"CANDIDATE_CLASSIFIED",reason);}
            if(!Visible(c,base,pass)){++culled[pass];++phase_culled;if(g_render_snapshot_active)Note(c,"CULLED",reason);continue;}
            ++selected[pass];++phase_selected;if(g_render_snapshot_active)Note(c,"SELECTED",reason);
            const Mesh& mesh=meshes.at(c.mesh.get());kich_r4_lighting::Constants constants=base;
            constants.world=c.world;constants.mvp=kich_r4_scene::Multiply(c.world,(pass==HUD || pass==HUDAdd)?c.vp:base.mvp);constants.normal_matrix=kich_r4_lighting::NormalMatrix(c.world);
            constants.alpha[0]=c.style.alpha_ref;constants.alpha[1]=static_cast<float>(c.style.alpha_func);constants.alpha[2]=c.style.blend?1.f:0.f;
            if(pass==Distortion)constants.alpha[3]=0;
            if(l.Expect(s,"G3b model transform constants",l.bridge.api.update_buffer(s.device,cb,&constants,sizeof(constants)))<0)return s.failure;
            kich_r4_capture::Style style=c.style;
            if(pass==Shadow){style.depth=style.write=1;style.blend=style.clamp=0;style.scissor=false;style.min_z=0;style.max_z=1;}
            if(pass==ForwardAdd || pass==HUDAdd){style.blend=1;style.src=style.dst=2;style.write=0;}
            if(pass==Distortion){style.depth=1;style.write=0;style.blend=1;style.src=5;style.dst=6;}
            KichR4ModelDrawDesc d={};d.struct_size=sizeof(d);d.surface.struct_size=sizeof(d.surface);KichR4StreamDrawDesc& state=d.surface.state;state.struct_size=sizeof(state);
            state.draw={sizeof(state.draw),pipelines[pass],mesh.vb,mesh.ib,cb,c.count,c.first,style.cull,style.depth};
            if((pass==Forward || pass==HUD) && c.effect==1)state.draw.pipeline=plain_pipeline;
            state.blend_enable=style.blend;state.source_blend=style.src;state.dest_blend=style.dst;state.clamp_texture=style.clamp;
            state.depth_write=style.write;state.scissor_enable=style.scissor?1:0;memcpy(state.scissor,style.rect,sizeof(state.scissor));state.min_depth=style.min_z;state.max_depth=style.max_z;
            const std::string& texture=c.texture;d.surface.textures[0]=texture.empty()?white:l.Texture(s,texture);
            if(g_render_snapshot_active){if(texture.empty())RenderSnapshot_Texture(0,"$white");
                else if(d.surface.textures[0]==l.checker){char resolved[256];snprintf(resolved,sizeof(resolved),"CHECKER_FALLBACK:%s",texture.c_str());RenderSnapshot_Texture(0,resolved);}}
            if(maps)for(unsigned j=0;j<6;++j)d.surface.textures[j+2]=maps[j];
            if(pass==Distortion)d.surface.textures[1]=scene_color;
            d.pose_buffer=command_poses[index];if(l.Expect(s,"G3b native model DrawIndexed",api.draw(s.device,&d))<0)return s.failure;
            ++draw_calls[pass];++phase_draws;
        }
        return 0;
    }
    void SnapshotSummary()const{if(!g_render_snapshot_active)return;
        RenderSnapshot_Text("MODEL_RENDERER","path","NATIVE_DX11_GPU_SKINNING");RenderSnapshot_Text("MODEL_RENDERER","selection_scope","PER_PASS_CONSERVATIVE_BOUNDS; NOT_VISIBLE_PIXELS");
        RenderSnapshot_Value("MODEL_RENDERER","commands",commands);RenderSnapshot_Value("MODEL_RENDERER","unique_cached_meshes",meshes.size());
        RenderSnapshot_Value("MODEL_RENDERER","mesh_uploads",mesh_uploads);RenderSnapshot_Value("MODEL_RENDERER","pose_uploads",pose_uploads);
        RenderSnapshot_Value("MODEL_RENDERER","gpu_vertex_bytes",static_cast<double>(vertex_bytes));RenderSnapshot_Value("MODEL_RENDERER","gpu_index_bytes",static_cast<double>(index_bytes));
        RenderSnapshot_Value("MODEL_RENDERER","pose_logical_bytes",static_cast<double>(pose_logical_bytes));RenderSnapshot_Value("MODEL_RENDERER","pose_budget_bytes",KICH_R4_MODEL_POSE_BUDGET_BYTES);
        RenderSnapshot_Value("MODEL_RENDERER","pose_upload_bytes",static_cast<double>(pose_upload_bytes));RenderSnapshot_Value("MODEL_RENDERER","pose_allocated_bytes",static_cast<double>((poses.size()+(rigid_pose?1:0))*sizeof(KichR4ModelPose)));
        RenderSnapshot_Value("MODEL_RENDERER","cpu_expanded_model_vertex_bytes",0);RenderSnapshot_Value("MODEL_RENDERER","cpu_expanded_model_index_bytes",0);
        uint64_t draws=0;for(unsigned i=0;i<PassCount;++i)draws+=draw_calls[i];RenderSnapshot_Value("MODEL_RENDERER","draw_calls",static_cast<double>(draws));
        RenderSnapshot_Text("MODEL_RENDERER","gpu_model_ms","NOT_INSTRUMENTED");
    }
};
#endif
