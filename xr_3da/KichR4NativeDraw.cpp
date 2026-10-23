#include "stdafx.h"
#include "RenderSnapshotMaterial.h"
#include "RenderSnapshotMaterialData.h"
#include "RenderSnapshotGeometryData.h"
#include "KichR4Native.h"
#include "KichR4NativeCPU.h"
#include "KichR4Capture.h"
#include "KichR4CaptureData.h"
#include "ResourceManager.h"
#include "KichR4LevelOption.h"
#include "KichR4DetailData.h"
#include "KichR4MaterialRules.h"
#include "KichR4OpticalRules.h"
#include "KichR4MaterialServices.h"
#include "RenderSnapshot.h"
#include <map>
#include <string>
#include <chrono>

namespace {
bool selected=false,menu_pp=false,menu_overlay=false,distortion_enabled=true;
bool celestial_source=false;
bool lens_effect=false,object_light_valid=false;float object_hemi=1,object_sun=1;
std::chrono::steady_clock::time_point queue_begin;
uint32_t states[256]={};
kich_r4_capture::Style style;
CTexture* textures[16]={};
uint32_t effect=0;
uint32_t source_element=UINT32_MAX,source_pass=0;
std::string active_shader,model_texture,model_secondary;
const kich_r4_material::Descriptor* active_material=NULL;
float Float(const unsigned char* p){float v;memcpy(&v,p,4);kich_r4_level::Check(kich_r4_level::Finite(v),"native nonfinite vertex");return v;}
uint32_t Word(const unsigned char* p){uint32_t v;memcpy(&v,p,4);return v;}
bool Has(const char* value,const char* part){return value && strstr(value,part)!=NULL;}
std::string TextureName(unsigned stage){if(stage>=16 || !textures[stage])return std::string();CTexture* t=textures[stage];t->bind(stage);
    const char* name=KichR4_NativeTextureName(t->pSurface);return name?name:(t->isUser()?std::string():std::string(t->cName.c_str()));}
}
namespace {
snapshot_material::Registry& MaterialRegistry(){static snapshot_material::Registry* registry=new snapshot_material::Registry;return *registry;}
uint64_t material_metadata_errors=0;
void MaterialError(){++material_metadata_errors;if(g_render_snapshot_active)RenderSnapshot_U64("MATERIAL_REGISTRY","metadata_errors",material_metadata_errors);}
}
void RenderSnapshot_RegisterMaterialElement(const void* element,uint32_t index,const char* shader,const char* texture_list,const char* origin,const char* bump,const char* detail,uint32_t usage,uint64_t class_id){
    try{KichR4_NativeRegisterMaterial(element,index,shader,texture_list,origin,bump,detail,usage,class_id);
        snapshot_material::Source s;s.element=index;s.usage=usage;s.class_id=class_id;s.shader=snapshot_material::Text(shader);s.textures=snapshot_material::Text(texture_list,1024);s.origin=snapshot_material::Text(origin);s.bump=snapshot_material::Text(bump);s.detail=snapshot_material::Text(detail);MaterialRegistry().AddSource(element,s);}catch(...){MaterialError();}}
void RenderSnapshot_ForgetMaterialElement(const void* element){try{KichR4_NativeForgetMaterial(element);MaterialRegistry().sources.erase(element);}catch(...){MaterialError();}}
void RenderSnapshot_CompilerBegin(const void* compiler){try{MaterialRegistry().Begin(compiler);}catch(...){MaterialError();}}
void RenderSnapshot_CompilerSampler(const void* compiler,const char* semantic,const char* texture,uint32_t slot,uint32_t address,uint32_t min_filter,uint32_t mip_filter,uint32_t mag_filter){
    try{snapshot_material::Sampler s;s.semantic=snapshot_material::Text(semantic);s.texture=snapshot_material::Text(texture);s.slot=slot;s.address=address;s.min_filter=min_filter;s.mip_filter=mip_filter;s.mag_filter=mag_filter;MaterialRegistry().AddSampler(compiler,s);}catch(...){MaterialError();}}
void RenderSnapshot_CompilerEnd(const void* compiler,const void* pass){try{MaterialRegistry().End(compiler,pass);}catch(...){MaterialError();}}
void RenderSnapshot_ForgetMaterialPass(const void* pass){try{MaterialRegistry().passes.erase(pass);}catch(...){MaterialError();}}
void RenderSnapshot_ShaderIncludesBegin(){try{MaterialRegistry().include_manifest.clear();MaterialRegistry().source_excerpts.clear();}catch(...){MaterialError();}}
void RenderSnapshot_ShaderInclude(const char* path,const void* bytes,uint32_t size){
    try{snapshot_material::Registry& r=MaterialRegistry();const std::string name=snapshot_material::Text(path);const std::string entry=name+"="+std::to_string(snapshot_material::Hash(bytes,bytes?size:0))+";";
        if(r.include_manifest.size()+entry.size()<4096)r.include_manifest+=entry;else if(r.include_manifest.size()<4128)r.include_manifest+="TRUNCATED;";
        // Literal source evidence, deliberately not a parser or an assertion that a branch executed.
        const char* data=static_cast<const char*>(bytes);size_t at=0;unsigned line=1;while(data && at<size && at<32768 && r.source_excerpts.size()<4096){size_t end=at;while(end<size && data[end]!='\n')++end;
            const std::string text(data+at,std::min<size_t>(end-at,240));if(text.find("=")!=std::string::npos && (text.find(".normal")!=std::string::npos || text.find(".gloss")!=std::string::npos || text.find(".height")!=std::string::npos))
                r.source_excerpts+=name+":"+std::to_string(line)+":"+text+";";
            at=end+1;++line;}
    }catch(...){MaterialError();}}
void RenderSnapshot_RegisterShaderVariant(const char* name,const char* profile,const void* source,uint32_t source_bytes,const void* bytecode,uint32_t bytecode_bytes,const char* const* names,const char* const* values,uint32_t count){
    try{snapshot_material::Variant v;v.name=snapshot_material::Text(name);v.profile=snapshot_material::Text(profile);v.source_bytes=source?source_bytes:0;v.bytecode_bytes=bytecode?bytecode_bytes:0;
        v.source_hash=snapshot_material::Hash(source,v.source_bytes);v.bytecode_hash=snapshot_material::Hash(bytecode,v.bytecode_bytes);v.defines_truncated=count>128;
        for(uint32_t i=0;i<count && i<128;++i){const std::string definition=snapshot_material::Text(names?names[i]:NULL)+"="+snapshot_material::Text(values?values[i]:NULL)+";";if(v.defines.size()+definition.size()>4096){v.defines_truncated=true;break;}v.defines+=definition;}
        v.include_manifest=MaterialRegistry().include_manifest;v.source_excerpts=MaterialRegistry().source_excerpts;MaterialRegistry().AddVariant(v);}catch(...){MaterialError();}}
void RenderSnapshot_MaterialSelection(ShaderElement* element,uint32_t pass,uint32_t selected_element,uint32_t available_mask,const char* reason){
    if(!RenderSnapshot_Detailed())return;
    try{snapshot_material::View v;v.element=element;v.pass_index=pass;v.selected_element=selected_element;v.available_mask=available_mask;v.native=KichR4_NativeActive();v.reason=reason;
        if(element){v.flags=(element->flags.bDistort?1u:0u)|(element->flags.bEmissive?2u:0u)|(element->flags.bWmark?4u:0u)|(element->flags.bStrictB2F?8u:0u)|(element->flags.iPriority<<8);
            if(pass<element->passes.size()){const SPass* p=element->passes[pass]._get();v.pass=p;
                if(p){v.vs=p->vs?p->vs->cName.c_str():"NONE";v.ps=p->ps?p->ps->cName.c_str():"NONE";
                    if(p->state){const uint32_t keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE};
                        for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);++k){const uint32_t value=p->state->state_code.get_RS(keys[k],UINT32_MAX);v.render_states+=std::to_string(keys[k])+"="+(value==UINT32_MAX?std::string("INHERITED"):std::to_string(value))+";";}
                        v.alpha_test=p->state->state_code.get_RS(D3DRS_ALPHATESTENABLE,UINT32_MAX);v.blend=p->state->state_code.get_RS(D3DRS_ALPHABLENDENABLE,UINT32_MAX);}
                    if(p->T)for(size_t i=0;i<p->T->size() && i<32;++i){const CTexture* t=(*p->T)[i].second._get();v.textures.push_back(std::make_pair((*p->T)[i].first,t?snapshot_material::Text(t->cName.c_str()):std::string("NONE")));}}}}
        const std::string id=snapshot_material::Observe(MaterialRegistry(),v);
        if(element && pass<element->passes.size()){const SPass* p=element->passes[pass]._get();if(p && p->T)for(size_t i=0;i<p->T->size() && i<32;++i){
            const CTexture* t=(*p->T)[i].second._get();char key[64];snprintf(key,sizeof(key),"pass_texture_slot_%u_load",(*p->T)[i].first);
            snapshot_material::Field("MATERIAL",id,key,t?(t->pSurface?"SOURCE_SURFACE_PRESENT":"SOURCE_SURFACE_ABSENT_NOT_LOAD_FAILURE_PROOF"):"NOT_ASSIGNED");}}
        RenderSnapshot_U64("MATERIAL_REGISTRY","metadata_errors",material_metadata_errors);
    }catch(...){RenderSnapshot_MaterialRoute("METADATA_UNAVAILABLE");MaterialError();}}
void RenderSnapshot_MaterialElement(ShaderElement* element,uint32_t pass){RenderSnapshot_MaterialSelection(element,pass,UINT32_MAX,UINT32_MAX,"ELEMENT_BOUND; REQUEST_SITE_NOT_INSTRUMENTED");}

bool KichR4_NativeActive(){return selected;}
bool KichR4_NativeInlineGeometry(void* visual){const kich_r4_capture::State* s=KichR4_CaptureState();if(!s)return false;const auto i=s->models.find(visual);return i!=s->models.end() && i->second.mesh.get()!=NULL;}
bool KichR4_NativeSelect(const char* params){if(selected)return true;char token[512];int read=0;while((read=KichR4_NextToken(params,token,sizeof(token)))!=0)if(read>0){
    if(!strcmp(token,"-kich_r4_game"))selected=true;else if(!strcmp(token,"-nodistort"))distortion_enabled=false;}return selected;}
bool KichR4_NativeDistortionEnabled(){return distortion_enabled;}
void KichR4_NativeSetState(uint32_t key,uint32_t value){if(key<256)states[key]=value;}
uint32_t KichR4_NativeGetState(uint32_t key){return key<256?states[key]:0;}
void KichR4_NativeMenuPP(bool enabled){if(menu_pp && !enabled)menu_overlay=true;menu_pp=enabled;}
void KichR4_NativeCelestialSource(bool enabled){celestial_source=enabled;}
void KichR4_NativeLensEffect(bool enabled){lens_effect=enabled;}
void KichR4_NativeObjectLight(bool valid,float hemi,float sun){object_light_valid=valid;object_hemi=hemi;object_sun=sun;}
void KichR4_NativeViewport(float a,float b){style.min_z=a;style.max_z=b;}
void KichR4_NativeScissor(const int* p){style.scissor=p!=NULL;if(p)memcpy(style.rect,p,sizeof(style.rect));}
bool KichR4_NativeStyle(kich_r4_capture::Style& out){out=style;out.blend=states[D3DRS_ALPHABLENDENABLE]!=0;out.src=states[D3DRS_SRCBLEND];out.dst=states[D3DRS_DESTBLEND];
    out.depth=states[D3DRS_ZENABLE]!=0;out.write=states[D3DRS_ZWRITEENABLE]!=0;out.alpha_func=states[D3DRS_ALPHATESTENABLE]?states[D3DRS_ALPHAFUNC]:8;
    out.alpha_ref=float(states[D3DRS_ALPHAREF]&255)/255.f;out.clamp=states[255]==D3DTADDRESS_CLAMP;
    out.cull=states[D3DRS_CULLMODE]==D3DCULL_CW?KICH_R4_CULL_FRONT:states[D3DRS_CULLMODE]==D3DCULL_CCW?KICH_R4_CULL_BACK:KICH_R4_CULL_NONE;return true;}
void KichR4_NativeElement(ShaderElement* e){KichR4_NativeElementPass(e,0);}
void KichR4_NativeElementSource(uint32_t element){source_element=element;}
void KichR4_NativeElementPass(ShaderElement* e,uint32_t pass){
    active_shader.clear();active_material=NULL;effect=0;source_element=UINT32_MAX;source_pass=pass;
    if(!e || pass>=e->passes.size())return;
    active_material=KichR4_NativeFindMaterial(e,pass);
    const SPass* selected_pass=e->passes[pass]._get();
    if(selected_pass && selected_pass->ps)active_shader=selected_pass->ps->cName.c_str();
    effect=kich_r4_material::Effect(active_shader.c_str(),e->flags.bDistort!=0,e->flags.bWmark!=0);
}
void KichR4_NativeTexture(uint32_t stage,CTexture* t){if(stage<16){textures[stage]=t;if(t)t->bind(stage);}}
void KichR4_NativeModelMaterial(kich_r4_capture::ModelInput& input){
    input.object_light_valid=object_light_valid;input.object_hemi=object_hemi;input.object_sun=object_sun;
    try{model_texture=TextureName(0);model_secondary=TextureName(1);
        input.shader=active_shader.c_str();input.texture=model_texture.c_str();input.secondary=model_secondary.c_str();
        input.effect=effect;input.shader_element=source_element;input.shader_pass=source_pass;
        input.material=active_material;if(active_material)kich_r4_material::Apply(*active_material,input.style);
    }catch(...){KichR4_CaptureFail("G3c model material snapshot failed");}
}
double KichR4_NativeQueueTime(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-queue_begin).count();}
void KichR4_NativeBegin(uint32_t width,uint32_t height,uint32_t frame){celestial_source=lens_effect=object_light_valid=false;object_hemi=object_sun=1;menu_pp=menu_overlay=false;queue_begin=std::chrono::steady_clock::now();memset(states,0,sizeof(states));style=kich_r4_capture::Style();states[D3DRS_ZENABLE]=states[D3DRS_ZWRITEENABLE]=1;
    KichR4_DetailFrame().ClearFrame(frame);active_material=NULL;
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
        Command command(first,6,2,std::string(),draw);command.semantic=CAPTURE_UI;command.captured_vertices=4;command.console=g_render_snapshot_console;command.effect=menu_pp?7:menu_overlay?8:0;
        if(RenderSnapshot_Detailed()){RenderSnapshot_GeometrySource("NATIVE_UI_RECT");RenderSnapshot_MaterialRoute("NATIVE_UI_RECT_NO_SOURCE_PASS");}
        SnapshotCommand(command,b.vertices,b.indices,"UI_RECT",NULL,0,false);b.commands.push_back(command);++b.ui;
    }catch(const std::exception& e){s->frame.Fail(e.what());}
}
namespace {
void GeometryVector(const std::string& id,const char* key,const float* values,unsigned n){char text[768];size_t at=0;for(unsigned i=0;i<n && at<sizeof(text);++i){const int wrote=snprintf(text+at,sizeof(text)-at,i?",%.9g":"%.9g",values[i]);if(wrote<0 || static_cast<size_t>(wrote)>=sizeof(text)-at)break;at+=wrote;}snapshot_material::Field("GEOMETRY",id,key,text);}
bool GeometryDirection(const unsigned char* p,uint32_t stride,const D3DVERTEXELEMENT9* e,float* out){if(!e)return false;
    if(e->Type==D3DDECLTYPE_D3DCOLOR && uint32_t(e->Offset)+4<=stride){out[0]=p[e->Offset+2]*(2.f/255)-1;out[1]=p[e->Offset+1]*(2.f/255)-1;out[2]=p[e->Offset]*(2.f/255)-1;return true;}
    if((e->Type==D3DDECLTYPE_FLOAT3 || e->Type==D3DDECLTYPE_FLOAT4) && uint32_t(e->Offset)+12<=stride){memcpy(out,p+e->Offset,12);return true;}return false;}
void SnapshotGeneratedGeometry(const D3DVERTEXELEMENT9* decl,uint32_t dn,uint32_t stride,uint32_t topology,const unsigned char* source_vertices,const std::map<uint32_t,uint32_t>& reused,
    const D3DVERTEXELEMENT9* normal,const D3DVERTEXELEMENT9* tangent,const D3DVERTEXELEMENT9* binormal,const D3DVERTEXELEMENT9* uv,const kich_r4_capture::Batch& batch,uint32_t vbase,
    const kich_r4_scene::Matrix& world,const kich_r4_scene::Matrix& normal_matrix,bool screen,const kich_r4_capture::Command& command){
    if(!RenderSnapshot_Detailed())return;
    try{using namespace snapshot_material;std::string layout;char entry[96];for(uint32_t i=0;i<dn && i<32 && decl[i].Stream!=255;++i){const D3DVERTEXELEMENT9& e=decl[i];snprintf(entry,sizeof(entry),"stream=%u,offset=%u,type=%u,method=%u,usage=%u,index=%u;",e.Stream,e.Offset,e.Type,e.Method,e.Usage,e.UsageIndex);layout+=entry;}
        uint64_t sample_hash=Hash(world.m,sizeof(world.m));const size_t checked=std::min<size_t>(128,batch.vertices.size()-vbase);
        for(size_t i=0;i<checked;++i)sample_hash=Hash(&batch.vertices[vbase+i],sizeof(kich_r4_capture::Vertex),sample_hash);
        const std::string id=ID("geometry",layout+std::to_string(stride)+std::to_string(sample_hash));RenderSnapshot_GeometrySource(id.c_str());
        Field("GEOMETRY",id,"source","CPU_CAPTURE_GENERATED_STREAM");Field("GEOMETRY",id,"layout",layout.c_str());Number("GEOMETRY",id,"stride",stride);Number("GEOMETRY",id,"source_topology",topology);
        Field("GEOMETRY",id,"layout_scope",dn<=32?"ALL_DECLARATION_ELEMENTS":"FIRST_32_ELEMENTS");Number("GEOMETRY",id,"generated_vertices",batch.vertices.size()-vbase);Number("GEOMETRY",id,"generated_vertex_bytes",uint64_t(batch.vertices.size()-vbase)*sizeof(kich_r4_capture::Vertex));Number("GEOMETRY",id,"generated_index_bytes",uint64_t(command.count)*4);
        Number("GEOMETRY",id,"semantic_category",command.semantic);Field("GEOMETRY",id,"normal_present",normal?"YES":"NO");Field("GEOMETRY",id,"tangent_present",tangent?"YES":"NO");Field("GEOMETRY",id,"binormal_present",binormal?"YES":"NO");
        Field("GEOMETRY",id,"normal_unpack",normal && normal->Type==D3DDECLTYPE_D3DCOLOR?"BGR_BYTES_TO_XYZ_SIGNED; A_IS_HEMISPHERE":normal && (normal->Type==2 || normal->Type==3)?"FLOAT_XYZ":"NO_SUPPORTED_SOURCE_NORMAL");
        Field("GEOMETRY",id,"tangent_basis","SOURCE_DECLARATION_RECORDED; TANGENT_BINORMAL_NOT_RETAINED_IN_R4_GENERATED_VERTEX");Field("GEOMETRY",id,"handedness","NOT_RETAINED; NO_INFERRED_SIGN");
        Field("GEOMETRY",id,"normal_transform",screen?"NOT_APPLICABLE_SCREEN":effect==2 || effect==3?"ENVIRONMENT_NORMAL_CHANNEL_REPURPOSED":"CPU_WORLD_INVERSE_TRANSPOSE_NORMALIZED; PS_DERIVATIVE_FALLBACK_WHEN_LENGTH_SQUARED_LT_0_05");
        Field("GEOMETRY",id,"skin_normals","CPU_STREAM_ALREADY_PRODUCED; SKIN_OPERATION_NOT_INFERRED");GeometryVector(id,"world",world.m,16);GeometryVector(id,"normal_matrix",normal_matrix.m,16);
        Field("GEOMETRY",id,"negative_scale",snapshot_geometry::Determinant(world.m)<0?"YES":"NO");Field("GEOMETRY",id,"nonuniform_scale",snapshot_geometry::Nonuniform(world.m)?"YES":"NO");Field("GEOMETRY",id,"singular_world",fabs(snapshot_geometry::Determinant(world.m))<1e-12?"YES":"NO");
        Field("GEOMETRY",id,"uv0_unpack",!uv?"ABSENT":uv->Type==6?"SHORT2_DIV1024; PACKED_TANGENT_BINORMAL_ALPHA_FRACTIONS_WHEN_PRESENT":uv->Type==7?"SHORT4_DIV2048":uv->Type==15 || uv->Type==16?"HALF2": "FLOAT_XY");
        Field("GEOMETRY",id,"uv_map_bindings","SOURCE_SAMPLER_SEMANTICS_IN_MATERIAL_ROUTE; R4_DIFFUSE_USES_UV0");Field("GEOMETRY",id,"atlas_frame","NOT_INSTRUMENTED_AT_SOURCE; GENERATED_UV_SAMPLES_ONLY");
        snapshot_geometry::Checks output_checks,source_checks;float uv_range[4]={0,0,0,0};uint32_t uv_nonfinite=0,uv_finite=0;
        for(size_t i=0;i<checked;++i){const kich_r4_capture::Vertex& vertex=batch.vertices[vbase+i];if(!screen && effect!=2 && effect!=3)output_checks.Normal(vertex.normal);
            if(!snapshot_geometry::Finite(vertex.uv[0]) || !snapshot_geometry::Finite(vertex.uv[1])){++uv_nonfinite;continue;}
            if(!uv_finite++){uv_range[0]=uv_range[2]=vertex.uv[0];uv_range[1]=uv_range[3]=vertex.uv[1];}else{uv_range[0]=std::min(uv_range[0],vertex.uv[0]);uv_range[1]=std::min(uv_range[1],vertex.uv[1]);uv_range[2]=std::max(uv_range[2],vertex.uv[0]);uv_range[3]=std::max(uv_range[3],vertex.uv[1]);}}
        uint32_t source_checked=0;for(auto i=reused.begin();i!=reused.end() && source_checked<128;++i,++source_checked){const unsigned char* p=source_vertices+size_t(i->first)*stride;float n[3],t[3],b[3];if(GeometryDirection(p,stride,normal,n)){source_checks.Normal(n);if(GeometryDirection(p,stride,tangent,t) && GeometryDirection(p,stride,binormal,b))source_checks.Basis(n,t,b);}}
        Number("GEOMETRY",id,"checked_generated_vertices",checked);Number("GEOMETRY",id,"checked_generated_normals",output_checks.normals);Number("GEOMETRY",id,"zero_generated_normals",output_checks.zero);Number("GEOMETRY",id,"nonfinite_generated_normals",output_checks.nonfinite);
        Number("GEOMETRY",id,"checked_source_normals",source_checks.normals);Number("GEOMETRY",id,"zero_source_normals",source_checks.zero);Number("GEOMETRY",id,"nonfinite_source_normals",source_checks.nonfinite);Number("GEOMETRY",id,"checked_source_bases",source_checks.bases);Number("GEOMETRY",id,"degenerate_source_bases",source_checks.degenerate);
        Field("GEOMETRY",id,"source_normal_check_status",source_checks.normals?"DECODED_CPU_VALUES":normal?"DECLARATION_TYPE_NOT_SUPPORTED_BY_CHECKER":"SOURCE_NORMAL_ABSENT");
        Field("GEOMETRY",id,"source_check_scope",reused.empty()?"NOT_CHECKED_FOR_EXPANDED_LINE_POINT_STREAM":source_checked==reused.size()?"ALL_REFERENCED_UNIQUE_SOURCE_VERTICES":"FIRST_128_UNIQUE_SOURCE_INDICES_ASCENDING");
        Field("GEOMETRY",id,"generated_check_scope",checked==batch.vertices.size()-vbase?"ALL_GENERATED_VERTICES":"FIRST_128_GENERATED_VERTICES");Number("GEOMETRY",id,"uv_nonfinite_samples",uv_nonfinite);Number("GEOMETRY",id,"uv_finite_samples",uv_finite);GeometryVector(id,"uv_sample_minmax",uv_range,4);Field("GEOMETRY",id,"uv_sample_scope",checked==batch.vertices.size()-vbase?"ALL_GENERATED_VERTICES":"FIRST_128_GENERATED_VERTICES_NOT_FULL_RANGE");
    }catch(...){RenderSnapshot_GeometrySource("METADATA_UNAVAILABLE");MaterialError();}
}
uint32_t CapturedSemantic(bool screen,uint32_t selected_effect){using namespace kich_r4_capture;if(screen)return CAPTURE_UI;if(selected_effect==2)return CAPTURE_SKY;if(selected_effect==3)return CAPTURE_CLOUDS;
    RenderSnapshotProvenance p={};RenderSnapshot_GetProvenance(&p);if(p.flags&RENDER_SNAPSHOT_PARTICLE)return CAPTURE_PARTICLE;if(p.flags&RENDER_SNAPSHOT_GLOW)return CAPTURE_GLOW;
    if(!strcmp(p.semantic_type,"STATIC_WALLMARK") || !strcmp(p.semantic_type,"DECAL_STATIC"))return CAPTURE_STATIC_WALLMARK;
    if(!strcmp(p.semantic_type,"SKELETAL_WALLMARK") || !strcmp(p.semantic_type,"DECAL_SKELETAL"))return CAPTURE_SKELETAL_WALLMARK;
    return CAPTURE_UNKNOWN;}
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
            if((celestial_source || lens_effect) && !screen)kich_r4_optics::SkyDepth(out.p);
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
        if(lens_effect && !screen){cmd.effect=1;kich_r4_optics::SkyStyle(cmd.style);}
        if(celestial_source && !screen){cmd.effect=kich_r4_optics::CelestialSource;kich_r4_optics::SkyStyle(cmd.style);}
        cmd.semantic=CapturedSemantic(screen,effect);cmd.captured_vertices=static_cast<uint32_t>(batch.vertices.size())-vbase;
        cmd.shader=active_shader;cmd.shader_element=source_element;cmd.shader_pass=source_pass;
        if(g_render_snapshot_active){cmd.resource=texture;cmd.skin="CPU_CAPTURE_STREAM";}
        cmd.console=g_render_snapshot_console;
        if(effect==2){cmd.style.depth=1;cmd.style.write=0;cmd.style.cull=KICH_R4_CULL_NONE;}
        SnapshotGeneratedGeometry(decl,dn,stride,topology,vertices,reused,normal,tangent,binormal,uv,batch,vbase,world,nm,screen,cmd);
        SnapshotCommand(cmd,batch.vertices,batch.indices,celestial_source && !screen?"CELESTIAL_SOURCE":screen?"UI_GEOMETRY":"UNKNOWN",NULL,0,uv!=NULL);
        batch.commands.push_back(cmd);if(layer==2)++batch.ui;else if(layer==1)++batch.hands;else ++batch.models;
        (void)vertex_count;
    }catch(const std::exception& e){state->frame.Fail(e.what());}
}
