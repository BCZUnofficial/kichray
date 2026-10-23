#ifndef KICH_R4_MATERIAL_FAKE_ENGINE_H
#define KICH_R4_MATERIAL_FAKE_ENGINE_H
#include <cmath>
#include <stdint.h>
#include <string>
#include <vector>
#include "KichR4MaterialRules.h"
#include "KichR4MaterialServices.h"
typedef uint32_t u32;
typedef int BOOL;
static const BOOL TRUE=1;
template<class T>using xr_vector=std::vector<T>;
template<class T>struct Ref {
    T* value;
    Ref(T* p=NULL):value(p){}
    T* _get()const{return value;}
    T* operator->()const{return value;}
    operator bool()const{return value!=NULL;}
};
struct PixelShader {std::string cName;explicit PixelShader(const char* p):cName(p){}};
struct SPass {
    Ref<PixelShader> ps;std::string texture;bool clamp;
    SPass(PixelShader* p,const char* t,bool c=false):ps(p),texture(t),clamp(c){}
};
struct ShaderElement {
    struct Flags {bool bDistort,bWmark;Flags():bDistort(false),bWmark(false){}}flags;
    std::vector<Ref<SPass> > passes;
};
struct SelectionObservation {uint32_t pass,element,available;std::string reason;};
extern std::vector<SelectionObservation> selections;
inline bool RenderSnapshot_Detailed(){return true;}
inline void RenderSnapshot_MaterialSelection(ShaderElement*,uint32_t pass,uint32_t element,uint32_t available,const char* reason){selections.push_back(SelectionObservation{pass,element,available,reason});}
struct Shader {Ref<ShaderElement> E[6];};
struct Fvector {
    float x,y,z;
    Fvector(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    float square_magnitude()const{return x*x+y*y+z*z;}
};
struct Fmatrix {
    Fvector i,j,k,c;
    Fmatrix():i(1,0,0),j(0,1,0),k(0,0,1){}
    void transform_tiny(Fvector& out,const Fvector& p)const{out=Fvector(p.x+c.x,p.y+c.y,p.z+c.z);}
};
inline float _sqrt(float value){return std::sqrt(value);}
extern std::string active_shader,bound_texture;
extern uint32_t effect,source_element,source_pass;
extern bool distortion_enabled,bound_clamp;
extern kich_r4_material::Cache native_materials;
extern const kich_r4_material::Descriptor* active_material;
inline const kich_r4_material::Descriptor* KichR4_NativeFindMaterial(const void* element,uint32_t pass){return native_materials.Find(element,pass);}
void KichR4_NativeElementPass(ShaderElement*,uint32_t);
void KichR4_NativeElementSource(uint32_t);
inline bool KichR4_NativeDistortionEnabled(){return distortion_enabled;}
struct Backend {
    Fmatrix world;
    const Fmatrix& get_xform_world()const{return world;}
    void set_xform_world(const Fmatrix& value){world=value;}
    void set_Element(ShaderElement* element,uint32_t pass){
        // Mirrors the engine's intentional no-op for an absent E[0]. The
        // production Submit must choose E[4], never inherit this binding.
        if(!element || pass>=element->passes.size())return;
        KichR4_NativeElementPass(element,pass);
        const SPass* selected=element->passes[pass]._get();
        bound_texture=selected->texture;bound_clamp=selected->clamp;
    }
};
extern Backend RCache;
struct Draw {std::string shader,texture;uint32_t effect,element,pass;bool clamp;float uv[8];};
extern std::vector<Draw> draws;
struct CKinematics;
enum {MT_PARTICLE_GROUP=1,MT_HIERRARHY,MT_LOD};
struct IRender_Visual {
    int Type;Ref<Shader> shader;bool native_model;
    struct Visibility {struct Sphere {Fvector P;float R;Sphere():R(1){}}sphere;}vis;
    float uv[8];
    IRender_Visual():Type(0),native_model(false){const float q[8]={0,0,1,0,0,1,1,1};for(unsigned i=0;i<8;++i)uv[i]=q[i];}
    virtual ~IRender_Visual(){}
    virtual CKinematics* dcast_PKinematics(){return NULL;}
    virtual void Render(float){
        Draw d;d.shader=active_shader;d.texture=bound_texture;d.effect=effect;
        d.element=source_element;d.pass=source_pass;d.clamp=bound_clamp;
        for(unsigned i=0;i<8;++i)d.uv[i]=uv[i];
        draws.push_back(d);
    }
};
inline bool KichR4_NativeInlineGeometry(void* p){return static_cast<IRender_Visual*>(p)->native_model;}
struct FHierrarhyVisual:IRender_Visual {xr_vector<IRender_Visual*> children;};
struct CKinematics:FHierrarhyVisual {
    CKinematics* dcast_PKinematics(){return this;}
    void CalculateBones(BOOL){}
    void CalculateWallmarks(){}
};
namespace PS {
struct CParticleGroup:IRender_Visual {
    struct Item {xr_vector<IRender_Visual*> children;void GetVisuals(xr_vector<IRender_Visual*>& out){out=children;}};
    xr_vector<Item> items;
};
}
struct Frustum {bool testSphere_dirty(const Fvector& p,float r)const{return std::fabs(p.x)<=10+r;}};
struct CRender {
    BOOL val_bHUD;Frustum ViewBase;
    CRender():val_bHUD(0){}
    void Submit(IRender_Visual*,CKinematics*);
};
#endif
