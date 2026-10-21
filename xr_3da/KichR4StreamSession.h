#ifndef KICH_R4_STREAM_SESSION_H
#define KICH_R4_STREAM_SESSION_H
#include "KichR4CaptureData.h"
#include "KichR4LevelSession.h"
struct KichR4StreamSession {
    KichR4StreamAPI api;
    KichR4Handle vs,ps,pipeline,cb,vb,ib,white;
    uint32_t vertex_capacity,index_capacity,frames;
    uint64_t layer_draws[3];
    std::vector<kich_r4_capture::Vertex> vertices;
    std::vector<uint32_t> indices;
    KichR4StreamSession():vs(0),ps(0),pipeline(0),cb(0),vb(0),ib(0),white(0),vertex_capacity(0),index_capacity(0),frames(0){memset(&api,0,sizeof(api));memset(layer_draws,0,sizeof(layer_draws));}
    static bool ValidAPI(const KichR4StreamAPI& a){return a.struct_size==sizeof(a) && a.abi_version==KICH_R4_STREAM_ABI &&
        a.pointer_size==sizeof(void*) && !a.flags && a.create_pipeline && a.draw;}
    int32_t Compile(KichR4FrameSession& s,KichR4LevelSession& level,uint32_t kind,const char* source,KichR4Handle& out){
        KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=kind;memcpy(d.entry,"main",5);memcpy(d.name,"kich_b43b_stream",16);
        return level.Expect(s,"B4.3b compile stream shader",level.bridge.resources.compile_shader(s.device,&d,source,static_cast<uint32_t>(strlen(source)),&out,level.bridge.shader_error,sizeof(level.bridge.shader_error)));}
    int32_t Start(KichR4FrameSession& s,KichR4LevelSession& level){
        if(!ValidAPI(api))return s.Record("B4.3b stream ABI",KICH_R4_E_ARGUMENT);
        if(Compile(s,level,KICH_R4_VERTEX_SHADER,kich_r4_capture::VertexShader(),vs)<0 ||
            Compile(s,level,KICH_R4_PIXEL_SHADER,kich_r4_capture::PixelShader(),ps)<0)return s.failure;
        KichR4PipelineDesc p={};p.struct_size=sizeof(p);p.vertex_stride=sizeof(kich_r4_capture::Vertex);p.element_count=3;p.vertex_shader=vs;p.pixel_shader=ps;
        p.elements[0]={KICH_R4_POSITION,0,KICH_R4_FLOAT4,0};p.elements[1]={KICH_R4_TEXCOORD,0,KICH_R4_FLOAT2,16};p.elements[2]={KICH_R4_COLOR,0,KICH_R4_UNORM8X4,24};
        if(level.Expect(s,"B4.3b stream pipeline",api.create_pipeline(s.device,&p,&pipeline))<0)return s.failure;
        float constants[4]={0,8,0,0};const KichR4BufferDesc b={sizeof(b),KICH_R4_CONSTANT_BUFFER,sizeof(constants),0};
        if(level.Expect(s,"B4.3b alpha constants",level.bridge.resources.create_buffer(s.device,&b,constants,sizeof(constants),&cb))<0)return s.failure;
        std::vector<unsigned char> d(132,0);KichR4LevelSession::Put32(d,0,0x20534444);KichR4LevelSession::Put32(d,4,124);
        KichR4LevelSession::Put32(d,8,0x100f);KichR4LevelSession::Put32(d,12,1);KichR4LevelSession::Put32(d,16,1);KichR4LevelSession::Put32(d,20,4);
        KichR4LevelSession::Put32(d,76,32);KichR4LevelSession::Put32(d,80,0x41);KichR4LevelSession::Put32(d,88,32);
        KichR4LevelSession::Put32(d,92,0xff);KichR4LevelSession::Put32(d,96,0xff00);KichR4LevelSession::Put32(d,100,0xff0000);KichR4LevelSession::Put32(d,104,0xff000000);KichR4LevelSession::Put32(d,108,0x1000);
        for(unsigned i=128;i<132;++i){d[i]=255;}const KichR4DdsDesc desc={sizeof(desc),2048,0};
        return level.Expect(s,"B4.3b white texture",level.api.create_texture_dds(s.device,&desc,d.data(),static_cast<uint32_t>(d.size()),&white));
    }
    int32_t Capacity(KichR4FrameSession& s,KichR4LevelSession& level,uint32_t kind,uint32_t needed,uint32_t stride,uint32_t& capacity,KichR4Handle& handle){
        if(needed<=capacity)return 0;
        uint32_t count=256;while(count<needed)count*=2;
        if(uint64_t(count)*stride>32u*1024u*1024u)return s.Record("B4.3b upload buffer budget",KICH_R4_E_MEMORY);
        KichR4Handle next=0;const KichR4BufferDesc d={sizeof(d),kind,count*stride,stride};
        if(level.Expect(s,"B4.3b grow upload buffer",level.bridge.resources.create_buffer(s.device,&d,NULL,0,&next))<0)return s.failure;
        if(handle && level.Expect(s,"B4.3b release replaced buffer",level.bridge.resources.release_resource(s.device,handle))<0)return s.failure;
        handle=next;capacity=count;return 0;
    }
    int32_t Upload(KichR4FrameSession& s,KichR4LevelSession& level,const kich_r4_capture::Batch& batch){
        if(!batch.Ready())return s.Record("B4.3b incomplete CPU frame",KICH_R4_E_STATE);
        if(batch.commands.empty())return 0;
        if(Capacity(s,level,KICH_R4_VERTEX_BUFFER,static_cast<uint32_t>(batch.vertices.size()),sizeof(vertices[0]),vertex_capacity,vb)<0 ||
            Capacity(s,level,KICH_R4_INDEX_BUFFER,static_cast<uint32_t>(batch.indices.size()),4,index_capacity,ib)<0)return s.failure;
        vertices.resize(vertex_capacity);indices.resize(index_capacity);std::copy(batch.vertices.begin(),batch.vertices.end(),vertices.begin());std::copy(batch.indices.begin(),batch.indices.end(),indices.begin());
        if(level.Expect(s,"B4.3b upload posed vertices",level.bridge.api.update_buffer(s.device,vb,vertices.data(),static_cast<uint32_t>(vertices.size()*sizeof(vertices[0]))))<0 ||
            level.Expect(s,"B4.3b upload indices",level.bridge.api.update_buffer(s.device,ib,indices.data(),static_cast<uint32_t>(indices.size()*4)))<0)return s.failure;
        ++frames;return 0;
    }
    int32_t Draw(KichR4FrameSession& s,KichR4LevelSession& level,const kich_r4_capture::Batch& batch){
        if(batch.commands.empty())return Upload(s,level,batch);
        if(!pipeline && Start(s,level)<0)return s.failure;
        if(Upload(s,level,batch)<0)return s.failure;
        for(unsigned layer=0;layer<3;++layer)for(size_t i=0;i<batch.commands.size();++i){const kich_r4_capture::Command& c=batch.commands[i];if(c.layer!=layer)continue;
            const KichR4Handle texture=c.texture.empty()?white:level.Texture(s,c.texture);
            if(layer==2 && texture==level.checker)return s.Record("B4.3b HUD DDS unavailable",KICH_R4_E_UNSUPPORTED);
            float constants[4]={c.style.alpha_ref,static_cast<float>(c.style.alpha_func),0,0};
            if(level.Expect(s,"B4.3b alpha test",level.bridge.api.update_buffer(s.device,cb,constants,sizeof(constants)))<0)return s.failure;
            KichR4StreamDrawDesc d={};d.struct_size=sizeof(d);d.draw={sizeof(d.draw),pipeline,vb,ib,cb,c.count,c.first,c.style.cull,c.style.depth};
            d.texture=texture;d.blend_enable=c.style.blend;d.source_blend=c.style.src;d.dest_blend=c.style.dst;d.clamp_texture=c.style.clamp;
            d.scissor_enable=c.style.scissor?1:0;d.depth_write=c.style.write;memcpy(d.scissor,c.style.rect,sizeof(d.scissor));d.min_depth=c.style.min_z;d.max_depth=c.style.max_z;
            if(level.Expect(s,layer==0?"B4.3b draw model":layer==1?"B4.3b draw hands/weapon":"B4.3b draw HUD",api.draw(s.device,&d))<0)return s.failure;
            ++layer_draws[layer];
        }return 0;
    }
};
#endif
