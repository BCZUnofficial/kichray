#include "stdafx.h"
#include "KichR4Native.h"
#include "KichR4NativeCPU.h"
#include "KichR4Capture.h"
#include "KichR4CaptureData.h"
#include "ResourceManager.h"
#include "KichR4LevelOption.h"
#include "KichR4DetailData.h"
#include "KichR4MaterialRules.h"
#include "RenderSnapshot.h"
#include <map>
#include <string>
#include <chrono>

namespace {
bool selected=false,menu_pp=false,menu_overlay=false,distortion_enabled=true;
std::chrono::steady_clock::time_point queue_begin;
uint32_t states[256]={};
kich_r4_capture::Style style;
CTexture* textures[16]={};
uint32_t effect=0;
uint32_t source_element=UINT32_MAX,source_pass=0;
std::string active_shader,model_texture,model_secondary;
float Float(const unsigned char* p){float v;memcpy(&v,p,4);kich_r4_level::Check(kich_r4_level::Finite(v),"native nonfinite vertex");return v;}
uint32_t Word(const unsigned char* p){uint32_t v;memcpy(&v,p,4);return v;}
bool Has(const char* value,const char* part){return value && strstr(value,part)!=NULL;}
std::string TextureName(unsigned stage){if(stage>=16 || !textures[stage])return std::string();CTexture* t=textures[stage];t->bind(stage);
    const char* name=KichR4_NativeTextureName(t->pSurface);return name?name:(t->isUser()?std::string():std::string(t->cName.c_str()));}
}
bool KichR4_NativeActive(){return selected;}
bool KichR4_NativeInlineGeometry(void* visual){const kich_r4_capture::State* s=KichR4_CaptureState();if(!s)return false;const auto i=s->models.find(visual);return i!=s->models.end() && i->second.mesh.get()!=NULL;}
bool KichR4_NativeSelect(const char* params){if(selected)return true;char token[512];int read=0;while((read=KichR4_NextToken(params,token,sizeof(token)))!=0)if(read>0){
    if(!strcmp(token,"-kich_r4_game"))selected=true;else if(!strcmp(token,"-nodistort"))distortion_enabled=false;}return selected;}
bool KichR4_NativeDistortionEnabled(){return distortion_enabled;}
void KichR4_NativeSetState(uint32_t key,uint32_t value){if(key<256)states[key]=value;}
uint32_t KichR4_NativeGetState(uint32_t key){return key<256?states[key]:0;}
void KichR4_NativeMenuPP(bool enabled){if(menu_pp && !enabled)menu_overlay=true;menu_pp=enabled;}
void KichR4_NativeViewport(float a,float b){style.min_z=a;style.max_z=b;}
void KichR4_NativeScissor(const int* p){style.scissor=p!=NULL;if(p)memcpy(style.rect,p,sizeof(style.rect));}
bool KichR4_NativeStyle(kich_r4_capture::Style& out){out=style;out.blend=states[D3DRS_ALPHABLENDENABLE]!=0;out.src=states[D3DRS_SRCBLEND];out.dst=states[D3DRS_DESTBLEND];
    out.depth=states[D3DRS_ZENABLE]!=0;out.write=states[D3DRS_ZWRITEENABLE]!=0;out.alpha_func=states[D3DRS_ALPHATESTENABLE]?states[D3DRS_ALPHAFUNC]:8;
    out.alpha_ref=float(states[D3DRS_ALPHAREF]&255)/255.f;out.clamp=states[255]==D3DTADDRESS_CLAMP;
    out.cull=states[D3DRS_CULLMODE]==D3DCULL_CW?KICH_R4_CULL_FRONT:states[D3DRS_CULLMODE]==D3DCULL_CCW?KICH_R4_CULL_BACK:KICH_R4_CULL_NONE;return true;}
void KichR4_NativeElement(ShaderElement* e){KichR4_NativeElementPass(e,0);}
void KichR4_NativeElementSource(uint32_t element){source_element=element;}
void KichR4_NativeElementPass(ShaderElement* e,uint32_t pass){
    active_shader.clear();effect=0;source_element=UINT32_MAX;source_pass=pass;
    if(!e || pass>=e->passes.size())return;
    const SPass* selected_pass=e->passes[pass]._get();
    if(selected_pass && selected_pass->ps)active_shader=selected_pass->ps->cName.c_str();
    effect=kich_r4_material::Effect(active_shader.c_str(),e->flags.bDistort!=0,e->flags.bWmark!=0);
}
void KichR4_NativeTexture(uint32_t stage,CTexture* t){if(stage<16){textures[stage]=t;if(t)t->bind(stage);}}
void KichR4_NativeModelMaterial(kich_r4_capture::ModelInput& input){
    try{model_texture=TextureName(0);model_secondary=TextureName(1);
        input.shader=active_shader.c_str();input.texture=model_texture.c_str();input.secondary=model_secondary.c_str();
        input.effect=effect;input.shader_element=source_element;input.shader_pass=source_pass;
    }catch(...){KichR4_CaptureFail("G3b model material snapshot failed");}
}
double KichR4_NativeQueueTime(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-queue_begin).count();}
void KichR4_NativeBegin(uint32_t width,uint32_t height,uint32_t frame){menu_pp=menu_overlay=false;queue_begin=std::chrono::steady_clock::now();memset(states,0,sizeof(states));style=kich_r4_capture::Style();states[D3DRS_ZENABLE]=states[D3DRS_ZWRITEENABLE]=1;
    KichR4_DetailFrame().ClearFrame(frame);
    states[D3DRS_CULLMODE]=D3DCULL_CCW;states[D3DRS_SRCBLEND]=D3DBLEND_SRCALPHA;states[D3DRS_DESTBLEND]=D3DBLEND_INVSRCALPHA;states[D3DRS_ALPHAFUNC]=D3DCMP_GREATER;
    states[D3DRS_COLORWRITEENABLE]=15;states[255]=D3DTADDRESS_WRAP;effect=0;source_element=UINT32_MAX;source_pass=0;active_shader.clear();model_texture.clear();model_secondary.clear();memset(textures,0,sizeof(textures));
    KichR4_CaptureBeginFrame(width,height,frame);kich_r4_capture::State* s=KichR4_CaptureState();if(s){s->native_models=true;s->frame.renderer_seen=true;s->frame.ui_seen=true;}
}
void KichR4_NativeFillRect(int32_t left,int32_t top,int32_t right,int32_t bottom,uint32_t color){
    using namespace kich_r4_capture;State* s=KichR4_CaptureState();if(!s || left>=right || top>=bottom)return;
    try{Batch& b=s->frame;b.Reserve(4,6,"native fill rect",2);const uint32_t base=static_cast<uint32_t>(b.vertices.size()),first=static_cast<uint32_t>(b.indices.size());
        const float x[4]={float(left),float(left),float(right),float(right)},y[4]={float(top),float(bottom),float(top),float(bottom)};
        for(unsigned i=0;i<4;++i)b.vertices.push_back(b.Screen(x[i]-.5f,y[i]-.5f,0,1,0,0,color));
        const uint32_t indices[6]={0,1,2,2,1,3};for(unsigned i=0;i<6;++i)b.indices.push_back(base+indices[i]);
        Style draw;draw.blend=draw.depth=draw.write=0;draw.cull=KICH_R4_CULL_NONE;draw.alpha_func=8;
        Command command(first,6,2,std::string(),draw);command.console=g_render_snapshot_console;command.effect=menu_pp?7:menu_overlay?8:0;
        SnapshotCommand(command,b.vertices,b.indices,"UI_RECT",NULL,0,false);b.commands.push_back(command);++b.ui;
    }catch(const std::exception& e){s->frame.Fail(e.what());}
}
void KichR4_NativeDraw(void* declaration,void* vb,void* ib,uint32_t stride,uint32_t topology,uint32_t base,uint32_t start,uint32_t vertex_count,uint32_t first,uint32_t primitives,bool indexed){
    using namespace kich_r4_capture;State* state=KichR4_CaptureState();if(!state || !primitives)return;
    try{uint32_t vn=0,in=0,dn=0;const unsigned char* vertices=KichR4_CPUBuffer(vb,vn);const unsigned char* indices=indexed?KichR4_CPUBuffer(ib,in):NULL;
        const D3DVERTEXELEMENT9* decl=KichR4_CPUElements(declaration,dn);Check(vertices && decl && stride && dn,"native draw resources");
        Check(!indexed || indices,"native draw indices");const D3DVERTEXELEMENT9 *position=NULL,*uv=NULL,*color=NULL,*normal=NULL,*tangent=NULL,*binormal=NULL;
        for(uint32_t i=0;i<dn && decl[i].Stream!=255;++i){const D3DVERTEXELEMENT9& e=decl[i];if(e.UsageIndex || e.Stream)continue;
            if(e.Usage==D3DDECLUSAGE_POSITION || e.Usage==D3DDECLUSAGE_POSITIONT)position=&e;
            else if(e.Usage==D3DDECLUSAGE_TEXCOORD)uv=&e;else if(e.Usage==D3DDECLUSAGE_COLOR)color=&e;else if(e.Usage==D3DDECLUSAGE_NORMAL)normal=&e;else if(e.Usage==6)tangent=&e;else if(e.Usage==7)binormal=&e;}
        Check(position && (position->Type==D3DDECLTYPE_FLOAT3 || position->Type==D3DDECLTYPE_FLOAT4),"native position declaration");
        const bool screen=position->Usage==D3DDECLUSAGE_POSITIONT;
        const uint32_t n=topology==D3DPT_TRIANGLELIST?primitives*3:topology==D3DPT_TRIANGLESTRIP || topology==D3DPT_TRIANGLEFAN?primitives+2:
            topology==D3DPT_LINELIST?primitives*2:topology==D3DPT_LINESTRIP?primitives+1:topology==D3DPT_POINTLIST?primitives:0;
        Check(n && n<4u*1024u*1024u,"native primitive count");Check(!indexed || uint64_t(first+n)*2<=in,"native index range");
        if(Has(active_shader.c_str(),"detail")){kich_r4_detail::Frame& details=KichR4_DetailFrame();++details.legacy_draws;details.legacy_vertices+=indexed?vertex_count:n;details.legacy_indices+=indexed?n:0;}
        Style draw;KichR4_NativeStyle(draw);if(screen){draw.depth=draw.write=0;draw.cull=KICH_R4_CULL_NONE;draw.min_z=0;draw.max_z=1;draw.clamp=1;}
        const uint32_t layer=screen?2:draw.max_z<.1f?1:0;
        const std::string texture=(uv || effect==2 || effect==3)?TextureName(0):std::string();const std::string secondary=(effect==2 || effect==3)?TextureName(1):std::string();
        Batch& batch=state->frame;batch.Reserve(indexed && vertex_count?std::min(n,vertex_count):n,topology>=D3DPT_TRIANGLELIST?primitives*3:primitives*6,
            (active_shader+" / "+texture).c_str(),layer);const uint32_t vbase=static_cast<uint32_t>(batch.vertices.size());
        Matrix world,vp;memcpy(world.m,RCache.get_xform_world().m,64);Fmatrix native_vp;native_vp.mul(RCache.get_xform_project(),RCache.get_xform_view());memcpy(vp.m,native_vp.m,64);
        const Matrix mvp=kich_r4_scene::Multiply(world,vp),nm=kich_r4_lighting::NormalMatrix(world);
        std::vector<uint32_t> references(n);std::map<uint32_t,uint32_t> reused;
        for(uint32_t i=0;i<n;++i){uint32_t at=start+i;if(indexed)at=base+uint32_t(indices[(first+i)*2])+(uint32_t(indices[(first+i)*2+1])<<8);
            Check((uint64_t(at)+1)*stride<=vn,"native vertex range");
            if(topology>=D3DPT_TRIANGLELIST){std::map<uint32_t,uint32_t>::const_iterator found=reused.find(at);if(found!=reused.end()){references[i]=found->second;continue;}references[i]=static_cast<uint32_t>(batch.vertices.size());reused[at]=references[i];}const unsigned char* p=vertices+at*stride;
            Check(position->Offset+12u<=stride,"native position offset");float xyz[4]={Float(p+position->Offset),Float(p+position->Offset+4),Float(p+position->Offset+8),1};
            float u=0,v=0;if(uv){Check(uv->Offset+kich_r4_level::TypeBytes(uv->Type)<=stride,"native UV declaration");const unsigned char* tc=p+uv->Offset;
                if(uv->Type==1 || uv->Type==2 || uv->Type==3){u=Float(tc);v=Float(tc+4);}
                else if(uv->Type==6 || uv->Type==7){const float quant=uv->Type==7?2048.f:1024.f;u=kich_r4_level::Short(tc)/quant;v=kich_r4_level::Short(tc+2)/quant;
                    if(uv->Type==6 && tangent && binormal && tangent->Type==4 && binormal->Type==4){Check(tangent->Offset+4u<=stride && binormal->Offset+4u<=stride,"native packed UV fractions");u+=p[tangent->Offset+3]/(255.f*quant);v+=p[binormal->Offset+3]/(255.f*quant);}}
                else if(uv->Type==15 || uv->Type==16){u=kich_r4_level::Half(tc);v=kich_r4_level::Half(tc+2);}else Check(false,"native UV declaration");}
            if(color)Check(color->Offset+4u<=stride,"native color offset");
            const uint32_t argb=color && color->Type==D3DDECLTYPE_D3DCOLOR?Word(p+color->Offset):0xffffffffu;
            Vertex out={};if(screen){Check(position->Type==D3DDECLTYPE_FLOAT4 && position->Offset+16u<=stride,"native screen RHW");out=batch.Screen(xyz[0],xyz[1],xyz[2],Float(p+position->Offset+12),u,v,argb);}
            else{Transform(xyz,mvp,out.p);float w[4];Transform(xyz,world,w);memcpy(out.world,w,12);out.uv[0]=u;out.uv[1]=v;out.color=RGBA(argb);out.normal[3]=1;
                if(normal && (normal->Type==D3DDECLTYPE_FLOAT3 || normal->Type==D3DDECLTYPE_FLOAT4 || normal->Type==D3DDECLTYPE_D3DCOLOR)){Check(normal->Offset+(normal->Type==D3DDECLTYPE_D3DCOLOR?4u:12u)<=stride,"native normal offset");const unsigned char* np=p+normal->Offset;const kich_r4_scene::Vec3 vector=normal->Type==D3DDECLTYPE_D3DCOLOR?kich_r4_scene::Vec3{np[2]*(2.f/255)-1,np[1]*(2.f/255)-1,np[0]*(2.f/255)-1}:kich_r4_scene::Vec3{Float(np),Float(np+4),Float(np+8)};if(normal->Type==D3DDECLTYPE_D3DCOLOR)out.normal[3]=np[3]/255.f;const kich_r4_scene::Vec3 result=kich_r4_scene::Normalize(kich_r4_lighting::Direction(vector,nm));out.normal[0]=result.x;out.normal[1]=result.y;out.normal[2]=result.z;}
                for(unsigned j=0;j<4;++j)Check(Finite(out.p[j]),"native clip position");}
            if(effect==2 && uv && !screen && uv->Type==D3DDECLTYPE_FLOAT3){out.normal[0]=u;out.normal[1]=v;out.normal[2]=Float(p+uv->Offset+8);}
            if(effect==3 && !screen){const float t=Device.fTimeGlobal;const float d0x=((argb>>16)&255)*(2.f/255)-1,d0y=((argb>>8)&255)*(2.f/255)-1,d1x=((argb>>24)&255)*(2.f/255)-1,d1y=(argb&255)*(2.f/255)-1;
                out.uv[0]=xyz[0]*.7f+d0x*t*.01f;out.uv[1]=xyz[2]*.7f+d0y*t*.01f;out.normal[0]=xyz[0]*2.8f+d1x*t*.005f;out.normal[1]=xyz[2]*2.8f+d1y*t*.005f;
                for(uint32_t j=0;j<dn && decl[j].Stream!=255;++j)if(decl[j].Usage==D3DDECLUSAGE_COLOR && decl[j].UsageIndex==1 && decl[j].Type==D3DDECLTYPE_D3DCOLOR)out.color=RGBA(Word(p+decl[j].Offset));
                const uint32_t opacity=static_cast<uint32_t>(((out.color>>24)&255)*powf(std::max(0.f,std::min(1.f,xyz[1])),25.f));out.color=(out.color&0xffffffu)|(opacity<<24);}
            batch.vertices.push_back(out);}
        const uint32_t ibase=static_cast<uint32_t>(batch.indices.size());
        if(topology==D3DPT_TRIANGLELIST){for(uint32_t i=0;i<n;++i)batch.indices.push_back(references[i]);}
        else if(topology==D3DPT_TRIANGLESTRIP || topology==D3DPT_TRIANGLEFAN){for(uint32_t i=0;i<primitives;++i){batch.indices.push_back(references[topology==D3DPT_TRIANGLEFAN?0:i+(i&1)]);batch.indices.push_back(references[topology==D3DPT_TRIANGLEFAN?i+1:i+1-(i&1)]);batch.indices.push_back(references[i+2]);}}
        else{const std::vector<Vertex> originals(batch.vertices.begin()+vbase,batch.vertices.end());batch.vertices.resize(vbase);
            for(uint32_t i=0;i<primitives;++i){const uint32_t a=topology==D3DPT_LINELIST?i*2:i,b=topology==D3DPT_POINTLIST?a:a+1;Vertex va=originals[a],vbv=originals[b];
                float dx=vbv.p[0]/vbv.p[3]-va.p[0]/va.p[3],dy=vbv.p[1]/vbv.p[3]-va.p[1]/va.p[3];const float length=sqrtf(dx*dx+dy*dy);if(length<1.e-8f){dx=1;dy=0;}else{dx/=length;dy/=length;}
                const float x=-dy/batch.width,y=dx/batch.height;const uint32_t q=static_cast<uint32_t>(batch.vertices.size());
                Vertex quad[4]={va,va,vbv,vbv};for(unsigned j=0;j<4;++j){const float sign=j&1?1.f:-1.f;quad[j].p[0]+=sign*x*quad[j].p[3];quad[j].p[1]+=sign*y*quad[j].p[3];batch.vertices.push_back(quad[j]);}
                const uint32_t ix[]={0,1,2,2,1,3};for(unsigned j=0;j<6;++j)batch.indices.push_back(q+ix[j]);}}
        Command cmd={ibase,static_cast<uint32_t>(batch.indices.size())-ibase,layer,texture,draw};cmd.effect=screen && menu_pp?7:screen && menu_overlay?8:effect;cmd.secondary=secondary;
        cmd.shader=active_shader;cmd.shader_element=source_element;cmd.shader_pass=source_pass;
        if(g_render_snapshot_active){cmd.resource=texture;cmd.skin="CPU_CAPTURE_STREAM";}
        cmd.console=g_render_snapshot_console;
        if(effect==2){cmd.style.depth=1;cmd.style.write=0;cmd.style.cull=KICH_R4_CULL_NONE;}
        SnapshotCommand(cmd,batch.vertices,batch.indices,screen?"UI_GEOMETRY":"UNKNOWN",NULL,0,uv!=NULL);
        batch.commands.push_back(cmd);if(layer==2)++batch.ui;else if(layer==1)++batch.hands;else ++batch.models;
        (void)vertex_count;
    }catch(const std::exception& e){state->frame.Fail(e.what());}
}
