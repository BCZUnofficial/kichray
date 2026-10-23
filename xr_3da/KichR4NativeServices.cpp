#include "stdafx.h"
#include "KichR4Native.h"
#include "ResourceManager.h"
#include "Render.h"
#include "KichR4CaptureInput.h"
#include "KichR4MaterialServices.h"
#include "blenders/Blender.h"
#include "blenders/Blender_CLSID.h"
#include "xrRender/Blender_BmmD.h"
#include "xrRender/Blender_tree.h"
#include "xrRender/Blender_Model_EbB.h"
#include "xrRender_R1/Blender_Model.h"
#include "xrRender_R1/Blender_default_aref.h"
#include "xrRender_R1/Blender_Vertex_aref.h"
#include "IGame_Level.h"
#include <wincodec.h>
#include <vector>
#include <string>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")
namespace {
bool screenshot=false;uint32_t screenshot_mode=0;std::string screenshot_name;
template<class T>void Release(T*& p){if(p){p->Release();p=NULL;}}
void Put32(std::vector<unsigned char>& data,unsigned at,uint32_t value){memcpy(data.data()+at,&value,4);}
}
bool KichR4_NativeTerrain(const char* name,char (*layers)[128]){IBlender* b=Device.Resources->_FindBlender(name);CBlender_BmmD* terrain=b?dynamic_cast<CBlender_BmmD*>(b):NULL;
    if(!terrain)return false;strcpy_s(layers[0],128,terrain->oR_Name);strcpy_s(layers[1],128,terrain->oG_Name);strcpy_s(layers[2],128,terrain->oB_Name);strcpy_s(layers[3],128,terrain->oA_Name);return true;
}
void KichR4_NativePumpLoading(){MSG message;while(PeekMessage(&message,NULL,0,0,PM_REMOVE)){if(message.message==WM_QUIT){PostQuitMessage(static_cast<int>(message.wParam));break;}TranslateMessage(&message);DispatchMessage(&message);}if(Device.b_is_Active)SetCursor(NULL);}
bool KichR4_NativeNeedScreenshot(){return screenshot;}
bool KichR4_NativeScreenshot(uint32_t mode,const char* name){screenshot=true;screenshot_mode=mode;screenshot_name=name?name:"";return true;}
void KichR4_NativeWriteScreenshot(const unsigned char* rgba,uint32_t width,uint32_t height){if(!screenshot || !rgba || !width || !height)return;screenshot=false;
    if(screenshot_mode==IRender_interface::SM_FOR_GAMESAVE){const uint32_t side=128;std::vector<unsigned char> dds(128+side*side*4,0);Put32(dds,0,0x20534444);Put32(dds,4,124);Put32(dds,8,0x100f);Put32(dds,12,side);Put32(dds,16,side);Put32(dds,20,side*4);Put32(dds,76,32);Put32(dds,80,0x41);Put32(dds,88,32);Put32(dds,92,0xff);Put32(dds,96,0xff00);Put32(dds,100,0xff0000);Put32(dds,104,0xff000000);Put32(dds,108,0x1000);
        for(uint32_t y=0;y<side;++y)for(uint32_t x=0;x<side;++x)memcpy(dds.data()+128+(y*side+x)*4,rgba+((uint64_t(y)*height/side)*width+uint64_t(x)*width/side)*4,4);
        IWriter* out=FS.w_open(screenshot_name.c_str());if(out){out->w(dds.data(),dds.size());FS.w_close(out);}return;
    }
    string64 stamp;string_path path;const bool jpeg=screenshot_mode==IRender_interface::SM_NORMAL;
    if(jpeg)sprintf_s(path,sizeof(path),"ss_%s_%s_(%s).jpg",Core.UserName,timestamp(stamp),g_pGameLevel?g_pGameLevel->name().c_str():"mainmenu");
    else sprintf_s(path,sizeof(path),"ss_%s_%s_#%s.tga",Core.UserName,timestamp(stamp),screenshot_name.c_str());
    if(!jpeg){std::vector<unsigned char> tga(18+size_t(width)*height*3,0);tga[2]=2;tga[12]=width&255;tga[13]=(width>>8)&255;tga[14]=height&255;tga[15]=(height>>8)&255;tga[16]=24;tga[17]=32;
        for(size_t i=0;i<size_t(width)*height;++i){tga[18+i*3]=rgba[i*4+2];tga[19+i*3]=rgba[i*4+1];tga[20+i*3]=rgba[i*4];}IWriter* out=FS.w_open("$screenshots$",path);if(out){out->w(tga.data(),tga.size());FS.w_close(out);}return;
    }
    const HRESULT initialized=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);IWICImagingFactory* factory=NULL;IStream* memory=NULL;IWICBitmapEncoder* encoder=NULL;IWICBitmapFrameEncode* frame=NULL;IPropertyBag2* options=NULL;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=CreateStreamOnHGlobal(NULL,TRUE,&memory);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatJpeg,NULL,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(memory,WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,&options);
    if(SUCCEEDED(hr))hr=frame->Initialize(options);
    if(SUCCEEDED(hr))hr=frame->SetSize(width,height);
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    std::vector<unsigned char> bgr(size_t(width)*height*3);for(size_t i=0;i<size_t(width)*height;++i){bgr[i*3]=rgba[i*4+2];bgr[i*3+1]=rgba[i*4+1];bgr[i*3+2]=rgba[i*4];}
    if(SUCCEEDED(hr))hr=frame->WritePixels(height,width*3,bgr.size(),bgr.data());if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();
    if(SUCCEEDED(hr)){STATSTG stat={};hr=memory->Stat(&stat,STATFLAG_NONAME);LARGE_INTEGER zero={};if(SUCCEEDED(hr))hr=memory->Seek(zero,STREAM_SEEK_SET,NULL);
        if(SUCCEEDED(hr) && stat.cbSize.HighPart==0){std::vector<unsigned char> result(stat.cbSize.LowPart);ULONG read=0;hr=memory->Read(result.data(),result.size(),&read);if(SUCCEEDED(hr) && read==result.size()){IWriter* out=FS.w_open("$screenshots$",path);if(out){out->w(result.data(),result.size());FS.w_close(out);}}}}
    if(FAILED(hr))Msg("[R4 native B5] screenshot failed HRESULT=0x%08X",static_cast<u32>(hr));
    Release(options);Release(frame);Release(encoder);Release(memory);Release(factory);if(SUCCEEDED(initialized))CoUninitialize();
}

bool KichR4_NativeMaterialState(const char* name,const char* textures,kich_r4_capture::Style& style){
    ref_shader shader;shader.create(name,textures);if(!shader)return false;
    ShaderElement* element=NULL;for(uint32_t i=0;i<2 && !element;++i)if(shader->E[i] && !shader->E[i]->passes.empty() && !shader->E[i]->flags.bDistort)element=shader->E[i]._get();
    if(!element || !element->passes[0]->state)return false;
    const SimulatorStates& state=element->passes[0]->state->state_code;style.blend=state.get_RS(D3DRS_ALPHABLENDENABLE,0)!=0;style.src=state.get_RS(D3DRS_SRCBLEND,5);style.dst=state.get_RS(D3DRS_DESTBLEND,6);
    style.depth=state.get_RS(D3DRS_ZENABLE,1)!=0;const uint32_t cull=state.get_RS(D3DRS_CULLMODE,D3DCULL_CCW);style.cull=cull==D3DCULL_CW?KICH_R4_CULL_FRONT:cull==D3DCULL_CCW?KICH_R4_CULL_BACK:KICH_R4_CULL_NONE;
    style.alpha_func=state.get_RS(D3DRS_ALPHATESTENABLE,0)?state.get_RS(D3DRS_ALPHAFUNC,7):8;style.alpha_ref=(state.get_RS(D3DRS_ALPHAREF,0)&255)/255.f;style.write=state.get_RS(D3DRS_ZWRITEENABLE,1)!=0;return true;
}

namespace {
kich_r4_material::Cache& NativeMaterials(){static kich_r4_material::Cache* cache=new kich_r4_material::Cache;return *cache;}
std::string MaterialTexture(const char* value){return value && strcmp(value,"$null")?value:"";}
kich_r4_material::PassState NativePassState(const ShaderElement& element,const SPass* pass){
    using namespace kich_r4_material;PassState out;
    out.distortion=element.flags.bDistort!=0;out.emissive=element.flags.bEmissive!=0;out.wallmark=element.flags.bWmark!=0;
    if(!pass || !pass->state)return out;
    const SimulatorStates& state=pass->state->state_code;
    out.blend=state.get_RS(D3DRS_ALPHABLENDENABLE,0)!=0;out.src=state.get_RS(D3DRS_SRCBLEND,D3DBLEND_ONE);out.dst=state.get_RS(D3DRS_DESTBLEND,D3DBLEND_ZERO);
    out.alpha_test=state.get_RS(D3DRS_ALPHATESTENABLE,0)!=0;out.alpha_func=state.get_RS(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS);out.alpha_reference=state.get_RS(D3DRS_ALPHAREF,0);
    out.depth_test=state.get_RS(D3DRS_ZENABLE,1)!=0;out.depth_write=state.get_RS(D3DRS_ZWRITEENABLE,1)!=0;
    const uint32_t cull=state.get_RS(D3DRS_CULLMODE,D3DCULL_CCW);
    out.cull=cull==D3DCULL_CW?KICH_R4_CULL_FRONT:cull==D3DCULL_CCW?KICH_R4_CULL_BACK:KICH_R4_CULL_NONE;
    return out;
}
}
void KichR4_NativeRegisterMaterial(const void* pointer,uint32_t index,const char* shader,const char* textures,
    const char* origin,const char* bump,const char* detail,uint32_t usage,uint64_t class_id){
    using namespace kich_r4_material;
    const ShaderElement* element=static_cast<const ShaderElement*>(pointer);if(!element || !Device.Resources)return;
    Source source;Descriptor& descriptor=source.descriptor;descriptor.shader=shader?shader:"";descriptor.source=origin?origin:"SOURCE_UNAVAILABLE";
    descriptor.class_id=class_id;descriptor.bump=MaterialTexture(bump);descriptor.bump_aux=descriptor.bump.empty()?"":descriptor.bump+"#";
    descriptor.detail=MaterialTexture(detail);descriptor.detail_usage=usage;source.strict_sorting=element->flags.bStrictB2F!=0;
    sh_list texture_list;Device.Resources->_ParseList(texture_list,textures);
    if(!texture_list.empty()){
        descriptor.base=MaterialTexture(texture_list[0].c_str());
        descriptor.material=Device.Resources->m_textures_description.GetMaterial(texture_list[0]);
        descriptor.detail_scale[0]=descriptor.detail_scale[1]=Device.Resources->m_textures_description.GetDetailScale(texture_list[0]);
    }
    IBlender* blender=class_id==UINT64_MAX?NULL:Device.Resources->_FindBlender(shader);
    if(blender && static_cast<uint64_t>(blender->getDescription().CLS)==class_id){
        if(CBlender_default_aref* aref=dynamic_cast<CBlender_default_aref*>(blender)){
            source.family=FAMILY_AREF;source.blend=aref->oBlend.value!=0;source.alpha_reference=aref->oAREF.value;
            descriptor.source+=";SoC_blender_deffer_aref";
        }else if(CBlender_Vertex_aref* aref=dynamic_cast<CBlender_Vertex_aref*>(blender)){
            source.family=FAMILY_AREF;source.blend=aref->oBlend.value!=0;source.alpha_reference=aref->oAREF.value;
            descriptor.source+=";SoC_blender_deffer_aref_vertex";
        }else if(CBlender_Model* model=dynamic_cast<CBlender_Model*>(blender)){
            source.family=FAMILY_MODEL;source.blend=model->oBlend.value!=0;source.alpha_reference=model->oAREF.value;
            descriptor.source+=";SoC_blender_deffer_model";
        }else if(CBlender_Model_EbB* model=dynamic_cast<CBlender_Model_EbB*>(blender)){
            source.family=FAMILY_MODEL;source.blend=model->oBlend.value!=0;source.alpha_reference=0;
            descriptor.source+=";SoC_Blender_Model_EbB_R2";
        }else if(CBlender_Tree* tree=dynamic_cast<CBlender_Tree*>(blender)){
            source.family=FAMILY_TREE;source.blend=tree->oBlend.value!=0;source.not_tree=tree->oNotAnTree.value!=0;
            descriptor.source+=";SoC_Blender_tree_R2";
        }else if(class_id==B_DEFAULT || class_id==B_VERT || class_id==B_LmBmmD){
            source.family=FAMILY_OPAQUE;descriptor.source+=";SoC_opaque_blender";
            if(class_id==B_LmBmmD){descriptor.special=SPECIAL_TERRAIN;descriptor.unsupported_reason="terrain_layers_preserved;terrain_layer_bump_not_implemented";}
        }else if(class_id==B_DETAIL){source.family=FAMILY_DETAIL;descriptor.source+=";detail_preserve_source";}
        else {source.family=FAMILY_SPECIAL;descriptor.source+=";special_preserve_source";}
        // R2 uber_deffer's lmap branch requires the explicit third lmap texture;
        // base alpha is never used as evidence of cutout or lightmapping.
        if(blender->canBeLMAPped() && texture_list.size()>2){
            const char* hemi=texture_list[2].c_str();
            if(hemi && strncmp(hemi,"lmap",4)==0){descriptor.lightmap=MaterialTexture(texture_list[1].c_str());descriptor.hemi=MaterialTexture(hemi);}
        }
    }
    // R1 lighting/special elements are not native base-geometry routes.
    if(index>1)source.family=FAMILY_SPECIAL;
    std::vector<Descriptor> passes;passes.reserve(element->passes.size());
    for(size_t p=0;p<element->passes.size();++p){
        const SPass* pass=element->passes[p]._get();Source pass_source=source;
        const char* ps=pass && pass->ps?pass->ps->cName.c_str():NULL;
        if(ps && (!strcmp(ps,"water") || !strcmp(ps,"waterd") || !strcmp(ps,"water_soft") || !strcmp(ps,"waterd_soft"))){
            pass_source.family=FAMILY_SPECIAL;pass_source.descriptor.special=SPECIAL_WATER;
            pass_source.descriptor.source+=";SoC_compiled_water_shader";
            pass_source.descriptor.unsupported_reason="water_preserved_special_forward;full_R2_water_not_implemented";
        }
        Descriptor material=Build(pass_source,NativePassState(*element,pass));
        if(Bumped(material)){if(!material.unsupported_reason.empty())material.unsupported_reason+=';';material.unsupported_reason+="bump_height_parallax_not_implemented";}
        passes.push_back(material);
    }
    NativeMaterials().Register(pointer,passes);
}
void KichR4_NativeForgetMaterial(const void* element){NativeMaterials().Forget(element);}
const kich_r4_material::Descriptor* KichR4_NativeFindMaterial(const void* element,uint32_t pass){return NativeMaterials().Find(element,pass);}
bool KichR4_NativeResolveMaterial(const char* name,const char* textures,kich_r4_material::Descriptor& out){
    // Only resource loading may compile a shader; Find is an in-memory lookup.
    ref_shader shader;shader.create(name,textures);if(!shader)return false;
    for(uint32_t i=0;i<2;++i)if(shader->E[i] && !shader->E[i]->passes.empty() && !shader->E[i]->flags.bDistort){
        const kich_r4_material::Descriptor* material=KichR4_NativeFindMaterial(shader->E[i]._get(),0);
        if(material){out=*material;return true;}
    }
    out=kich_r4_material::Descriptor();out.shader=name?name:"";out.unsupported_reason="normal_source_element_unavailable";return false;
}
