#ifndef KICH_R4_GEOMETRY_SESSION_H
#define KICH_R4_GEOMETRY_SESSION_H
#include "KichR4FrameSession.h"
#include "KichR4TestScene.h"

/* EXE-only plain state; the owning device retains all partially created
 * resources until the guarded destroy call, including after shader failure. */
struct KichR4GeometrySession {
    KichR4ResourceAPI resources;
    KichR4GeometryAPI api;
    KichR4Handle vs,ps,vb,ib,cb,pipeline;
    char shader_error[1024];
    uint32_t triangle_frames,cube_frames;
    static bool ValidResources(const KichR4ResourceAPI& a){return a.struct_size==sizeof(a) &&
        a.abi_version==KICH_R4_RESOURCE_ABI && a.pointer_size==sizeof(void*) && !a.flags &&
        a.create_device && a.destroy_device && a.create_texture && a.create_buffer && a.compile_shader &&
        a.get_info && a.get_binding && a.get_variable && a.release_resource && a.get_stats;}
    static bool ValidAPI(const KichR4GeometryAPI& a){return a.struct_size==sizeof(a) &&
        a.abi_version==KICH_R4_GEOMETRY_ABI && a.pointer_size==sizeof(void*) && !a.flags &&
        a.create_pipeline && a.update_buffer && a.draw_indexed && a.get_stats && a.destroy_device;}
    int32_t Expect(KichR4FrameSession& s,const char* step,int32_t hr){return s.Record(step,hr==0?0:hr<0?hr:KICH_R4_E_STATE);}
    int32_t Compile(KichR4FrameSession& s,uint32_t kind,const char* source,KichR4Handle* out){
        KichR4ShaderDesc d={};d.struct_size=sizeof(d);d.kind=kind;memcpy(d.entry,"main",5);
        memcpy(d.name,"kich_b41_geometry",18);
        const int32_t hr=resources.compile_shader(s.device,&d,source,static_cast<uint32_t>(strlen(source)),out,shader_error,sizeof(shader_error));
        return Expect(s,kind==KICH_R4_VERTEX_SHADER?"B4.1 compile vs_5_0":"B4.1 compile ps_5_0",hr);
    }
    int32_t Start(KichR4FrameSession& s){
        if(Compile(s,KICH_R4_VERTEX_SHADER,kich_r4_scene::VertexShader(),&vs)<0 ||
            Compile(s,KICH_R4_PIXEL_SHADER,kich_r4_scene::PixelShader(),&ps)<0)return s.failure;
        const kich_r4_scene::Mesh mesh=kich_r4_scene::BuildMesh();
        const kich_r4_scene::Controls controls=kich_r4_scene::Defaults();
        const kich_r4_scene::Constants initial=kich_r4_scene::ObjectConstants(controls,960,540,0);
        KichR4BufferDesc b={sizeof(b),KICH_R4_VERTEX_BUFFER,sizeof(mesh.vertices),sizeof(mesh.vertices[0])};
        if(Expect(s,"B4.1 vertex buffer",resources.create_buffer(s.device,&b,mesh.vertices,sizeof(mesh.vertices),&vb))<0)return s.failure;
        b.kind=KICH_R4_INDEX_BUFFER;b.byte_size=sizeof(mesh.indices);b.stride=sizeof(mesh.indices[0]);
        if(Expect(s,"B4.1 index buffer",resources.create_buffer(s.device,&b,mesh.indices,sizeof(mesh.indices),&ib))<0)return s.failure;
        b.kind=KICH_R4_CONSTANT_BUFFER;b.byte_size=sizeof(initial);b.stride=0;
        if(Expect(s,"B4.1 constant buffer",resources.create_buffer(s.device,&b,&initial,sizeof(initial),&cb))<0)return s.failure;
        KichR4PipelineDesc p={};p.struct_size=sizeof(p);p.vertex_stride=sizeof(kich_r4_scene::Vertex);
        p.element_count=2;p.vertex_shader=vs;p.pixel_shader=ps;
        p.elements[0].semantic=KICH_R4_POSITION;p.elements[0].format=KICH_R4_FLOAT3;
        p.elements[1].semantic=KICH_R4_COLOR;p.elements[1].format=KICH_R4_FLOAT4;p.elements[1].byte_offset=12;
        return Expect(s,"B4.1 input layout / pipeline states",api.create_pipeline(s.device,&p,&pipeline));
    }
    int32_t Draw(KichR4FrameSession& s,const kich_r4_scene::Controls& controls,uint32_t w,uint32_t h){
        if(!w || !h)return s.Record("B4.1 zero viewport",KICH_R4_E_ARGUMENT);
        for(unsigned object=0;object<(controls.mode==1?1u:2u);++object){
            const kich_r4_scene::Constants constants=kich_r4_scene::ObjectConstants(controls,w,h,object);
            if(Expect(s,"B4.1 update constants",api.update_buffer(s.device,cb,&constants,sizeof(constants)))<0)return s.failure;
            const KichR4DrawIndexedDesc draw={sizeof(draw),pipeline,vb,ib,cb,
                controls.mode==1?3u:36u,controls.mode==1?0u:3u,controls.cull,controls.depth};
            if(Expect(s,"B4.1 DrawIndexed",api.draw_indexed(s.device,&draw))<0)return s.failure;
        }
        if(controls.mode==1)++triangle_frames;else ++cube_frames;return KICH_R4_OK;
    }
    int32_t Stop(KichR4FrameSession& s,KichR4GeometryReport* report){
        if(!s.device)return 0;
        const int32_t hr=api.destroy_device(s.device,report,report?sizeof(*report):0);
        if(hr==0)s.device=0;
        return Expect(s,"B4.1 destroy device",hr);
    }
};
#endif
