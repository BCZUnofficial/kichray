#include "KichR4Material.h"
#include "KichR4CaptureInput.h"
#include <cassert>
#include <iostream>
using namespace kich_r4_material;
struct Alpha {float x,y;} alpha;
// PRODUCTION_ALPHA_TEST
bool Survives(float a,float ref,uint32_t func){alpha={ref,float(func)};try{AlphaTest(a);return true;}catch(bool){return false;}}
template<class T>struct Ref{T* p;Ref(T* v=NULL):p(v){}operator bool()const{return p!=NULL;}T* operator->()const{return p;}T* _get()const{return p;}};
enum {D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,
    D3DRS_CULLMODE=22,D3DRS_ALPHAREF=24,D3DRS_ALPHAFUNC=25,D3DRS_ALPHABLENDENABLE=27,D3DRS_FOGENABLE=28,
    D3DCMP_GREATER=5,D3DCULL_CW=2,D3DCULL_CCW=3,D3DBLEND_ONE=2,D3DBLEND_ZERO=1};
struct SimulatorStates{std::map<uint32_t,uint32_t> values;
    uint32_t get_RS(uint32_t key,uint32_t fallback)const{const auto found=values.find(key);return found==values.end()?fallback:found->second;}
    uint32_t get_SAMP(uint32_t,uint32_t,uint32_t fallback)const{return fallback;}};
struct State{SimulatorStates state_code;};struct PS{std::string cName;};
struct SPass{Ref<State> state;Ref<PS> ps;};
struct ShaderElement{struct Flags{bool bDistort=false,bEmissive=false,bWmark=false;} flags;std::vector<Ref<SPass>> passes;};
struct Shader{Ref<ShaderElement> E[6];};Shader* source_shader=NULL;
struct ref_shader:Ref<Shader>{void create(const char*,const char*){p=source_shader;}};
// PRODUCTION_MATERIAL_STATE
// PRODUCTION_PASS_STATE
int main(){
    for(Family family:{FAMILY_AREF,FAMILY_MODEL,FAMILY_TREE}){
        Source source;source.family=family;source.blend=family!=FAMILY_AREF;source.alpha_reference=173;
        const Descriptor material=Build(source,PassState());kich_r4_capture::Style copied;Apply(material,copied);
        for(unsigned value:{0u,199u,200u,201u,219u,220u,221u,255u}){
            const float a=value/255.f;
            assert(Survives(a,AlphaReference(copied,false),AlphaFunction(copied,false))==(value>=200));
            assert(Survives(a,AlphaReference(copied,true),AlphaFunction(copied,true))==(value>(family==FAMILY_TREE?200u:220u)));
        }
    }
    Descriptor unresolved;unresolved.alpha_func=4;unresolved.alpha_ref=.5f;
    assert(AlphaFunction(unresolved,true)==4 && AlphaReference(unresolved,true)==.5f);
    State states;states.state_code.values[D3DRS_ALPHATESTENABLE]=1;states.state_code.values[D3DRS_ALPHAREF]=200;
    SPass pass;pass.state=&states;ShaderElement element;element.passes.push_back(&pass);Shader shader;shader.E[0]=&element;source_shader=&shader;
    kich_r4_capture::Style captured;
    assert(KichR4_NativeMaterialState("authored","base",captured) && captured.alpha_func==5);
    auto registered=NativePassState(element,&pass);assert(registered.alpha_test && registered.alpha_func==5);
    assert(!Survives(200.f/255.f,captured.alpha_ref,captured.alpha_func));
    states.state_code.values[D3DRS_ALPHAFUNC]=7;
    assert(KichR4_NativeMaterialState("authored","base",captured) && captured.alpha_func==7);
    registered=NativePassState(element,&pass);assert(registered.alpha_func==7);
    assert(Survives(200.f/255.f,captured.alpha_ref,captured.alpha_func));
    states.state_code.values[D3DRS_ALPHATESTENABLE]=0;
    assert(KichR4_NativeMaterialState("authored","base",captured) && captured.alpha_func==8);
    registered=NativePassState(element,&pass);assert(!registered.alpha_test);
    std::cout<<"PASS: real HLSL alpha boundary coverage, SoC R2 camera/model/tree shadow contracts and actual missing-state adapters\n";
}
