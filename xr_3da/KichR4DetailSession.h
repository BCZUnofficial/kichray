#ifndef KICH_R4_DETAIL_SESSION_H
#define KICH_R4_DETAIL_SESSION_H
#include "KichR4DetailData.h"
#include "KichR4LevelSession.h"
#include "KichR4LightingMath.h"
#include "KichR4Visibility.h"
#include "RenderSnapshot.h"
struct KichR4DetailSession {
    struct Mesh {KichR4Handle vb,ib,texture;uint32_t indices;Mesh():vb(0),ib(0),texture(0),indices(0){}};
    KichR4DetailAPI api;
    const kich_r4_detail::Frame* source;
    std::vector<Mesh> meshes;
    KichR4Handle instance_buffer,shadow_instance_buffer,vs,pipelines[2];
    uint64_t generation,vertex_bytes,index_bytes;
    uint32_t capacity,shadow_capacity,uploaded_frame,draw_calls,shadow_draw_calls;
    uint32_t telemetry_frame,uploaded_instances;
    uint64_t submitted_instances,shadow_submitted_instances;
    uint64_t shadow_upload_bytes,shadow_candidates,shadow_culled;
    std::vector<KichR4DetailInstance> selected_instances;
    std::vector<kich_r4_detail::Group> selected_groups;
    KichR4DetailSession():source(NULL),instance_buffer(0),shadow_instance_buffer(0),vs(0),generation(UINT64_MAX),vertex_bytes(0),index_bytes(0),capacity(0),shadow_capacity(0),uploaded_frame(UINT32_MAX),draw_calls(0),shadow_draw_calls(0),telemetry_frame(UINT32_MAX),uploaded_instances(0),submitted_instances(0),shadow_submitted_instances(0),shadow_upload_bytes(0),shadow_candidates(0),shadow_culled(0){memset(&api,0,sizeof(api));pipelines[0]=pipelines[1]=0;}
    static std::string MaterialKey(const std::string& identity){
        uint64_t hash=UINT64_C(14695981039346656037);for(size_t i=0;i<identity.size();++i){hash^=static_cast<unsigned char>(identity[i]);hash*=UINT64_C(1099511628211);}
        char key[64];snprintf(key,sizeof(key),"material/fnv1a64/%016llx",static_cast<unsigned long long>(hash));return key;
    }
    void ResetFrame(uint32_t frame){telemetry_frame=frame;uploaded_instances=draw_calls=shadow_draw_calls=0;submitted_instances=shadow_submitted_instances=shadow_upload_bytes=shadow_candidates=shadow_culled=0;}
    void SnapshotSummary(uint32_t frame,bool scene)const{
        if(!g_render_snapshot_active)return;
        try{
        const bool current=scene && source && source->source_frame_known && source->engine_frame==frame;
        const bool attempted=scene && telemetry_frame==frame;
        const char* status=!scene?"SCENE_NOT_SUBMITTED":!source?"UNAVAILABLE":!current?"UNPREPARED_FOR_FRAME":source->instances.empty()?(source->shadow_instances.empty()?"CURRENT_EMPTY":"CURRENT_SHADOW_ONLY"):attempted && uploaded_frame==frame?"CURRENT_UPLOADED":"CURRENT_NOT_UPLOADED";
        RenderSnapshot_Text("DETAIL_RENDERER","status",status);RenderSnapshot_Text("DETAIL_RENDERER","path","NATIVE_DX11");
        RenderSnapshot_Text("DETAIL_RENDERER","instance_scope","CAMERA_SELECTED_LIST; NOT_VISIBLE_PIXELS");
        RenderSnapshot_Text("DETAIL_RENDERER","shadow_selection","PER_SHADOW_FRUSTUM; READY_CACHE_WITHIN_DETAIL_RADIUS; WIND_EXPANDED_BOUNDS");
        const bool source_frame_known=source && source->source_frame_known;
        RenderSnapshot_U64("DETAIL_RENDERER","source_frame_valid",source_frame_known?1:0);
        if(source_frame_known){RenderSnapshot_U64("DETAIL_RENDERER","source_engine_frame",source->engine_frame);
            if(frame>=source->engine_frame)RenderSnapshot_U64("DETAIL_RENDERER","source_age_engine_frames",uint64_t(frame)-source->engine_frame);
            else RenderSnapshot_Text("DETAIL_RENDERER","source_age_engine_frames","FRAME_COUNTER_WRAP_OR_RESET");}
        else{RenderSnapshot_Text("DETAIL_RENDERER","source_engine_frame","NOT_AVAILABLE");RenderSnapshot_Text("DETAIL_RENDERER","source_age_engine_frames","NOT_AVAILABLE");}
        if(source)RenderSnapshot_U64("DETAIL_RENDERER","source_generation",source->generation);
        RenderSnapshot_Text("DETAIL_RENDERER","visibility_pixels","NOT_MEASURED");
        RenderSnapshot_Text("DETAIL_RENDERER","source_bound_method","DECODED_LOCAL_MESH_AABB; INSTANCE_TRANSFORM; WIND_ENVELOPE");
        RenderSnapshot_Text("DETAIL_RENDERER","shadow_portal_hom","NOT_USED; READY_CACHE_AND_FRUSTUM_ONLY");
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_source_instances",current?source->shadow_instances.size():0);
        RenderSnapshot_U64("DETAIL_RENDERER","visible_instances",current?source->instances.size():0);
        RenderSnapshot_U64("DETAIL_RENDERER","still_instances",current?source->variants[0]:0);RenderSnapshot_U64("DETAIL_RENDERER","wind1_instances",current?source->variants[1]:0);RenderSnapshot_U64("DETAIL_RENDERER","wind2_instances",current?source->variants[2]:0);
        RenderSnapshot_U64("DETAIL_RENDERER","loaded_meshes",source?source->meshes.size():0);RenderSnapshot_U64("DETAIL_RENDERER","unique_meshes",source?source->meshes.size():0);
        size_t active=0;if(current && RenderSnapshot_Detailed()){std::vector<bool> seen(source->meshes.size(),false);for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];if(g.count && g.mesh<seen.size() && !seen[g.mesh]){seen[g.mesh]=true;++active;}}}
        if(RenderSnapshot_Detailed())RenderSnapshot_U64("DETAIL_RENDERER","active_meshes",active);else RenderSnapshot_Text("DETAIL_RENDERER","active_meshes","NOT_MEASURED_COUNTER_MODE");RenderSnapshot_U64("DETAIL_RENDERER","groups",current?source->groups.size():0);
        const bool mesh_memory_known=!source || generation==source->generation || (!vertex_bytes && !index_bytes);
        RenderSnapshot_Text("DETAIL_RENDERER","mesh_memory_status",mesh_memory_known?"TRACKED_ALLOCATION":"GENERATION_UPLOAD_INCOMPLETE");
        if(mesh_memory_known){RenderSnapshot_U64("DETAIL_RENDERER","gpu_vertex_bytes",vertex_bytes);RenderSnapshot_U64("DETAIL_RENDERER","gpu_index_bytes",index_bytes);}
        else{RenderSnapshot_Text("DETAIL_RENDERER","gpu_vertex_bytes","NOT_AVAILABLE");RenderSnapshot_Text("DETAIL_RENDERER","gpu_index_bytes","NOT_AVAILABLE");}
        RenderSnapshot_U64("DETAIL_RENDERER","instance_capacity",capacity);RenderSnapshot_U64("DETAIL_RENDERER","instance_allocated_bytes",uint64_t(capacity)*sizeof(KichR4DetailInstance));
        RenderSnapshot_U64("DETAIL_RENDERER","instance_upload_bytes",attempted?uint64_t(uploaded_instances)*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_instance_capacity",shadow_capacity);RenderSnapshot_U64("DETAIL_RENDERER","shadow_instance_allocated_bytes",uint64_t(shadow_capacity)*sizeof(KichR4DetailInstance));
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_instance_upload_bytes",attempted?shadow_upload_bytes:0);
        RenderSnapshot_U64("DETAIL_RENDERER","shadow_candidate_observations",attempted?shadow_candidates:0);RenderSnapshot_U64("DETAIL_RENDERER","shadow_culled_observations",attempted?shadow_culled:0);
        RenderSnapshot_U64("DETAIL_RENDERER","instanced_draw_calls",attempted?draw_calls:0);RenderSnapshot_U64("DETAIL_RENDERER","shadow_draw_calls",attempted?shadow_draw_calls:0);
        RenderSnapshot_U64("DETAIL_RENDERER","geometry_submitted_instances",attempted?submitted_instances:0);RenderSnapshot_U64("DETAIL_RENDERER","shadow_submitted_instances",attempted?shadow_submitted_instances:0);
        RenderSnapshot_U64("DETAIL_RENDERER","legacy_capture_vertices",source && source->engine_frame==frame?source->legacy_vertices:0);RenderSnapshot_U64("DETAIL_RENDERER","legacy_capture_indices",source && source->engine_frame==frame?source->legacy_indices:0);RenderSnapshot_U64("DETAIL_RENDERER","legacy_detail_draws",source && source->engine_frame==frame?source->legacy_draws:0);
        RenderSnapshot_Value("DETAIL_RENDERER","cpu_prepare_ms",current?source->prepare_ms:0);RenderSnapshot_Text("DETAIL_RENDERER","gpu_detail_ms","NOT_INSTRUMENTED");
        RenderSnapshot_U64("MEMORY","detail_cpu_instance_payload_bytes",source?source->instances.size()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_instance_capacity_bytes",source?source->instances.capacity()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_candidate_payload_bytes",source?source->shadow_instances.size()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_candidate_capacity_bytes",source?source->shadow_instances.capacity()*sizeof(KichR4DetailInstance):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_selected_payload_bytes",selected_instances.size()*sizeof(KichR4DetailInstance));
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_selected_capacity_bytes",selected_instances.capacity()*sizeof(KichR4DetailInstance));
        uint64_t mesh_payload=0;if(source)for(size_t i=0;i<source->meshes.size();++i){const kich_r4_detail::Mesh& mesh=source->meshes[i];mesh_payload+=mesh.vertices.size()*sizeof(KichR4DetailVertex)+mesh.indices.size()*sizeof(uint16_t);}
        RenderSnapshot_U64("MEMORY","detail_cpu_mesh_payload_bytes",mesh_payload);
        RenderSnapshot_U64("MEMORY","detail_cpu_group_payload_bytes",source?source->groups.size()*sizeof(kich_r4_detail::Group):0);
        RenderSnapshot_U64("MEMORY","detail_cpu_shadow_group_payload_bytes",source?source->shadow_groups.size()*sizeof(kich_r4_detail::Group):0);
        RenderSnapshot_Text("MEMORY","detail_cpu_instance_scope","RETAINED_FRONTEND_STORAGE; MAY_BE_FROM_PRIOR_FRAME");
        }catch(...){RenderSnapshot_Text("TELEMETRY_LOSS","r4_detail_summary","ALLOCATION_FAILED; PARTIAL_SUMMARY");}
    }
    bool ShadowVisible(const kich_r4_detail::Mesh& mesh,const KichR4DetailInstance& instance,uint32_t variant,const kich_r4_scene::Matrix& vp)const{
        if(!mesh.bounds_known || !source || variant>2)return true;
        float low[3]={},high[3]={};
        for(unsigned corner=0;corner<8;++corner){const float p[3]={corner&1?mesh.maximum[0]:mesh.minimum[0],corner&2?mesh.maximum[1]:mesh.minimum[1],corner&4?mesh.maximum[2]:mesh.minimum[2]};
            for(unsigned a=0;a<3;++a){const float value=instance.row[a][0]*p[0]+instance.row[a][1]*p[1]+instance.row[a][2]*p[2]+instance.row[a][3];
                if(!kich_r4_detail::Finite(value))return true;
                if(!corner || value<low[a])low[a]=value;
                if(!corner || value>high[a])high[a]=value;}}
        /* VSDetail cyclic wave lies in [-1,1]. Height is transformed Y minus
         * the instance translation; misc.z is the source height factor. */
        if(variant){const float height=std::max(std::fabs(low[1]-instance.row[1][3]),std::fabs(high[1]-instance.row[1][3]));
            for(unsigned a=0;a<3;a+=2){const float pad=std::fabs(source->wind[variant][a])*height*mesh.height_factor;low[a]-=pad;high[a]+=pad;}}
        return kich_r4_visibility::Visible(low,high,vp);
    }
    int32_t UploadShadow(KichR4FrameSession& s,KichR4LevelSession& l){
        if(selected_instances.empty())return 0;
        if(selected_instances.size()>KICH_R4_MAX_DETAIL_INSTANCES)return s.Record("G3b detail shadow instance budget",KICH_R4_E_MEMORY);
        const uint32_t needed=static_cast<uint32_t>(selected_instances.size());
        if(needed>shadow_capacity){uint32_t next=256;while(next<needed)next*=2;KichR4Handle handle=0;
            if(l.Expect(s,"G3b shadow instance stream",api.create_instances(s.device,next,&handle))<0)return s.failure;
            if(shadow_instance_buffer && l.Expect(s,"G3b replace shadow instance stream",l.bridge.resources.release_resource(s.device,shadow_instance_buffer))<0)return s.failure;
            shadow_instance_buffer=handle;shadow_capacity=next;}
        if(l.Expect(s,"G3b shadow instance WRITE_DISCARD",api.upload_instances(s.device,shadow_instance_buffer,selected_instances.data(),needed))<0)return s.failure;
        shadow_upload_bytes+=uint64_t(needed)*sizeof(KichR4DetailInstance);return 0;
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
        struct Summary {char section[64];uint32_t calls,allocated_capacity;uint64_t variants[3],candidates,culled,selected,uploaded_bytes;bool enabled,shadow;
            Summary(const char* name,bool is_shadow):calls(0),allocated_capacity(0),candidates(0),culled(0),selected(0),uploaded_bytes(0),enabled(g_render_snapshot_active),shadow(is_shadow){section[0]=0;if(enabled)snprintf(section,sizeof(section),"DETAIL_PASS %s",name);variants[0]=variants[1]=variants[2]=0;}
            ~Summary(){if(enabled){RenderSnapshot_U64(section,"draw_calls",calls);RenderSnapshot_U64(section,"submitted_instances",variants[0]+variants[1]+variants[2]);
                RenderSnapshot_Text(section,"candidate_scope",shadow?"READY_CACHE_WITHIN_DETAIL_RADIUS":"CAMERA_SELECTED_LIST");RenderSnapshot_U64(section,"instance_capacity",allocated_capacity);RenderSnapshot_U64(section,"instance_allocated_bytes",uint64_t(allocated_capacity)*sizeof(KichR4DetailInstance));
                RenderSnapshot_U64(section,"candidate_instances",candidates);RenderSnapshot_U64(section,"culled_instances",culled);RenderSnapshot_U64(section,"selected_instances",selected);RenderSnapshot_U64(section,"instance_upload_bytes",uploaded_bytes);
                RenderSnapshot_U64(section,"still_submitted_instances",variants[0]);RenderSnapshot_U64(section,"wave1_submitted_instances",variants[1]);RenderSnapshot_U64(section,"wave2_submitted_instances",variants[2]);}}
        } summary(phase,shadow);
        summary.allocated_capacity=shadow?shadow_capacity:capacity;
        if(!source || source->engine_frame!=frame || generation!=source->generation || telemetry_frame!=frame)return 0;
        const std::vector<kich_r4_detail::Group>* groups=&source->groups;
        if(shadow){selected_instances.clear();selected_groups.clear();
            for(size_t i=0;i<source->shadow_groups.size();++i){const kich_r4_detail::Group& g=source->shadow_groups[i];
                if(g.mesh>=meshes.size() || g.variant>2 || g.first>source->shadow_instances.size() || g.count>source->shadow_instances.size()-g.first)return s.Record("G3b detail shadow group range",KICH_R4_E_ARGUMENT);
                kich_r4_detail::Group selected={g.mesh,g.variant,static_cast<uint32_t>(selected_instances.size()),0};
                for(uint32_t j=0;j<g.count;++j){const KichR4DetailInstance& instance=source->shadow_instances[g.first+j];++summary.candidates;++shadow_candidates;
                    if(!ShadowVisible(source->meshes[g.mesh],instance,g.variant,base.mvp)){++summary.culled;++shadow_culled;continue;}
                    if(selected_instances.size()>=KICH_R4_MAX_DETAIL_INSTANCES)return s.Record("G3b selected detail budget",KICH_R4_E_MEMORY);
                    selected_instances.push_back(instance);++selected.count;++summary.selected;}
                if(selected.count)selected_groups.push_back(selected);}
            if(UploadShadow(s,l)<0)return s.failure;
            summary.allocated_capacity=shadow_capacity;summary.uploaded_bytes=uint64_t(selected_instances.size())*sizeof(KichR4DetailInstance);groups=&selected_groups;
        }else{if(uploaded_frame!=frame || source->instances.empty())return 0;summary.candidates=summary.selected=source->instances.size();summary.uploaded_bytes=uint64_t(uploaded_instances)*sizeof(KichR4DetailInstance);}
        for(size_t i=0;i<groups->size();++i){const kich_r4_detail::Group& g=(*groups)[i];
            const size_t count=shadow?selected_instances.size():source->instances.size();
            if(g.mesh>=meshes.size() || g.variant>2 || g.first>count || g.count>count-g.first)return s.Record("G3a detail group range",KICH_R4_E_ARGUMENT);
            const Mesh& mesh=meshes[g.mesh];kich_r4_lighting::Constants c=base;
            if(RenderSnapshot_Detailed()){try{const kich_r4_detail::Mesh& named=source->meshes[g.mesh];
                const std::string material=MaterialKey(named.shader+"\n"+named.texture+"\ncutout=200/255;two_sided;depth_write");
                RenderSnapshot_Context("DETAIL",named.name.c_str(),named.shader.c_str());RenderSnapshot_Object("DETAIL",named.name.c_str(),material.c_str(),"LEVEL_DETAILS_INSTANCE_GROUP",0);
                RenderSnapshot_Queue("R4_DETAIL_INSTANCED");RenderSnapshot_PassMask(pass_mask);RenderSnapshot_DetailVariant(g.variant);
                float uv[8]={};uint32_t samples=0;for(size_t j=0;j<named.indices.size() && samples<4;++j){const uint16_t index=named.indices[j];if(index<named.vertices.size()){uv[samples*2]=named.vertices[index].misc[0];uv[samples*2+1]=named.vertices[index].misc[1];++samples;}}
                RenderSnapshot_UV(uv,samples,"DETAIL_INDEXED_MESH_QUANTIZED_BASE_UV");RenderSnapshot_Texture(0,named.texture.c_str());RenderSnapshot_Skin("NONE_INSTANCED_DETAIL");
                const std::string normal_id="detail/"+named.name;
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"geometry_normal_source","VSDetail: NORMALIZE(WORLD_POSITION - INSTANCE_BASE_MINUS_0.75_Y)");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"normal_texture_variant","NOT_SUPPORTED_BY_VSDetail_PSGeometry");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"tangent_basis","NOT_PRESENT_IN_DETAIL_LAYOUT");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"normal_buffer_value","NOT_READ_BACK; PSGeometry NORMALIZATION_AND_FACE_SIGN");
                RenderSnapshot_Record("NORMAL_MAPPING",normal_id.c_str(),"uv_measurement","FIRST_FOUR_INDEXED_SAMPLES; NOT_FULL_RANGE");
                char wave[192];snprintf(wave,sizeof(wave),"%.9g,%.9g,%.9g,%.9g",source->wave[g.variant][0],source->wave[g.variant][1],source->wave[g.variant][2],source->wave[g.variant][3]);
                RenderSnapshot_Record("DETAIL_GEOMETRY",normal_id.c_str(),"wave_constants",wave);
                snprintf(wave,sizeof(wave),"%.9g,%.9g,%.9g,%.9g",source->wind[g.variant][0],source->wind[g.variant][1],source->wind[g.variant][2],source->wind[g.variant][3]);
                RenderSnapshot_Record("DETAIL_GEOMETRY",normal_id.c_str(),"wind_constants",wave);
                RenderSnapshot_RecordU64("DETAIL_GEOMETRY",normal_id.c_str(),"variant",g.variant);
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
            d.surface.textures[0]=mesh.texture;d.instance_buffer=shadow?shadow_instance_buffer:instance_buffer;d.first_instance=g.first;d.instance_count=g.count;
            if(l.Expect(s,shadow?"G3a detail shadow DrawIndexedInstanced":"G3a detail G-buffer DrawIndexedInstanced",api.draw(s.device,&d))<0)return s.failure;
            ++summary.calls;summary.variants[g.variant]+=g.count;
            if(shadow){++shadow_draw_calls;shadow_submitted_instances+=g.count;}else{++draw_calls;submitted_instances+=g.count;}
        }return 0;
    }
};
#endif
