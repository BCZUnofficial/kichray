#pragma once
#include "Render.h"
#include "xrRender/PSLibrary.h"
#include "xrRender/ModelPool.h"
#include "xrRender/DetailManager.h"
#include "xrRender/WallmarksEngine.h"
#include "Fmesh.h"
#include "KichR4NativeHOM.h"
#include "KichR4CandidateScratch.h"
class ENGINE_API ISpatial;

class CR4Target:public IRender_Target {
public:
    float blur,gray,duality_h,duality_v,noise,noise_scale,noise_fps;
    u32 color_base,color_gray,color_add;
    CR4Target():blur(0),gray(0),duality_h(0),duality_v(0),noise(0),noise_scale(1),noise_fps(25),color_base(0x007f7f7f),color_gray(0xff555555),color_add(0){}
    void set_blur(float v){blur=v;}void set_gray(float v){gray=v;}
    void set_duality_h(float v){duality_h=v;}void set_duality_v(float v){duality_v=v;}
    void set_noise(float v){noise=v;}void set_noise_scale(float v){noise_scale=v;}void set_noise_fps(float v){noise_fps=v;}
    void set_color_base(u32 v){color_base=v;}void set_color_gray(u32 v){color_gray=v;}void set_color_add(u32 v){color_add=v;}
    u32 get_width(){return Device.dwWidth;}u32 get_height(){return Device.dwHeight;}
};
class CRender:public IRender_interface,public pureFrame {
    struct Queued {IRender_Visual* visual;Fmatrix world;bool hud;float distance;bool light_valid;float hemi,sun;};
    xr_vector<Queued> queue;
    xr_vector<CFrustum> shadow_views;
    xr_vector<ISpatial*> candidate_objects;
    kich_r4_candidates::UniquePointers<IRenderable> candidate_seen;
    std::vector<uint8_t> selected_shadow_lights;
    Fmatrix transform;
    IRenderable* object;
    bool invisible,loaded;
    CR4Target target;
    xr_vector<ref_shader> shaders;
    xr_vector<IRender_Visual*> visuals;
    xr_vector<IRender_Sector*> sectors;
    xr_vector<IDirect3DVertexBuffer9*> vertices;
    xr_vector<IDirect3DIndexBuffer9*> indices;
    typedef svector<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> Decl;
    xr_vector<Decl> declarations;
    xr_vector<FSlideWindowItem> windows;
    void Submit(IRender_Visual* visual,CKinematics* parent);
    void LoadBuffers();
public:
    enum {PHASE_NORMAL=0,PHASE_POINT=1,PHASE_SPOT=2};
    u32 phase;BOOL val_bHUD;
    CModelPool* Models;CDetailManager* Details;CWallmarksEngine* Wallmarks;
    CPSLibrary PSLibrary;
    KichR4NativeHOM HOM;
    CRender();~CRender();
    GenerationLevel get_generation(){return GENERATION_R4;}
    void create();void destroy();void reset_begin();void reset_end();
    void level_Load(IReader*);void level_Unload();
    IDirect3DBaseTexture9* texture_load(LPCSTR,u32&);
    HRESULT shader_compile(LPCSTR,LPCSTR,UINT,void*,void*,LPCSTR,LPCSTR,DWORD,void*,void*,void*);
    LPCSTR getShaderPath(){return "r1\\";}
    ref_shader getShader(int);IRender_Sector* getSector(int);IRender_Visual* getVisual(int);
    IRender_Sector* detectSector(const Fvector&);IRender_Target* getTarget(){return &target;}
    void set_Transform(Fmatrix*);void set_HUD(BOOL);BOOL get_HUD(){return val_bHUD;}
    void set_Invisible(BOOL v){invisible=v!=FALSE;}void flush();void set_Object(IRenderable* v){object=v;}
    void add_Occluder(Fbox2&){}void add_Visual(IRender_Visual*);void add_Geometry(IRender_Visual*);
    void add_StaticWallmark(ref_shader&,const Fvector&,float,CDB::TRI*,Fvector*);
    void clear_static_wallmarks();void add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark>);
    void add_SkeletonWallmark(const Fmatrix*,CKinematics*,ref_shader&,const Fvector&,const Fvector&,float);
    IBlender* blender_create(CLASS_ID);void blender_destroy(IBlender*&);
    IRender_ObjectSpecific* ros_create(IRenderable*);void ros_destroy(IRender_ObjectSpecific*&);
    IRender_Light* light_create();IRender_Glow* glow_create();
    IRender_Visual* model_CreateParticles(LPCSTR);IRender_DetailModel* model_CreateDM(IReader*);
    IRender_Visual* model_Create(LPCSTR,IReader* =0);IRender_Visual* model_CreateChild(LPCSTR,IReader*);
    IRender_Visual* model_Duplicate(IRender_Visual*);void model_Delete(IRender_Visual*&,BOOL=FALSE);
    void model_Delete(IRender_DetailModel*&);void model_Logging(BOOL v){Models->Logging(v);}
    void models_Prefetch(){Models->Prefetch();}void models_Clear(BOOL v){Models->ClearPool(v);}
    BOOL occ_visible(vis_data&);BOOL occ_visible(Fbox&);BOOL occ_visible(sPoly&);
    void OnFrame();
    void Calculate();void Render();void Screenshot(ScreenshotMode,LPCSTR=0);
    void rmNear();void rmFar();void rmNormal();u32 memory_usage(){return 0;}
    D3DVERTEXELEMENT9* getVB_Format(int);IDirect3DVertexBuffer9* getVB(int);
    IDirect3DIndexBuffer9* getIB(int);FSlideWindowItem* getSWI(int);
    IRender_Visual* model_CreatePE(LPCSTR);
    void apply_lmaterial(){}
    void r_dsgraph_render_wmarks(){}
};
extern CRender RImplementation;
