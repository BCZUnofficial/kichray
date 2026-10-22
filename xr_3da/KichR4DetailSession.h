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
    KichR4DetailSession():source(NULL),instance_buffer(0),vs(0),generation(UINT64_MAX),vertex_bytes(0),index_bytes(0),capacity(0),uploaded_frame(UINT32_MAX),draw_calls(0),shadow_draw_calls(0){memset(&api,0,sizeof(api));pipelines[0]=pipelines[1]=0;}
    int32_t Upload(KichR4FrameSession& s,KichR4LevelSession& l,uint32_t frame){
        draw_calls=shadow_draw_calls=0;
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
        uploaded_frame=frame;return 0;
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
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& l,KichR4Handle cb,const kich_r4_lighting::Constants& base,bool shadow,uint32_t frame){
        if(!source || uploaded_frame!=frame || source->engine_frame!=frame || source->instances.empty())return 0;
        for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];
            if(g.mesh>=meshes.size() || g.variant>2 || g.first>source->instances.size() || g.count>source->instances.size()-g.first)return s.Record("G3a detail group range",KICH_R4_E_ARGUMENT);
            const Mesh& mesh=meshes[g.mesh];kich_r4_lighting::Constants c=base;
            if(g_render_snapshot_active){const kich_r4_detail::Mesh& named=source->meshes[g.mesh];
                RenderSnapshot_Context("DETAIL",named.name.c_str(),named.shader.c_str());RenderSnapshot_Texture(0,named.texture.c_str());RenderSnapshot_Skin("NONE_INSTANCED_DETAIL");}
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
            if(shadow)++shadow_draw_calls;else ++draw_calls;
        }return 0;
    }
};
#endif
