#ifndef KICH_R4_MODEL_SESSION_H
#define KICH_R4_MODEL_SESSION_H
#include "KichR4Model.h"
#include "KichR4CaptureData.h"
#include "KichR4LevelSession.h"
#include "KichR4Visibility.h"
#include <stddef.h>
#include "KichR4TelemetryText.h"
#include "KichR4MaterialSession.h"
#include "KichR4Watch.h"
#include "KichR4ShadowRecord.h"
#include "KichR4LightProjector.h"
#include "KichR4NormalMapProbe.h"
/* EXE-owned immutable-source cache. Device destruction remains the final owner
 * of every successfully created handle, including partial construction. Weak
 * source ownership lets visual deletion retire GPU storage without retaining
 * the decoded mesh; an expired pointer key is removed before it can be reused. */
struct KichR4ModelSession {
    typedef kich_r4_capture::NativeModelCommand Command;
    typedef kich_r4_capture::Mesh Source;
    enum Pass {Geometry,Shadow,Forward,ForwardAdd,HUD,Distortion,HUDAdd,PassCount};
    // One immutable partition for each source triangle alignment (normally 0),
    // not one duplicated GPU buffer for every overlapping progressive LOD.
    struct BasisPartition {KichR4Handle ib;uint32_t good,bad;std::vector<uint32_t> prefix;
        BasisPartition():ib(0),good(0),bad(0){}
        bool Range(uint32_t first,uint32_t count,uint32_t& good_first,uint32_t& good_count,uint32_t& bad_first,uint32_t& bad_count)const{
            const size_t begin=first/3,end=begin+count/3;
            if(count%3 || end>=prefix.size())return false;
            good_first=prefix[begin];good_count=prefix[end]-good_first;
            bad_first=good+static_cast<uint32_t>(begin)*3-good_first;bad_count=count-good_count;return true;
        }
    };
    struct Mesh {std::weak_ptr<const Source> source;KichR4Handle vb,ib;uint64_t vertex_bytes,index_bytes;
        std::map<uint32_t,BasisPartition> basis;
        Mesh():vb(0),ib(0),vertex_bytes(0),index_bytes(0){} };
    KichR4ModelAPI api;
    struct PoseSlot {KichR4Handle handle;uint32_t capacity;bool used;PoseSlot(uint32_t c):handle(0),capacity(c),used(false){}};
    KichR4Handle vs[KICH_R4_MODEL_CAPACITY_CLASSES],pipelines[KICH_R4_MODEL_CAPACITY_CLASSES][PassCount],rigid_pose,white,plain_pipeline[KICH_R4_MODEL_CAPACITY_CLASSES];
    std::map<const Source*,Mesh> meshes;
    kich_r4_visibility::SunCasterVolume sun_casters;
    void SetSunCasterVolume(const kich_r4_visibility::SunCasterVolume& volume){sun_casters=volume;}
    std::vector<PoseSlot> poses;std::vector<KichR4Handle> command_poses;std::vector<uint32_t> command_classes;size_t pose_cursor[KICH_R4_MODEL_CAPACITY_CLASSES];
    // Immutable within one captured frame. Keep vector capacity across frames;
    // shadow faces only repeat visibility tests, not routing or matrix inversion.
    std::vector<size_t> pass_commands[PassCount];
    std::vector<kich_r4_scene::Matrix> command_normals;
    float sort_camera[PassCount][3];bool pass_sorted[PassCount];
    const kich_r4_capture::Batch* prepared;bool rigid_ready;
    uint32_t frame,commands,pose_uploads,mesh_uploads,draw_calls[PassCount];
    uint32_t hud_recorded_draws,hud_exposure_valid_draws,hud_mapped_draws,hud_unlit_draws;
    float hud_hemi_min,hud_hemi_max;
    uint64_t vertex_bytes,index_bytes,pose_upload_bytes,pose_logical_bytes,candidates[PassCount],culled[PassCount],selected[PassCount];
    uint64_t pose_command_logical_bytes,pose_api_bytes,pose_reuses;
    uint64_t basis_buffers,basis_bytes,basis_creations;
    std::vector<uint32_t> command_uses;std::map<std::string,uint64_t> pose_use_counts;bool model_telemetry_complete;
    uint64_t unique_skeletons,unique_parts,unique_poses,unknown_skeleton_commands,repeated_pose_uploads,max_pose_upload_multiplicity,pose_uses;
    uint64_t peak_pose_bytes,group_id,group_peak_pose_bytes;
    bool phase_active;char phase_section[96];uint64_t phase_candidates,phase_culled,phase_selected;uint32_t phase_draws;
    KichR4ModelSession():rigid_pose(0),white(0),prepared(NULL),rigid_ready(false),frame(UINT32_MAX),commands(0),pose_uploads(0),mesh_uploads(0),vertex_bytes(0),index_bytes(0),pose_upload_bytes(0),pose_logical_bytes(0),peak_pose_bytes(0),group_id(0),group_peak_pose_bytes(0){
        basis_buffers=basis_bytes=basis_creations=0;
        memset(&api,0,sizeof(api));memset(vs,0,sizeof(vs));memset(plain_pipeline,0,sizeof(plain_pipeline));memset(pipelines,0,sizeof(pipelines));ResetFrame();}
    void ResetFrame(){model_telemetry_complete=true;command_uses.clear();pose_use_counts.clear();unique_skeletons=unique_parts=unique_poses=unknown_skeleton_commands=repeated_pose_uploads=max_pose_upload_multiplicity=pose_uses=0;phase_active=false;phase_section[0]=0;phase_candidates=phase_culled=phase_selected=phase_draws=0;prepared=NULL;frame=UINT32_MAX;commands=pose_uploads=mesh_uploads=0;pose_upload_bytes=pose_logical_bytes=0;
        hud_recorded_draws=hud_exposure_valid_draws=hud_mapped_draws=hud_unlit_draws=0;hud_hemi_min=1;hud_hemi_max=0;
        command_normals.clear();for(unsigned i=0;i<PassCount;++i){pass_commands[i].clear();pass_sorted[i]=false;}
        basis_creations=0;pose_command_logical_bytes=pose_api_bytes=pose_reuses=0;command_poses.clear();command_classes.clear();memset(pose_cursor,0,sizeof(pose_cursor));for(size_t i=0;i<poses.size();++i)poses[i].used=false;
        memset(draw_calls,0,sizeof(draw_calls));memset(candidates,0,sizeof(candidates));memset(culled,0,sizeof(culled));memset(selected,0,sizeof(selected));}
    static uint32_t CapacityClass(uint32_t capacity){uint32_t index=0;for(uint32_t c=16;c<capacity;c*=2)++index;return index;}
    uint64_t AllocatedPoseBytes()const{uint64_t bytes=rigid_pose?KichR4ModelPaletteBytes(16):0;for(size_t i=0;i<poses.size();++i)if(poses[i].handle)bytes+=KichR4ModelPaletteBytes(poses[i].capacity);return bytes;}
    static bool ReusablePoseKey(const Command& c,uint32_t capture_frame,std::string& key){
        /* Identity is scoped to this capture generation; neither a mesh name,
         * part handle nor a telemetry hash establishes skeleton ownership.
         * Direct OGF indices have no per-mesh remap. Different spans or weight
         * layouts deliberately do not alias, even if their prefixes match. */
        if(!c.identity_measured || !c.skeleton_id || c.source_frame!=capture_frame)return false;
        const uint32_t words[]={c.source_frame,c.skin_mode,c.mesh->two_weights?1u:0u,static_cast<uint32_t>(c.bones.size()),1u};
        key.assign(reinterpret_cast<const char*>(&c.skeleton_id),sizeof(c.skeleton_id));key.append(reinterpret_cast<const char*>(words),sizeof(words));
        key.append(reinterpret_cast<const char*>(c.bones.data()),c.bones.size()*sizeof(kich_r4_scene::Matrix));return true;
    }
    int32_t ReservePosePool(KichR4FrameSession& s,KichR4LevelSession& l,const uint32_t* required){
        uint32_t available[KICH_R4_MODEL_CAPACITY_CLASSES]={};for(size_t i=0;i<poses.size();++i)if(poses[i].handle)++available[CapacityClass(poses[i].capacity)];
        uint64_t projected=AllocatedPoseBytes();for(unsigned i=0;i<KICH_R4_MODEL_CAPACITY_CLASSES;++i)if(required[i]>available[i])projected+=uint64_t(required[i]-available[i])*KichR4ModelPaletteBytes(16u<<i);
        /* Keep warm capacity until its unused classes would exceed the actual
         * 16MiB allocation budget. No resource selected by this frame is freed. */
        for(size_t i=0;i<poses.size() && projected>KICH_R4_MODEL_POSE_BUDGET_BYTES;){PoseSlot& p=poses[i];const uint32_t cls=CapacityClass(p.capacity);
            if(p.handle && available[cls]>required[cls]){if(l.Expect(s,"G3c retire excess pose capacity",l.bridge.resources.release_resource(s.device,p.handle))<0)return s.failure;
                --available[cls];projected-=KichR4ModelPaletteBytes(p.capacity);poses.erase(poses.begin()+i);}else ++i;}
        return projected>KICH_R4_MODEL_POSE_BUDGET_BYTES?s.Record("G3c GPU model pose budget (16 MiB)",KICH_R4_E_MEMORY):0;
    }
    int32_t AcquirePose(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t capacity,KichR4Handle& out){
        const uint32_t cls=CapacityClass(capacity);size_t index=pose_cursor[cls];for(;index<poses.size();++index)if(!poses[index].used && poses[index].capacity==capacity)break;
        if(index==poses.size())poses.push_back(PoseSlot(capacity));
        PoseSlot& p=poses[index];
        if(!p.handle && l.Expect(s,"G3c capacity-sized pose buffer",api.create_pose(s.device,capacity,&p.handle))<0)return s.failure;
        p.used=true;pose_cursor[cls]=index+1;out=p.handle;return 0;
    }
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
        Source::RangeInfo range;
        return c.mesh->Range(c.first,c.count,c.vertex_count,range);
    }
    int32_t ReleaseMesh(KichR4FrameSession& s,KichR4LevelSession& l,Mesh& m){
        for(auto& p:m.basis)if(p.second.ib){if(l.Expect(s,"E1 retire basis partition",l.bridge.resources.release_resource(s.device,p.second.ib))<0)return s.failure;
            --basis_buffers;basis_bytes-=uint64_t(p.second.good+p.second.bad)*2;p.second.ib=0;}
        m.basis.clear();
        if(m.vb){if(l.Expect(s,"G3b retire model vertices",l.bridge.resources.release_resource(s.device,m.vb))<0)return s.failure;m.vb=0;vertex_bytes-=m.vertex_bytes;m.vertex_bytes=0;}
        if(m.ib){if(l.Expect(s,"G3b retire model indices",l.bridge.resources.release_resource(s.device,m.ib))<0)return s.failure;m.ib=0;index_bytes-=m.index_bytes;m.index_bytes=0;}return 0;
    }
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& l,const char* hlsl,KichR4Handle geometry_ps,KichR4Handle shadow_ps,
                  KichR4Handle forward_ps,KichR4Handle local_ps,KichR4Handle plain_ps,KichR4Handle distortion_ps,KichR4Handle white_texture){
        static_assert(sizeof(kich_r4_capture::SourceVertex)==sizeof(KichR4ModelVertex),"source model GPU stride");
        static_assert(offsetof(kich_r4_capture::SourceVertex,bone)==24 && offsetof(kich_r4_capture::SourceVertex,normal)==28,"source model GPU offsets");
        if(!api.create_pipeline || !api.create_pose || !api.upload_pose || !api.draw)return s.Record("G3b model API",KICH_R4_E_STATE);
        white=white_texture;
        const KichR4Handle ps[PassCount]={geometry_ps,shadow_ps,forward_ps,local_ps,forward_ps,distortion_ps,local_ps};
        for(unsigned cls=0;cls<KICH_R4_MODEL_CAPACITY_CLASSES;++cls){const uint32_t capacity=16u<<cls;
            if(!vs[cls]){KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=KICH_R4_VERTEX_SHADER;
                memcpy(d.entry,"VSModelGPU",11);snprintf(d.name,sizeof(d.name),"kich_g3c_models_%u",capacity);
                char prefix[96];snprintf(prefix,sizeof(prefix),"#define KICH_R4_MODEL_BONE_CAPACITY %u\n",capacity);const std::string source=std::string(prefix)+hlsl;
                if(l.Expect(s,"G3c capacity-specific VSModelGPU",l.bridge.resources.compile_shader(s.device,&d,source.c_str(),static_cast<uint32_t>(source.size()),&vs[cls],l.bridge.shader_error,sizeof(l.bridge.shader_error)))<0)return s.failure;}
            for(unsigned i=0;i<PassCount;++i)if(!pipelines[cls][i]){const KichR4ModelPipelineDesc p={sizeof(p),vs[cls],ps[i],capacity};
                if(l.Expect(s,"G3c model input layout and palette class",api.create_pipeline(s.device,&p,&pipelines[cls][i]))<0)return s.failure;}
            if(!plain_pipeline[cls]){const KichR4ModelPipelineDesc p={sizeof(p),vs[cls],plain_ps,capacity};
                if(l.Expect(s,"G3c unlit model layout",api.create_pipeline(s.device,&p,&plain_pipeline[cls]))<0)return s.failure;}
        }
        if(!rigid_pose && l.Expect(s,"G3c rigid model constants",api.create_pose(s.device,16,&rigid_pose))<0)return s.failure;
        if(!rigid_ready){const KichR4ModelPose p={};if(l.Expect(s,"G3c rigid model pose",api.upload_pose(s.device,rigid_pose,&p))<0)return s.failure;rigid_ready=true;}
        return 0;
    }
    int32_t Prepare(KichR4FrameSession& s,KichR4LevelSession& l,const kich_r4_capture::Batch& batch){
        ResetFrame();frame=batch.engine_frame;
        if(batch.native_models.size()>KICH_R4_MAX_MODEL_POSE_SLOTS)return s.Record("G3c model command budget",KICH_R4_E_MEMORY);
        std::vector<size_t> owners(batch.native_models.size());std::map<std::string,size_t> shared;
        uint32_t required[KICH_R4_MODEL_CAPACITY_CLASSES]={};uint64_t needed=KichR4ModelPaletteBytes(16);
        for(size_t i=0;i<batch.native_models.size();++i){const Command& c=batch.native_models[i];owners[i]=i;
            if(!ValidCommand(c))return s.Record("G3c model range or pose",KICH_R4_E_ARGUMENT);
            if(!c.mesh->skinned)continue;
            const uint32_t capacity=KichR4ModelPaletteCapacity(static_cast<uint32_t>(c.bones.size()));
            if(!capacity)return s.Record("G3c model bone capacity",KICH_R4_E_ARGUMENT);
            std::string key;if(ReusablePoseKey(c,frame,key)){const auto found=shared.find(key);if(found!=shared.end()){owners[i]=found->second;continue;}shared.insert(std::make_pair(std::move(key),i));}
            ++required[CapacityClass(capacity)];needed+=KichR4ModelPaletteBytes(capacity);
        }
        if(needed>KICH_R4_MODEL_POSE_BUDGET_BYTES)return s.Record("G3c GPU model pose budget (16 MiB)",KICH_R4_E_MEMORY);
        if(ReservePosePool(s,l,required)<0)return s.failure;
        for(std::map<const Source*,Mesh>::iterator i=meshes.begin();i!=meshes.end();){if(i->second.source.expired()){
                if(ReleaseMesh(s,l,i->second)<0)return s.failure;
                i=meshes.erase(i);}else ++i;}
        command_poses.reserve(batch.native_models.size());command_classes.reserve(batch.native_models.size());command_normals.reserve(batch.native_models.size());
        if(g_render_snapshot_active)try{command_uses.resize(batch.native_models.size(),0);}catch(...){model_telemetry_complete=false;RenderSnapshot_Text("TELEMETRY_LOSS","model_pass_uses","ALLOCATION_FAILED");}
        std::set<uint64_t> skeleton_ids,part_ids;std::map<std::string,uint64_t> pose_counts,pose_bytes,pose_reuse_counts;
        for(size_t i=0;i<batch.native_models.size();++i){const Command& c=batch.native_models[i];
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
            const uint32_t basis=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
            if(c.has_material && kich_r4_material::Geometry(c.material) && !c.material.bump.empty() && (c.available_attributes&basis)!=basis){
                const uint32_t alignment=c.first%3;BasisPartition& part=gpu.basis[alignment];
                if(part.prefix.empty()){
                    part.prefix.push_back(0);
                    for(size_t at=alignment;at+2<c.mesh->indices.size();at+=3){bool valid=true;
                        for(unsigned j=0;j<3;++j)valid=valid && (c.mesh->vertices[c.mesh->indices[at+j]].attributes&basis)==basis;
                        part.prefix.push_back(part.prefix.back()+(valid?3u:0u));}
                    part.good=part.prefix.back();part.bad=static_cast<uint32_t>((part.prefix.size()-1)*3)-part.good;
                }
                uint32_t good_first,good_count,bad_first,bad_count;
                if(!part.Range(c.first,c.count,good_first,good_count,bad_first,bad_count))return s.Record("E1.8 model partition range",KICH_R4_E_ARGUMENT);
                if(good_count && bad_count && !part.ib){
                    std::vector<uint16_t> good,bad;good.reserve(part.good+part.bad);bad.reserve(part.bad);
                    for(size_t triangle=0;triangle+1<part.prefix.size();++triangle){const size_t at=alignment+triangle*3;
                        auto& target=part.prefix[triangle+1]!=part.prefix[triangle]?good:bad;
                        target.insert(target.end(),c.mesh->indices.begin()+at,c.mesh->indices.begin()+at+3);}
                    good.insert(good.end(),bad.begin(),bad.end());const uint32_t bytes=static_cast<uint32_t>(good.size()*2);
                        if(vertex_bytes+index_bytes+bytes>256u*1024u*1024u)return s.Record("E1 model partition memory",KICH_R4_E_MEMORY);
                        const KichR4BufferDesc desc={sizeof(desc),KICH_R4_INDEX_BUFFER,bytes,2};
                        if(l.Expect(s,"E1 immutable basis partition",l.bridge.resources.create_buffer(s.device,&desc,good.data(),bytes,&part.ib))<0)return s.failure;
                    gpu.index_bytes+=bytes;index_bytes+=bytes;++basis_buffers;basis_bytes+=bytes;++basis_creations;
                }
            }
            KichR4Handle pose=rigid_pose;
            const uint32_t capacity=KichR4ModelPaletteCapacity(static_cast<uint32_t>(c.bones.size()));
            command_classes.push_back(c.mesh->skinned?CapacityClass(capacity):0);
            if(c.mesh->skinned){pose_command_logical_bytes+=c.bones.size()*64;
                const bool reuse=owners[i]!=i;
                if(reuse){pose=command_poses[owners[i]];++pose_reuses;}
                else{if(AcquirePose(s,l,capacity,pose)<0)return s.failure;
                    KichR4ModelPose p;p.bone_count=static_cast<uint32_t>(c.bones.size());p.skin_mode=c.skin_mode;p.skinned=1;p.reserved=0;
                    for(size_t j=0;j<c.bones.size();++j)memcpy(p.bones[j],c.bones[j].m,64);
                    if(l.Expect(s,"G3c skeletal pose WRITE_DISCARD",api.upload_pose(s.device,pose,&p))<0)return s.failure;
                    ++pose_uploads;pose_upload_bytes+=KichR4ModelPaletteBytes(capacity);pose_logical_bytes+=c.bones.size()*64;pose_api_bytes+=16+c.bones.size()*64;}
                if(g_render_snapshot_active && model_telemetry_complete)try{if(c.identity_measured && c.skeleton_id){skeleton_ids.insert(c.skeleton_id);if(c.pose_hash_valid){char id[128];PoseID(c,id,sizeof(id));
                        if(reuse)++pose_reuse_counts[id];else{const uint64_t multiplicity=++pose_counts[id];pose_bytes[id]+=KichR4ModelPaletteBytes(capacity);max_pose_upload_multiplicity=std::max(max_pose_upload_multiplicity,multiplicity);if(multiplicity>1)++repeated_pose_uploads;}}}else ++unknown_skeleton_commands;}
                    catch(...){model_telemetry_complete=false;RenderSnapshot_Text("TELEMETRY_LOSS","model_identity_counts","ALLOCATION_FAILED");}}
            if(g_render_snapshot_active && model_telemetry_complete && c.identity_measured && c.visual_id)try{part_ids.insert(c.visual_id);}catch(...){model_telemetry_complete=false;RenderSnapshot_Text("TELEMETRY_LOSS","model_part_counts","ALLOCATION_FAILED");}
            command_poses.push_back(pose);command_normals.push_back(kich_r4_lighting::NormalMatrix(c.world));
            for(unsigned pass=0;pass<PassCount;++pass)if(Routes(c,static_cast<Pass>(pass)))pass_commands[pass].push_back(i);
            if(g_render_snapshot_active){SnapshotCommandDetails(c,i,frame);
                if(RenderSnapshot_Detailed()){char id[96];snprintf(id,sizeof(id),"frame_%u_command_%u",frame,static_cast<unsigned>(i));
                    kich_r4_telemetry::U64("MODEL_COMMAND",id,"palette_capacity_bones",c.mesh->skinned?capacity:16);
                    kich_r4_telemetry::U64("MODEL_COMMAND",id,"palette_buffer_bytes",KichR4ModelPaletteBytes(c.mesh->skinned?capacity:16));
                    kich_r4_telemetry::U64("MODEL_COMMAND",id,"palette_reused",c.mesh->skinned && owners[i]!=i?1:0);
                    kich_r4_telemetry::U64("MODEL_COMMAND",id,"palette_owner_command",owners[i]);}}
        }
        /* Keep warm-frame palette capacity. Empty frames still retire geometry
         * through weak ownership and can never replay an earlier command. */
        commands=static_cast<uint32_t>(batch.native_models.size());prepared=&batch;
        unique_skeletons=skeleton_ids.size();unique_parts=part_ids.size();unique_poses=pose_counts.size();
        if(g_render_snapshot_active)for(auto it=pose_counts.begin();it!=pose_counts.end();++it){kich_r4_telemetry::U64("MODEL_POSE",it->first.c_str(),"uploads_this_frame",it->second);
            kich_r4_telemetry::U64("MODEL_POSE",it->first.c_str(),"uploaded_bytes",pose_bytes[it->first]);kich_r4_telemetry::U64("MODEL_POSE",it->first.c_str(),"allocated_bytes_for_commands",pose_bytes[it->first]);
            kich_r4_telemetry::U64("MODEL_POSE",it->first.c_str(),"reused_command_palettes",pose_reuse_counts[it->first]);
            kich_r4_telemetry::Text("MODEL_POSE",it->first.c_str(),"duplicate_upload_reason",it->second>1?"IDENTITY_OR_LAYOUT_OR_EXACT_POSE_DIFFERS":"NOT_DUPLICATED");}
        const uint64_t allocated=AllocatedPoseBytes();peak_pose_bytes=std::max(peak_pose_bytes,allocated);
        const uint64_t current_group=RenderSnapshot_GroupID();if(current_group!=group_id){group_id=current_group;group_peak_pose_bytes=0;}if(group_id)group_peak_pose_bytes=std::max(group_peak_pose_bytes,allocated);
        return 0;
    }
    static bool SourceOpaque(const Command& c){return c.has_material && c.material.semantic!=kich_r4_material::UNKNOWN?
        kich_r4_material::Geometry(c.material):!c.style.blend && c.style.depth && c.style.write && (!c.effect || c.style.emissive_gain>0);}
    static bool Opaque(const Command& c){return !c.layer && SourceOpaque(c);}
    static bool Routes(const Command& c,Pass pass){
        if(c.effect==5 || (c.has_material && c.material.semantic==kich_r4_material::DISTORTION))return pass==Distortion;
        if(pass==Distortion)return false;
        if(pass==Shadow && c.style.emissive_gain>0)return false;
        if((pass==ForwardAdd || pass==HUDAdd) && c.has_material && c.material.semantic==kich_r4_material::EMISSIVE)return false;
        if(c.layer==1)return pass==HUD || (pass==HUDAdd && (!c.effect || c.effect==6));
        if(pass==HUD || pass==HUDAdd)return false;
        if(pass==Geometry || pass==Shadow)return Opaque(c) && (pass!=Shadow || !c.has_material || c.material.semantic==kich_r4_material::UNKNOWN || kich_r4_material::Shadow(c.material));
        if(pass==ForwardAdd && c.effect && c.effect!=6)return false;
        return !Opaque(c);
    }
    static bool LightAffects(const Command& c,const kich_r4_lighting::Constants& frame){
        if(!c.bounds_known || !c.bounds.valid || !kich_r4_capture::Finite(frame.light_position[3]) || frame.light_position[3]<=0)return true;
        double distance=0;
        for(unsigned i=0;i<3;++i){const double p=frame.light_position[i];
            if(!kich_r4_capture::Finite(frame.light_position[i]) || !kich_r4_capture::Finite(c.bounds.min[i]) ||
                !kich_r4_capture::Finite(c.bounds.max[i]) || c.bounds.min[i]>c.bounds.max[i])return true;
            const double delta=p<c.bounds.min[i]?c.bounds.min[i]-p:p>c.bounds.max[i]?p-c.bounds.max[i]:0;distance+=delta*delta;}
        const double range=frame.light_position[3],conservative=range+1.e-4*(1+range);
        return distance<=conservative*conservative;
    }
    static bool Visible(const Command& c,const kich_r4_lighting::Constants& frame,Pass pass){
        // VSModelGPU supplies world-space lighting coordinates even for HUD:
        // the separate hands VP affects clip position only. The same world
        // bounds can reject zero-contribution local lights without applying
        // the ordinary camera frustum to the HUD projection.
        if((pass==ForwardAdd || pass==HUDAdd) && !LightAffects(c,frame))return false;
        if(!c.bounds_known || !c.bounds.valid || pass==HUD || pass==HUDAdd)return true;
        return kich_r4_visibility::Visible(c.bounds.min,c.bounds.max,frame.mvp);
    }
    static double Distance(const Command& c,const kich_r4_lighting::Constants& frame){double distance=0;
        for(unsigned i=0;i<3;++i){const double center=c.bounds_known?(double(c.bounds.min[i])+c.bounds.max[i])*.5:c.world.m[12+i];const double d=center-frame.camera[i];distance+=d*d;}return distance;}
    static void PoseID(const Command& c,char* id,size_t size){snprintf(id,size,"frame_%u_skeleton_%llu_pose_%016llx_bones_%u_skin_%u_2w_%u",c.source_frame,static_cast<unsigned long long>(c.skeleton_id),static_cast<unsigned long long>(c.pose_hash),static_cast<unsigned>(c.bones.size()),c.skin_mode,c.mesh->two_weights?1:0);}
    static void SnapshotCommandDetails(const Command& c,size_t index,uint32_t current_frame){
        using namespace kich_r4_telemetry;if(!g_render_snapshot_active || !RenderSnapshot_Detailed())return;
        char id[96],mesh[160],pose[128];snprintf(id,sizeof(id),"frame_%u_command_%u",current_frame,static_cast<unsigned>(index));PoseID(c,pose,sizeof(pose));
        kich_r4_capture::SnapshotModelID(*c.mesh,mesh,sizeof(mesh),0,static_cast<uint32_t>(c.mesh->indices.size()));
        Text("MODEL_COMMAND",id,"mesh_resource_id",mesh);Text("MODEL_COMMAND",id,"resource",c.resource.c_str());
        Text("MODEL_COMMAND",id,"identity_scope",c.identity_measured?"FRAME_SCOPED_PROCESS_POINTER; NOT_NPC_ID":"NOT_INSTRUMENTED");
        U64("MODEL_COMMAND",id,"visual_part_handle",c.visual_id);U64("MODEL_COMMAND",id,"skeleton_handle",c.skeleton_id);
        U64("MODEL_COMMAND",id,"source_frame",c.source_frame);U64("MODEL_COMMAND",id,"source_age_frames",c.identity_measured?uint32_t(current_frame-c.source_frame):0);
        Text("MODEL_COMMAND",id,"source_age_valid",c.identity_measured?"1":"0");Text("MODEL_COMMAND",id,"pose_id",c.mesh->skinned?(c.pose_hash_valid?pose:"NOT_MEASURED"):"NOT_APPLICABLE");
        U64("MODEL_COMMAND",id,"vertex_prefix",c.vertex_count);U64("MODEL_COMMAND",id,"first_index",c.first);U64("MODEL_COMMAND",id,"index_count",c.count);
        Text("MODEL_COMMAND",id,"lod_range_source","VISUAL_RENDER_ARGUMENTS; SWI_INDEX_NOT_EXPOSED");
        Text("MODEL_COMMAND",id,"bounds_method",c.mesh->skinned?"UNION_OF_TRANSFORMED_PER_BONE_SOURCE_AABBS":"TRANSFORMED_SOURCE_AABB");
        Text("MODEL_COMMAND",id,"bounds_valid",c.bounds_known && c.bounds.valid?"1":"0");
        Text("MODEL_COMMAND",id,"bounds_unknown_reason",c.bounds_known?"NOT_APPLICABLE":c.mesh->two_weights && c.mesh->outside_weights && c.skin_mode==KICH_R4_SKIN_SOFTWARE?"RAW_2W_EXTRAPOLATION":"MISSING_OR_NONAFFINE_BOUNDS");
        Text("MODEL_COMMAND",id,"unknown_bounds_policy","CONSERVATIVE_NO_CULL");Vector("MODEL_COMMAND",id,"world",c.world.m,16);Vector("MODEL_COMMAND",id,"projection_view",c.vp.m,16);
        if(c.bounds_known){Vector("MODEL_COMMAND",id,"bounds_min",c.bounds.min,3);Vector("MODEL_COMMAND",id,"bounds_max",c.bounds.max,3);}
        Text("MODEL_COMMAND",id,"candidate_source","CAMERA_OR_SELECTED_SHADOW_SPATIAL_UNION; PER_OBJECT_QUERY_MASK_NOT_RETAINED");
        Text("MODEL_COMMAND",id,"sectors_portals_hom","NOT_USED_BY_NATIVE_MODEL_SELECTOR; FRUSTUM_ONLY");
        if(c.mesh->skinned && c.pose_hash_valid){Text("MODEL_POSE",pose,"version_source","FNV64_OF_COPIED_MATRIX_BYTES; NOT_ENGINE_ANIMATION_GENERATION");U64("MODEL_POSE",pose,"source_frame",c.source_frame);
            U64("MODEL_POSE",pose,"skeleton_process_handle",c.skeleton_id);U64("MODEL_POSE",pose,"matrix_count",c.bones.size());U64("MODEL_POSE",pose,"logical_bytes",c.bones.size()*64);
            Text("MODEL_POSE",pose,"weight_mode",c.mesh->two_weights?(c.skin_mode==KICH_R4_SKIN_HARDWARE?"2W_CLAMP_U8":"2W_RAW"):"1W");
            Text("MODEL_POSE",pose,"upload_reuse_policy","FRAME_SCOPED_SKELETON_AND_GENERATION; EXACT_MATRIX_BYTES; DIRECT_INDEX_SPAN_AND_WEIGHT_MODE; WORLD_SEPARATE");}
        const kich_r4_scene::Matrix normal=kich_r4_lighting::NormalMatrix(c.world);Vector("NORMAL_MAPPING",id,"normal_matrix",normal.m,16);
        const float* w=c.world.m;const float determinant=w[0]*(w[5]*w[10]-w[6]*w[9])-w[1]*(w[4]*w[10]-w[6]*w[8])+w[2]*(w[4]*w[9]-w[5]*w[8]);
        const float scale2[]={w[0]*w[0]+w[1]*w[1]+w[2]*w[2],w[4]*w[4]+w[5]*w[5]+w[6]*w[6],w[8]*w[8]+w[9]*w[9]+w[10]*w[10]};
        Float("NORMAL_MAPPING",id,"world_determinant3x3",determinant);U64("NORMAL_MAPPING",id,"negative_orientation",determinant<0?1:0);Vector("NORMAL_MAPPING",id,"world_axis_length_squared",scale2,3);
        const float largest=std::max(scale2[0],std::max(scale2[1],scale2[2])),smallest=std::min(scale2[0],std::min(scale2[1],scale2[2]));
        U64("NORMAL_MAPPING",id,"nonuniform_axis_scale",largest-smallest>1.e-5f*std::max(1.f,largest)?1:0);
        kich_r4_scene::Matrix inverse;Text("NORMAL_MAPPING",id,"normal_matrix_source",kich_r4_lighting::Inverse(c.world,inverse)?"WORLD_INVERSE_TRANSPOSE":"IDENTITY_FALLBACK_NONINVERTIBLE_WORLD");
        Text("NORMAL_MAPPING",id,"geometry_layout","POSITION_FLOAT3; UV_FLOAT2; WEIGHT_FLOAT; BONE_UINT16X2; N_T_B_UNORM10X3; UV1_HEMI_SUN_COLOR_ATTRIBUTES");U64("NORMAL_MAPPING",id,"vertex_stride",sizeof(kich_r4_capture::SourceVertex));
        U64("NORMAL_MAPPING",id,"available_attributes",c.mesh->available_attributes);U64("NORMAL_MAPPING",id,"basis_vertices",c.mesh->basis_vertices);U64("NORMAL_MAPPING",id,"derived_basis_vertices",c.mesh->derived_basis_vertices);
        Text("NORMAL_MAPPING",id,"tangent_basis","IMMUTABLE_SOURCE_OR_LOAD_TIME_DERIVATION; SKIN_DIRECTIONS_AND_WORLD_TANGENTS; HANDEDNESS_FROM_BINORMAL");
        Text("NORMAL_MAPPING",id,"texture_normal_branch",c.has_material && kich_r4_material::Bumped(c.material)?"SOC_R2_BUMP_REQUESTED; SEE_MATERIAL_EXECUTION_FOR_BOUND_RESOURCES":"GEOMETRIC_NORMAL");
        Text("NORMAL_MAPPING",id,"normal_transform","BONE_DIRECTION_BLEND_THEN_WORLD_INVERSE_TRANSPOSE; WORLD_SPACE");Text("NORMAL_MAPPING",id,"pixel_contribution","NOT_MEASURED");
        const size_t checked=std::min(size_t(128),c.mesh->vertices.size());size_t zero=0;for(size_t i=0;i<checked;++i)if(!c.mesh->vertices[i].normal)++zero;
        U64("NORMAL_MAPPING",id,"normal_sample_vertices",checked);U64("NORMAL_MAPPING",id,"zero_packed_normal_samples",zero);Text("NORMAL_MAPPING",id,"normal_check_scope",checked==c.mesh->vertices.size()?"ALL_SOURCE_VERTICES":"FIRST_128_SOURCE_VERTICES");
    }
    static void SnapshotMaterialBasis(const Command& c,uint32_t flags){
        if(!RenderSnapshot_Detailed())return;
        Source::RangeInfo range;
        const uint32_t valid=c.mesh->Range(c.first,c.count,c.vertex_count,range)?range.basis_indices:0;
        char id[64];kich_r4_material::ExecutionID(c.material,c.available_attributes,flags,false,id,sizeof(id));
        using namespace kich_r4_telemetry;
        Text("MODEL_MATERIAL_EXECUTION",id,"basis_scope","LAST_BOUND_COMMAND; INDEX_REFERENCES_INCLUDE_DUPLICATES; MESH_COUNTS_ARE_FULL_SOURCE");
        Text("MODEL_MATERIAL_EXECUTION",id,"basis_resource",c.resource.c_str());
        U64("MODEL_MATERIAL_EXECUTION",id,"basis_index_references",c.count);
        U64("MODEL_MATERIAL_EXECUTION",id,"basis_valid_index_references",valid);
        U64("MODEL_MATERIAL_EXECUTION",id,"basis_missing_index_references",c.count-valid);
        U64("MODEL_MATERIAL_EXECUTION",id,"basis_mesh_vertices",c.mesh->vertices.size());
        U64("MODEL_MATERIAL_EXECUTION",id,"basis_mesh_derived_vertices",c.mesh->derived_basis_vertices);
    }
    static void Snapshot(const Command& c,uint32_t pass_mask){
        const char* semantic=c.layer?"HUD_MODEL":"DYNAMIC_MODEL";
        RenderSnapshot_Context(semantic,c.resource.empty()?"UNKNOWN":c.resource.c_str(),c.shader.empty()?"UNKNOWN":c.shader.c_str());
        if(c.provenance)RenderSnapshot_SetProvenance(c.provenance.get());
        else{char mesh[160];kich_r4_capture::SnapshotModelID(*c.mesh,mesh,sizeof(mesh),c.first,c.count);RenderSnapshot_Object(semantic,mesh,"NOT_AVAILABLE","IMMUTABLE_OGF_SOURCE",0);}
        RenderSnapshot_Queue(c.layer?"R4_NATIVE_HUD_MODEL":"R4_NATIVE_MODEL");RenderSnapshot_PassMask(pass_mask);
        for(unsigned i=0;i<8;++i)RenderSnapshot_Texture(i,"UNBOUND");
        RenderSnapshot_Texture(0,c.has_material && !c.material.base.empty()?c.material.base.c_str():c.texture.c_str());
        if(c.has_material){char id[64];snprintf(id,sizeof(id),"NATIVE_MATERIAL:%016llx",static_cast<unsigned long long>(kich_r4_material::Key(c.material)));RenderSnapshot_MaterialRoute(id);
            kich_r4_telemetry::Text("NATIVE_MATERIAL",id,"semantic",kich_r4_material::Name(c.material.semantic));
            kich_r4_telemetry::Text("NATIVE_MATERIAL",id,"source",c.material.source.c_str());
            kich_r4_telemetry::Text("NATIVE_MATERIAL",id,"fallback_reason",c.material.unsupported_reason.empty()?"NONE":c.material.unsupported_reason.c_str());}
        RenderSnapshot_Skin(!c.mesh->skinned?"NONE":!c.mesh->two_weights?"GPU_1W":c.skin_mode==KICH_R4_SKIN_HARDWARE?"GPU_2W_HARDWARE_U8":"GPU_2W_SOFTWARE_RAW");
    }
    static void Note(const Command& c,const char* status,const char* reason){RenderSnapshotProvenance p={};RenderSnapshot_GetProvenance(&p);
        RenderSnapshot_Note(status,p.semantic_type[0]?p.semantic_type:c.layer?"HUD_MODEL":"DYNAMIC_MODEL",p.mesh_id[0]?p.mesh_id:"NOT_AVAILABLE",reason);}
    void BeginPhase(const char* phase){phase_active=true;phase_candidates=phase_culled=phase_selected=phase_draws=0;
        snprintf(phase_section,sizeof(phase_section),"MODEL_PASS %s",phase?phase:"unspecified");}
    void EndPhase(){if(!phase_active)return;
        if(g_render_snapshot_active){RenderSnapshot_U64(phase_section,"candidate_observations",phase_candidates);
            RenderSnapshot_U64(phase_section,"culled_observations",phase_culled);
            RenderSnapshot_U64(phase_section,"selected_observations",phase_selected);RenderSnapshot_U64(phase_section,"draw_calls",phase_draws);}
        phase_active=false;}
    static void BindHUDMaterial(KichR4FrameSession& s,KichR4LevelSession& level,const Command& command,
        kich_r4_lighting::Constants& constants,KichR4ModelDrawDesc& draw){
        const kich_r4_material::Descriptor& material=command.material;std::string fallback;bool dummy=false;
        const bool mapped=kich_r4_material::BindBump(s,level,material,command.available_attributes,
            draw.material_textures[0],draw.material_textures[1],fallback,dummy);
        constants.material[0]=mapped?float(kich_r4_material::SHADER_BUMP|kich_r4_material::SHADER_MODEL_MAPS):0;
        if(mapped){draw.material_samplers[0]=material.surface.samplers[1];draw.material_samplers[1]=material.surface.samplers[2];}
        if(g_render_snapshot_active){char id[64];kich_r4_material::ExecutionID(material,command.available_attributes,uint32_t(constants.material[0]),false,id,sizeof(id));
            using namespace kich_r4_telemetry;Text("HUD_MATERIAL_EXECUTION",id,"resource",command.resource.c_str());
            Text("HUD_MATERIAL_EXECUTION",id,"normal_path",mapped?"SOC_R2_BUMP; WORLD_TBN; MODEL_T8_T9":"GEOMETRIC_NORMAL");
            Text("HUD_MATERIAL_EXECUTION",id,"gloss_path",mapped?"BUMP_R_SQUARED":"FLAT_2_OVER_255");
            Text("HUD_MATERIAL_EXECUTION",id,"bump",mapped?material.bump.c_str():"UNBOUND");
            Text("HUD_MATERIAL_EXECUTION",id,"bump_aux",mapped?(dummy?"ed\\ed_dummy_bump#":material.bump_aux.c_str()):"UNBOUND");
            U64("HUD_MATERIAL_EXECUTION",id,"texture8",draw.material_textures[0]);U64("HUD_MATERIAL_EXECUTION",id,"texture9",draw.material_textures[1]);
            Text("HUD_MATERIAL_EXECUTION",id,"fallback_reason",fallback.empty()?"NONE":fallback.c_str());
            Text("HUD_MATERIAL_EXECUTION",id,"sun_policy",SourceOpaque(command)?"CURRENT_PCF; ROS_HEMI_ONLY":"SOURCE_FORWARD_ROS_TIMES_CURRENT_PCF");
            Text("HUD_MATERIAL_EXECUTION",id,"pixel_contribution","NOT_MEASURED");}
    }
    static void RecordNormalSubmission(const Command& command,Pass pass,const kich_r4_lighting::Constants& constants,
        const KichR4ModelDrawDesc& draw,uint32_t attributes,bool plain){
        if(!kich_r4_record::Enabled() || (pass!=Geometry && pass!=HUD))return;
        const auto& material=command.material;const uint32_t basis=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
        kich_r4_normal_probe::Input input;input.known=command.has_material && material.semantic!=kich_r4_material::UNKNOWN;
        input.authored=!material.bump.empty();input.basis=(attributes&basis)==basis;input.metadata=!material.bump_aux.empty();input.path_enabled=!plain;
        input.flags=uint32_t(constants.material[0]);input.attributes=attributes;input.index_count=draw.surface.state.draw.index_count;input.identity=command.visual_id;
        input.mapped=(input.flags&kich_r4_material::SHADER_BUMP)!=0 && (pass==HUD?
            draw.material_textures[0] && draw.material_textures[1]:draw.surface.textures[1] && draw.surface.textures[2]);
        input.resource=command.resource.c_str();input.base=material.base.empty()?command.texture.c_str():material.base.c_str();
        input.bump=material.bump.c_str();input.auxiliary=material.bump_aux.c_str();
        kich_r4_normal_probe::Global().Observe(pass==HUD?kich_r4_normal_probe::HUD:kich_r4_normal_probe::Model,input);
    }
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle cb,const kich_r4_lighting::Constants& base,
                 const kich_r4_capture::Batch& batch,Pass pass,uint32_t pass_mask,const char* phase,const KichR4Handle* maps=NULL,KichR4Handle scene_color=0,uint32_t only_index=UINT32_MAX){
        if(pass>=PassCount || prepared!=&batch || frame!=batch.engine_frame || command_poses.size()!=batch.native_models.size() || command_classes.size()!=batch.native_models.size() || command_normals.size()!=batch.native_models.size())return s.Record("G3b unprepared model frame",KICH_R4_E_STATE);
        if(only_index!=UINT32_MAX && only_index>=batch.native_models.size())return s.Record("G3b model command index",KICH_R4_E_ARGUMENT);
        struct PhaseScope {KichR4ModelSession& session;bool automatic;
            PhaseScope(KichR4ModelSession& owner,const char* name):session(owner),automatic(!owner.phase_active){if(automatic)session.BeginPhase(name);}
            ~PhaseScope(){if(automatic)session.EndPhase();}
        } scope(*this,phase);
        std::vector<size_t>& order=pass_commands[pass];
        if(only_index==UINT32_MAX && (pass==Forward || pass==ForwardAdd || pass==Distortion) &&
            (!pass_sorted[pass] || memcmp(sort_camera[pass],base.camera,sizeof(sort_camera[pass])))){
            // Reset equal-distance ties to original capture order even if a
            // caller changed the camera after an earlier ordered submission.
            if(pass_sorted[pass])std::sort(order.begin(),order.end());
            std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return Distance(batch.native_models[a],base)>Distance(batch.native_models[b],base);});
            memcpy(sort_camera[pass],base.camera,sizeof(sort_camera[pass]));pass_sorted[pass]=true;
        }
        const bool sun_pass=pass==Shadow && phase && !strncmp(phase,"sun_",4);
        const char* reason=sun_pass?"SHADOW_FRUSTUM_AND_SUN_RECEIVERS":pass==Shadow?"SHADOW_FRUSTUM":pass==HUD?"HUD_EXPLICIT":pass==HUDAdd?"HUD_WORLD_LIGHT_RANGE":pass==ForwardAdd?"CAMERA_FRUSTUM_AND_LIGHT_RANGE":"CAMERA_FRUSTUM";
        const size_t count=only_index==UINT32_MAX?order.size():1;
        for(size_t n=0;n<count;++n){const size_t index=only_index==UINT32_MAX?order[n]:only_index;const Command& c=batch.native_models[index];if(only_index!=UINT32_MAX && !Routes(c,pass))continue;
            ++candidates[pass];++phase_candidates;if(g_render_snapshot_active){Snapshot(c,pass_mask);Note(c,"CANDIDATE_CLASSIFIED",reason);}
            if(!Visible(c,base,pass) || (sun_pass && c.bounds_known && c.bounds.valid && !sun_casters.Visible(c.bounds.min,c.bounds.max))){++culled[pass];++phase_culled;if(g_render_snapshot_active)Note(c,"CULLED",reason);continue;}
            ++selected[pass];++phase_selected;if(g_render_snapshot_active)Note(c,"SELECTED",reason);
            const Mesh& mesh=meshes.at(c.mesh.get());kich_r4_lighting::Constants constants=base;
            constants.world=c.world;constants.mvp=kich_r4_scene::Multiply(c.world,(pass==HUD || pass==HUDAdd)?c.vp:base.mvp);constants.normal_matrix=command_normals[index];
            constants.alpha[0]=kich_r4_material::AlphaReference(c.style,pass==Shadow);constants.alpha[1]=static_cast<float>(kich_r4_material::AlphaFunction(c.style,pass==Shadow));constants.alpha[2]=c.style.blend?1.f:0.f;
            if(pass==Distortion)constants.alpha[3]=0;
            if(c.has_material){constants.material[3]=kich_r4_material::MaterialClass(c.material.material);
                constants.ambient[3]=c.material.emissive_gain;constants.hemi[3]=c.material.emissive_specular;}
            kich_r4_capture::Style style=c.style;
            if(c.has_material)kich_r4_material::Apply(c.material,style);
            if(pass==Shadow){style.depth=style.write=1;style.blend=style.clamp=0;style.scissor=false;style.min_z=0;style.max_z=1;}
            if(pass==ForwardAdd || pass==HUDAdd){style.blend=1;style.src=style.dst=2;style.write=0;}
            if(pass==Distortion){style.depth=1;style.write=0;style.blend=1;style.src=5;style.dst=6;}
            constants.fog_params[2]=(pass==Forward || pass==ForwardAdd) && style.fog_enabled?(style.blend && style.dst==2?2.f:1.f):0;
            KichR4ModelDrawDesc d={};d.struct_size=sizeof(d);d.surface.struct_size=sizeof(d.surface);KichR4StreamDrawDesc& state=d.surface.state;state.struct_size=sizeof(state);
            state.draw={sizeof(state.draw),pipelines[command_classes[index]][pass],mesh.vb,mesh.ib,cb,c.count,c.first,style.cull,style.depth};
            if((pass==Forward || pass==HUD) && (c.effect==1 || (c.has_material && c.material.semantic==kich_r4_material::EMISSIVE)))state.draw.pipeline=plain_pipeline[command_classes[index]];
            state.blend_enable=style.blend;state.source_blend=style.src;state.dest_blend=style.dst;state.clamp_texture=style.clamp;state.surface=style.surface;
            if((pass==ForwardAdd || pass==HUDAdd) && kich_r4_projector::Enabled(constants))kich_r4_projector::Sampler(state.surface);
            state.depth_write=style.write;state.scissor_enable=style.scissor?1:0;memcpy(state.scissor,style.rect,sizeof(state.scissor));state.min_depth=style.min_z;state.max_depth=style.max_z;
            const std::string& texture=c.has_material && !c.material.base.empty()?c.material.base:c.texture;d.surface.textures[0]=pass==Shadow && (c.has_material?kich_r4_material::AlphaFunction(c.material,true):kich_r4_material::AlphaFunction(c.style,true))==8?0:texture.empty()?white:l.Texture(s,texture);
            if(g_render_snapshot_active){if(texture.empty())RenderSnapshot_Texture(0,"$white");
                else if(d.surface.textures[0]==l.checker){char resolved[256];snprintf(resolved,sizeof(resolved),"CHECKER_FALLBACK:%s",texture.c_str());RenderSnapshot_Texture(0,resolved);}}
            if(maps)for(unsigned j=0;j<6;++j)d.surface.textures[j+2]=maps[j];
            if(pass==Distortion)d.surface.textures[1]=scene_color;
            const auto partition=mesh.basis.find(c.first%3);
            uint32_t good_first=0,good_count=0,bad_first=0,bad_count=0;
            const uint32_t basis_attributes=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
            const BasisPartition* split=pass==Geometry && c.has_material && kich_r4_material::Geometry(c.material) && !c.material.bump.empty() &&
                (c.available_attributes&basis_attributes)!=basis_attributes && partition!=mesh.basis.end() && partition->second.ib &&
                partition->second.Range(c.first,c.count,good_first,good_count,bad_first,bad_count) && good_count && bad_count?&partition->second:NULL;
            uint32_t attributes=c.available_attributes;
            if(split){attributes|=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
                state.draw.index_buffer=split->ib;state.draw.start_index=good_first;state.draw.index_count=good_count;}
            if(c.has_material && (pass==Geometry || pass==Shadow))kich_r4_material::Bind(s,l,c.material,attributes,constants,d.surface,pass==Shadow,false,c.object_light_valid && pass==Geometry?kich_r4_material::SHADER_SUN:0,"MODEL_MATERIAL_EXECUTION");
            if(c.has_material && (pass==HUD || pass==HUDAdd) && state.draw.pipeline!=plain_pipeline[command_classes[index]])BindHUDMaterial(s,l,c,constants,d);
            if(pass==Forward || pass==ForwardAdd || pass==HUD || pass==HUDAdd){const KichR4Handle lut=l.MaterialLut(s);if(!lut)return s.failure;kich_r4_material_lut::Bind(d.surface,lut);}
            if(c.style.emissive_gain>0){constants.ambient[3]=c.style.emissive_gain;constants.hemi[3]=c.style.emissive_specular;
                if(pass==Geometry)constants.material[0]=float(uint32_t(constants.material[0])|256u);}
            if(g_render_snapshot_active && c.has_material && pass==Geometry)SnapshotMaterialBasis(c,uint32_t(constants.material[0]));
            // Reuse unused model constants; static/tree layout and DLL ABI stay unchanged.
            constants.color_bias[3]=c.object_light_valid?1.f:0.f;constants.color_scale[3]=c.object_hemi;constants.sun_scale_bias[2]=c.object_sun;
            if(c.object_light_valid && pass==Geometry)constants.material[0]=float(uint32_t(constants.material[0])|kich_r4_material::SHADER_SUN);
            // Opaque HUD uses the R2 deferred source contract despite drawing
            // forward here: ROS supplies indirect exposure, current PCF owns
            // direct sun. Genuine forward materials retain their source policy.
            if(c.object_light_valid && (pass==Forward || (pass==HUD && !SourceOpaque(c))))for(unsigned axis=0;axis<3;++axis)constants.sun_color[axis]*=c.object_sun;
            if(g_render_snapshot_active){char id[64];snprintf(id,sizeof(id),"command_%u",static_cast<unsigned>(index));
                kich_r4_telemetry::U64("MODEL_LIGHTING",id,"valid",c.object_light_valid);kich_r4_telemetry::Float("MODEL_LIGHTING",id,"hemi_visibility",c.object_hemi);kich_r4_telemetry::Float("MODEL_LIGHTING",id,"sun_visibility",c.object_sun);
                kich_r4_telemetry::Text("MODEL_LIGHTING",id,"resource",c.resource.c_str());}
            if(g_render_snapshot_active && maps)for(unsigned j=0;j<6;++j){char name[48];snprintf(name,sizeof(name),"SELECTED_SHADOW_FACE_%u",j);RenderSnapshot_Texture(j+2,maps[j]?name:"UNBOUND");}
            if(l.Expect(s,"G3c model material constants",l.bridge.api.update_buffer(s.device,cb,&constants,sizeof(constants)))<0)return s.failure;
            d.pose_buffer=command_poses[index];if(l.Expect(s,"G3b native model DrawIndexed",api.draw(s.device,&d))<0)return s.failure;
            RecordNormalSubmission(c,pass,constants,d,attributes,state.draw.pipeline==plain_pipeline[command_classes[index]]);
            if(pass==HUD && kich_r4_record::Enabled()){
                ++hud_recorded_draws;if(c.object_light_valid){++hud_exposure_valid_draws;hud_hemi_min=std::min(hud_hemi_min,c.object_hemi);hud_hemi_max=std::max(hud_hemi_max,c.object_hemi);}
                if(d.material_textures[0] && d.material_textures[1] && (uint32_t(constants.material[0])&kich_r4_material::SHADER_BUMP))++hud_mapped_draws;
                if(state.draw.pipeline==plain_pipeline[command_classes[index]])++hud_unlit_draws;}
            if(kich_r4_record::Enabled())kich_r4_watch::Global().ObserveModel(c,unsigned(pass),constants,d.surface,split!=NULL,state.draw.pipeline==plain_pipeline[command_classes[index]]);
            if(pass==Shadow)if(kich_r4_shadow_record::Pass* recorded=kich_r4_shadow_record::Active())recorded->Submitted(kich_r4_shadow_record::Model,recorded->signatures?c.visual_id^kich_r4_shadow_record::Mix(c.lifecycle_serial):0,c.first,c.count,c.identity_measured && c.visual_id!=0);
            if(split){const bool mapped=(uint32_t(constants.material[0])&kich_r4_material::SHADER_BUMP)!=0;state.draw.start_index=bad_first;state.draw.index_count=bad_count;
                constants.material[0]=float(uint32_t(constants.material[0])&~kich_r4_material::SHADER_BUMP);d.surface.textures[1]=d.surface.textures[2]=0;
                if(g_render_snapshot_active){RenderSnapshot_Texture(1,"UNBOUND");RenderSnapshot_Texture(2,"UNBOUND");}
                if(l.Expect(s,"E1 geometric subset constants",l.bridge.api.update_buffer(s.device,cb,&constants,sizeof(constants)))<0 ||
                    l.Expect(s,"E1 geometric subset draw",api.draw(s.device,&d))<0)return s.failure;
                RecordNormalSubmission(c,pass,constants,d,c.available_attributes,false);
                if(kich_r4_record::Enabled())kich_r4_watch::Global().ObserveModel(c,unsigned(pass),constants,d.surface,true,state.draw.pipeline==plain_pipeline[command_classes[index]]);
                ++draw_calls[pass];++phase_draws;
                if(g_render_snapshot_active){char id[96];snprintf(id,sizeof(id),"frame_%u_command_%u",frame,static_cast<unsigned>(index));
                    kich_r4_telemetry::U64("MODEL_BASIS_PARTITION",id,"mapped_indices",mapped?good_count:0);
                    kich_r4_telemetry::U64("MODEL_BASIS_PARTITION",id,"valid_basis_indices",good_count);
                    kich_r4_telemetry::U64("MODEL_BASIS_PARTITION",id,"geometric_basis_indices",bad_count);}}
            ++draw_calls[pass];++phase_draws;
            if(g_render_snapshot_active && index<command_uses.size()){const uint32_t uses_in_pass=split?2:1;command_uses[index]+=uses_in_pass;if(c.mesh->skinned)pose_uses+=uses_in_pass;
                char id[96];snprintf(id,sizeof(id),"frame_%u_command_%u",frame,static_cast<unsigned>(index));kich_r4_telemetry::U64("MODEL_COMMAND",id,"successful_pass_uses",command_uses[index]);
                if(c.mesh->skinned && c.identity_measured && c.skeleton_id && c.pose_hash_valid)try{char pose_id[128];PoseID(c,pose_id,sizeof(pose_id));const uint64_t uses=(pose_use_counts[pose_id]+=uses_in_pass);kich_r4_telemetry::U64("MODEL_POSE",pose_id,"successful_draw_uses",uses);}
                    catch(...){model_telemetry_complete=false;RenderSnapshot_Text("TELEMETRY_LOSS","pose_use_counts","ALLOCATION_FAILED");}}
        }
        return 0;
    }
    void SnapshotSummary()const{if(!g_render_snapshot_active)return;
        RenderSnapshot_Text("MODEL_RENDERER","path","NATIVE_DX11_GPU_SKINNING");RenderSnapshot_Text("MODEL_RENDERER","selection_scope","PER_PASS_CONSERVATIVE_BOUNDS; NOT_VISIBLE_PIXELS");
        RenderSnapshot_U64("MODEL_RENDERER","commands",commands);RenderSnapshot_U64("MODEL_RENDERER","unique_cached_meshes",meshes.size());
        RenderSnapshot_U64("MODEL_RENDERER","mesh_uploads",mesh_uploads);RenderSnapshot_U64("MODEL_RENDERER","pose_uploads",pose_uploads);
        RenderSnapshot_U64("MODEL_RENDERER","gpu_vertex_bytes",vertex_bytes);RenderSnapshot_U64("MODEL_RENDERER","gpu_index_bytes",index_bytes);
        RenderSnapshot_U64("MODEL_RENDERER","partition_buffers",basis_buffers);RenderSnapshot_U64("MODEL_RENDERER","partition_bytes",basis_bytes);RenderSnapshot_U64("MODEL_RENDERER","partition_creations",basis_creations);
        RenderSnapshot_U64("MODEL_RENDERER","pose_logical_bytes",pose_logical_bytes);RenderSnapshot_U64("MODEL_RENDERER","pose_budget_bytes",KICH_R4_MODEL_POSE_BUDGET_BYTES);
        RenderSnapshot_U64("MODEL_RENDERER","pose_upload_bytes",pose_upload_bytes);RenderSnapshot_U64("MODEL_RENDERER","pose_allocated_bytes",AllocatedPoseBytes());
        RenderSnapshot_U64("MODEL_RENDERER","pose_command_logical_bytes",pose_command_logical_bytes);RenderSnapshot_U64("MODEL_RENDERER","pose_api_supplied_bytes",pose_api_bytes);
        RenderSnapshot_U64("MODEL_RENDERER","pose_map_range_bytes",pose_upload_bytes);RenderSnapshot_U64("MODEL_RENDERER","pose_map_calls",pose_uploads);RenderSnapshot_U64("MODEL_RENDERER","pose_reused_commands",pose_reuses);
        RenderSnapshot_Text("MODEL_RENDERER","pose_map_policy","D3D11_WRITE_DISCARD_ENTIRE_CLASS_BUFFER; PADDING_ZEROED; ONE_UPLOAD_PER_UNIQUE_FRAME_POSE");
        RenderSnapshot_U64("MODEL_RENDERER","cpu_expanded_model_vertex_bytes",0);RenderSnapshot_U64("MODEL_RENDERER","cpu_expanded_model_index_bytes",0);
        uint64_t draws=0;for(unsigned i=0;i<PassCount;++i)draws+=draw_calls[i];RenderSnapshot_U64("MODEL_RENDERER","draw_calls",draws);
        RenderSnapshot_U64("MODEL_RENDERER","identity_counts_complete",model_telemetry_complete?1:0);
        RenderSnapshot_U64("MODEL_RENDERER","unique_skeleton_instances",unique_skeletons);RenderSnapshot_U64("MODEL_RENDERER","unique_visual_parts",unique_parts);if(RenderSnapshot_Detailed())RenderSnapshot_U64("MODEL_RENDERER","unique_pose_contents",unique_poses);else RenderSnapshot_Text("MODEL_RENDERER","unique_pose_contents","NOT_MEASURED_COUNTERS_ONLY");
        RenderSnapshot_U64("MODEL_RENDERER","unknown_skeleton_identity_commands",unknown_skeleton_commands);RenderSnapshot_Text("MODEL_RENDERER","npc_count","NOT_MEASURED");
        if(RenderSnapshot_Detailed()){RenderSnapshot_U64("MODEL_RENDERER","duplicate_pose_uploads",repeated_pose_uploads);RenderSnapshot_U64("MODEL_RENDERER","max_uploads_per_pose",max_pose_upload_multiplicity);}
        else{RenderSnapshot_Text("MODEL_RENDERER","duplicate_pose_uploads","NOT_MEASURED_COUNTERS_ONLY");RenderSnapshot_Text("MODEL_RENDERER","max_uploads_per_pose","NOT_MEASURED_COUNTERS_ONLY");}RenderSnapshot_U64("MODEL_RENDERER","pose_pass_uses",pose_uses);
        RenderSnapshot_U64("MODEL_RENDERER","pose_capacity_peak_session_bytes",peak_pose_bytes);RenderSnapshot_U64("MODEL_RENDERER","capture_group_id",group_id);
        if(group_id)RenderSnapshot_U64("MODEL_RENDERER","pose_capacity_peak_group_bytes",group_peak_pose_bytes);else RenderSnapshot_Text("MODEL_RENDERER","pose_capacity_peak_group_bytes","NOT_APPLICABLE");
        RenderSnapshot_Text("MODEL_RENDERER","reuse_policy","SAME_FRAME_SKELETON_IDENTITY_AND_EXACT_POSE_LAYOUT_MODE; UNKNOWN_OR_STALE_IDENTITY_NEVER_SHARED");
        RenderSnapshot_Text("MODEL_RENDERER","identity_scope","ACTUAL_CKINEMATICS_PARENT_POINTER_WITHIN_FRAME; NOT_NPC_COUNT");
        RenderSnapshot_Text("MODEL_RENDERER","gpu_model_ms","NOT_INSTRUMENTED");
    }
};
#endif
