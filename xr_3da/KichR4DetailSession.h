#ifndef KICH_R4_DETAIL_SESSION_H
#define KICH_R4_DETAIL_SESSION_H
#include "KichR4DetailData.h"
#include "KichR4LevelSession.h"
#include "KichR4LightingMath.h"
#include "RenderSnapshot.h"
struct KichR4DetailSession {
    struct Mesh {KichR4Handle vb,ib,texture;uint32_t indices;Mesh():vb(0),ib(0),texture(0),indices(0){}};
    KichR4DetailAPI api;
    const kich_r4_detail::Frame* source;
    std::vector<Mesh> meshes;
    KichR4Handle instance_buffer,vs,pipelines[2];
    uint64_t generation,vertex_bytes,index_bytes;
    uint32_t capacity,uploaded_frame,draw_calls,shadow_draw_calls;
    uint32_t telemetry_frame,uploaded_instances;
    uint64_t submitted_instances,shadow_submitted_instances;
    KichR4DetailSession():source(NULL),instance_buffer(0),vs(0),generation(UINT64_MAX),vertex_bytes(0),index_bytes(0),capacity(0),uploaded_frame(UINT32_MAX),draw_calls(0),shadow_draw_calls(0),telemetry_frame(UINT32_MAX),uploaded_instances(0),submitted_instances(0),shadow_submitted_instances(0){memset(&api,0,sizeof(api));pipelines[0]=pipelines[1]=0;}
    static std::string MaterialKey(const std::string& identity){
        uint64_t hash=UINT64_C(14695981039346656037);for(size_t i=0;i<identity.size();++i){hash^=static_cast<unsigned char>(identity[i]);hash*=UINT64_C(1099511628211);}
        char key[64];snprintf(key,sizeof(key),"material/fnv1a64/%016llx",static_cast<unsigned long long>(hash));return key;
    }
    void ResetFrame(uint32_t frame){telemetry_frame=frame;uploaded_instances=draw_calls=shadow_draw_calls=0;submitted_instances=shadow_submitted_instances=0;}
    void SnapshotSummary(uint32_t frame,bool scene)const{
        if(!g_render_snapshot_active)return;
        try{
        const bool current=scene && source && source->engine_frame==frame;
        const bool attempted=scene && telemetry_frame==frame;
        const char* status=!scene?"SCENE_NOT_SUBMITTED":!source?"UNAVAILABLE":!current?"UNPREPARED_FOR_FRAME":source->instances.empty()?"CURRENT_EMPTY":attempted && uploaded_frame==frame?"CURRENT_UPLOADED":"CURRENT_NOT_UPLOADED";
        RenderSnapshot_Text("DETAIL_RENDERER","status",status);RenderSnapshot_Text("DETAIL_RENDERER","path","NATIVE_DX11");
        RenderSnapshot_Text("DETAIL_RENDERER","instance_scope","CAMERA_SELECTED_LIST; NOT_VISIBLE_PIXELS");
        RenderSnapshot_Text("DETAIL_RENDERER","shadow_selection","REUSES_CAMERA_SELECTED_INSTANCES; NO_PER_SHADOW_INSTANCE_CULL");
        RenderSnapshot_Value("DETAIL_RENDERER","visible_instances",current?source->instances.size():0);
        RenderSnapshot_Value("DETAIL_RENDERER","still_instances",current?source->variants[0]:0);RenderSnapshot_Value("DETAIL_RENDERER","wind1_instances",current?source->variants[1]:0);RenderSnapshot_Value("DETAIL_RENDERER","wind2_instances",current?source->variants[2]:0);
        RenderSnapshot_Value("DETAIL_RENDERER","loaded_meshes",source?source->meshes.size():0);RenderSnapshot_Value("DETAIL_RENDERER","unique_meshes",source?source->meshes.size():0);
        size_t active=0;if(current){std::vector<bool> seen(source->meshes.size(),false);for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];if(g.count && g.mesh<seen.size() && !seen[g.mesh]){seen[g.mesh]=true;++active;}}}
        RenderSnapshot_Value("DETAIL_RENDERER","active_meshes",active);RenderSnapshot_Value("DETAIL_RENDERER","groups",current?source->groups.size():0);
        const bool mesh_memory_known=!source || generation==source->generation || (!vertex_bytes && !index_bytes);
        RenderSnapshot_Text("DETAIL_RENDERER","mesh_memory_status",mesh_memory_known?"TRACKED_ALLOCATION":"GENERATION_UPLOAD_INCOMPLETE");
        if(mesh_memory_known){RenderSnapshot_Value("DETAIL_RENDERER","gpu_vertex_bytes",static_cast<double>(vertex_bytes));RenderSnapshot_Value("DETAIL_RENDERER","gpu_index_bytes",static_cast<double>(index_bytes));}
        else{RenderSnapshot_Text("DETAIL_RENDERER","gpu_vertex_bytes","NOT_AVAILABLE");RenderSnapshot_Text("DETAIL_RENDERER","gpu_index_bytes","NOT_AVAILABLE");}
        RenderSnapshot_Value("DETAIL_RENDERER","instance_capacity",capacity);RenderSnapshot_Value("DETAIL_RENDERER","instance_allocated_bytes",uint64_t(capacity)*sizeof(KichR4DetailInstance));
        RenderSnapshot_Value("DETAIL_RENDERER","instance_upload_bytes",attempted?uint64_t(uploaded_instances)*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_Value("DETAIL_RENDERER","instanced_draw_calls",attempted?draw_calls:0);RenderSnapshot_Value("DETAIL_RENDERER","shadow_draw_calls",attempted?shadow_draw_calls:0);
        RenderSnapshot_Value("DETAIL_RENDERER","geometry_submitted_instances",attempted?static_cast<double>(submitted_instances):0);RenderSnapshot_Value("DETAIL_RENDERER","shadow_submitted_instances",attempted?static_cast<double>(shadow_submitted_instances):0);
        RenderSnapshot_Value("DETAIL_RENDERER","legacy_capture_vertices",source && source->engine_frame==frame?static_cast<double>(source->legacy_vertices):0);RenderSnapshot_Value("DETAIL_RENDERER","legacy_capture_indices",source && source->engine_frame==frame?static_cast<double>(source->legacy_indices):0);RenderSnapshot_Value("DETAIL_RENDERER","legacy_detail_draws",source && source->engine_frame==frame?source->legacy_draws:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_prepare_ms",current?source->prepare_ms:0);RenderSnapshot_Text("DETAIL_RENDERER","gpu_detail_ms","NOT_INSTRUMENTED");
        RenderSnapshot_Value("MEMORY","detail_cpu_instance_payload_bytes",source?source->instances.size()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_Value("MEMORY","detail_cpu_instance_capacity_bytes",source?source->instances.capacity()*sizeof(KichR4DetailInstance):0);
        uint64_t mesh_payload=0;if(source)for(size_t i=0;i<source->meshes.size();++i){const kich_r4_detail::Mesh& mesh=source->meshes[i];mesh_payload+=mesh.vertices.size()*sizeof(KichR4DetailVertex)+mesh.indices.size()*sizeof(uint16_t);}
        RenderSnapshot_Value("MEMORY","detail_cpu_mesh_payload_bytes",static_cast<double>(mesh_payload));
        RenderSnapshot_Value("MEMORY","detail_cpu_group_payload_bytes",source?source->groups.size()*sizeof(kich_r4_detail::Group):0);
        RenderSnapshot_Text("MEMORY","detail_cpu_instance_scope","RETAINED_FRONTEND_STORAGE; MAY_BE_FROM_PRIOR_FRAME");
        }catch(...){RenderSnapshot_Text("TELEMETRY_LOSS","r4_detail_summary","ALLOCATION_FAILED; PARTIAL_SUMMARY");}
    }
    int32_t Upload(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t frame){
        ResetFrame(frame);
        if(!source)return 0;
        if(generation!=source->generation){
            for(size_t i=0;i<meshes.size();++i){
                if(meshes[i].vb && l.Expect(s,"G3a release old mesh VB",l.bridge.resources.release_resource(s.device,meshes[i].vb))<0)return s.failure;
                if(meshes[i].ib && l.Expect(s,"G3a release old mesh IB",l.bridge.resources.release_resource(s.device,meshes[i].ib))<0)return s.failure;}
            meshes.clear();vertex_bytes=index_bytes=0;uploaded_frame=UINT32_MAX;
            for(size_t i=0;i<source->meshes.size();++i){const kich_r4_detail::Mesh& m=source->meshes[i];Mesh gpu;
                if(m.vertices.empty() || m.indices.empty() || m.vertices.size()>65536 || m.indices.size()>UINT32_MAX/2)return s.Record("G3a invalid detail mesh",KICH_R4_E_ARGUMENT);
                vertex_bytes+=m.vertices.size()*sizeof(m.vertices[0]);index_bytes+=m.indices.size()*2;
                if(vertex_bytes+index_bytes>64u*1024u*1024u)return s.Record("G3a detail mesh budget",KICH_R4_E_MEMORY);
                const KichR4BufferDesc vb={sizeof(vb),KICH_R4_VERTEX_BUFFER,static_cast<uint32_t>(m.vertices.size()*sizeof(m.vertices[0])),sizeof(m.vertices[0])};
                const KichR4BufferDesc ib={sizeof(ib),KICH_R4_INDEX_BUFFER,static_cast<uint32_t>(m.indices.size()*2),2};
                if(l.Expect(s,"G3a immutable mesh vertices",l.bridge.resources.create_buffer(s.device,&vb,m.vertices.data(),vb.byte_size,&gpu.vb))<0)return s.failure;
                if(l.Expect(s,"G3a immutable mesh indices",l.bridge.resources.create_buffer(s.device,&ib,m.indices.data(),ib.byte_size,&gpu.ib))<0)return s.failure;
                gpu.texture=l.Texture(s,m.texture);gpu.indices=static_cast<uint32_t>(m.indices.size());meshes.push_back(gpu);
            }
            generation=source->generation;
        }
        if(source->engine_frame!=frame || source->instances.empty())return 0;
        if(source->instances.size()>KICH_R4_MAX_DETAIL_INSTANCES)return s.Record("G3a instance budget",KICH_R4_E_MEMORY);
        const uint32_t needed=static_cast<uint32_t>(source->instances.size());
        if(needed>capacity){uint32_t next=256;while(next<needed)next*=2;KichR4Handle handle=0;
            if(l.Expect(s,"G3a dynamic instance stream",api.create_instances(s.device,next,&handle))<0)return s.failure;
            if(instance_buffer && l.Expect(s,"G3a replace instance stream",l.bridge.resources.release_resource(s.device,instance_buffer))<0)return s.failure;
            instance_buffer=handle;capacity=next;
        }
        if(l.Expect(s,"G3a instance WRITE_DISCARD",api.upload_instances(s.device,instance_buffer,source->instances.data(),needed))<0)return s.failure;
        uploaded_frame=frame;uploaded_instances=needed;return 0;
    }
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& l,const char* hlsl,KichR4Handle geometry_ps,KichR4Handle shadow_ps){
        if(!api.create_pipeline || !api.create_instances || !api.upload_instances || !api.draw)return s.Record("G3a detail API",KICH_R4_E_STATE);
        if(vs && pipelines[0] && pipelines[1])return 0;
        KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=KICH_R4_VERTEX_SHADER;memcpy(d.entry,"VSDetail",9);memcpy(d.name,"kich_g3a_details",16);
        if(l.Expect(s,"G3a VSDetail",l.bridge.resources.compile_shader(s.device,&d,hlsl,static_cast<uint32_t>(strlen(hlsl)),&vs,l.bridge.shader_error,sizeof(l.bridge.shader_error)))<0)return s.failure;
        for(unsigned i=0;i<2;++i){const KichR4DetailPipelineDesc p={sizeof(p),vs,i?shadow_ps:geometry_ps};
            if(l.Expect(s,"G3a instance input layout",api.create_pipeline(s.device,&p,&pipelines[i]))<0)return s.failure;}
        return 0;
    }
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle cb,const kich_r4_lighting::Constants& base,bool shadow,uint32_t frame,const char* phase="unspecified",uint32_t pass_mask=0){
        struct Summary {char section[64];uint32_t calls;uint64_t variants[3];bool enabled;
            Summary(const char* name):calls(0),enabled(g_render_snapshot_active){section[0]=0;if(enabled)snprintf(section,sizeof(section),"DETAIL_PASS %s",name);variants[0]=variants[1]=variants[2]=0;}
            ~Summary(){if(enabled){RenderSnapshot_Value(section,"draw_calls",calls);RenderSnapshot_Value(section,"submitted_instances",static_cast<double>(variants[0]+variants[1]+variants[2]));
                RenderSnapshot_Value(section,"still_submitted_instances",static_cast<double>(variants[0]));RenderSnapshot_Value(section,"wave1_submitted_instances",static_cast<double>(variants[1]));RenderSnapshot_Value(section,"wave2_submitted_instances",static_cast<double>(variants[2]));}}
        } summary(phase);
        if(!source || uploaded_frame!=frame || source->engine_frame!=frame || source->instances.empty())return 0;
        for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];
            if(g.mesh>=meshes.size() || g.variant>2 || g.first>source->instances.size() || g.count>source->instances.size()-g.first)return s.Record("G3a detail group range",KICH_R4_E_ARGUMENT);
            const Mesh& mesh=meshes[g.mesh];kich_r4_lighting::Constants c=base;
            if(g_render_snapshot_active){try{const kich_r4_detail::Mesh& named=source->meshes[g.mesh];
                const std::string material=MaterialKey(named.shader+"\n"+named.texture+"\ncutout=200/255;two_sided;depth_write");
                RenderSnapshot_Context("DETAIL",named.name.c_str(),named.shader.c_str());RenderSnapshot_Object("DETAIL",named.name.c_str(),material.c_str(),"LEVEL_DETAILS_INSTANCE_GROUP",0);
                RenderSnapshot_Queue("R4_DETAIL_INSTANCED");RenderSnapshot_PassMask(pass_mask);RenderSnapshot_DetailVariant(g.variant);
                float uv[8]={};uint32_t samples=0;for(size_t j=0;j<named.indices.size() && samples<4;++j){const uint16_t index=named.indices[j];if(index<named.vertices.size()){uv[samples*2]=named.vertices[index].misc[0];uv[samples*2+1]=named.vertices[index].misc[1];++samples;}}
                RenderSnapshot_UV(uv,samples,"DETAIL_INDEXED_MESH_QUANTIZED_BASE_UV");RenderSnapshot_Texture(0,named.texture.c_str());RenderSnapshot_Skin("NONE_INSTANCED_DETAIL");
                }catch(...){RenderSnapshot_Context("DETAIL","NOT_AVAILABLE","NOT_AVAILABLE");RenderSnapshot_DetailVariant(g.variant);RenderSnapshot_Text("TELEMETRY_LOSS","r4_detail_provenance","ALLOCATION_FAILED");}}
            /* VSDetail owns world rows: wave, wind, variant. Other shaders keep
             * their ordinary matrix meaning; the 816-byte frame ABI is intact. */
            memcpy(c.world.m,source->wave[g.variant],16);memcpy(c.world.m+4,source->wind[g.variant],16);
            c.world.m[8]=static_cast<float>(g.variant);c.alpha[0]=200.f/255;c.alpha[1]=7;c.alpha[2]=c.alpha[3]=0;
            if(l.Expect(s,"G3a detail constants",l.bridge.api.update_buffer(s.device,cb,&c,sizeof(c)))<0)return s.failure;
            KichR4DetailDrawDesc d={};d.struct_size=sizeof(d);d.surface.struct_size=sizeof(d.surface);
            KichR4StreamDrawDesc& state=d.surface.state;state.struct_size=sizeof(state);
            state.draw={sizeof(state.draw),pipelines[shadow?1:0],mesh.vb,mesh.ib,cb,mesh.indices,0,KICH_R4_CULL_NONE,KICH_R4_DEPTH_ON};
            state.source_blend=2;state.dest_blend=1;state.depth_write=1;state.max_depth=1;
            d.surface.textures[0]=mesh.texture;d.instance_buffer=instance_buffer;d.first_instance=g.first;d.instance_count=g.count;
            if(l.Expect(s,shadow?"G3a detail shadow DrawIndexedInstanced":"G3a detail G-buffer DrawIndexedInstanced",api.draw(s.device,&d))<0)return s.failure;
            ++summary.calls;summary.variants[g.variant]+=g.count;
            if(shadow){++shadow_draw_calls;shadow_submitted_instances+=g.count;}else{++draw_calls;submitted_instances+=g.count;}
        }return 0;
    }
};
#endif
