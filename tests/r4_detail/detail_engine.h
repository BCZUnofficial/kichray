#pragma once
#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <utility>
#include <mutex>
#include <functional>
#define PI_MUL_2 6.2831853071795864769f
typedef uint32_t u32;
template<size_t N,class... A>int sprintf_s(char (&out)[N],const char* format,A... args){return snprintf(out,N,format,args...);}
inline void Msg(const char*,...){}
template<class T>struct Items:std::vector<T>{void clear_not_free(){this->clear();}};
struct Fvector {float x,y,z;};
struct Fmatrix {float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44;};
struct Texture {std::string cName;};
struct VS {std::string cName;};
typedef std::vector<std::pair<uint32_t,Texture*> > STextureList;
struct SPass {VS* vs;STextureList* T;};
struct Element {std::vector<SPass*> passes;};
struct Shader {Element* E[2];};
struct CDetail {
    struct fvfVertexIn {Fvector P;float u,v;};
    fvfVertexIn* vertices;uint16_t* indices;uint32_t number_vertices,number_indices;
    struct Box {Fvector min,max;}bv_bb;
    Shader* shader;
    struct Sphere {float R;}bv_sphere;
};
// A 3x3 cache grid around the camera slot (production: dm_size 96, 193x193).
const int dm_size=1,dm_cache_line=dm_size+1+dm_size;
const unsigned dm_cache_size=dm_cache_line*dm_cache_line,dm_obj_in_slot=4;
const float dm_fade=63.5f,dm_slot_size=2.f;
inline int iFloor(float v){return int(std::floor(v));}
struct DetailSlot {enum {ID_Empty=0xffff};};
#ifndef _MSC_VER
#define __stdcall
#endif
// P0-S11: the parallel instance assembly's engine services.
struct xrCriticalSection {std::mutex m;void Enter(){m.lock();}void Leave(){m.unlock();}};
namespace fastdelegate {template<class R=void> class FastDelegate0 {std::function<void()> f;
public:template<class T,class M> FastDelegate0(T* object,M method):f([object,method]{(object->*method)();}){}
    void operator()()const{f();}};}
struct DetailEnvironment {float wind_strength_factor;};
struct DetailPersistent {DetailEnvironment environment;DetailEnvironment& Environment(){return environment;}};
extern DetailPersistent* g_pGamePersistent;
class CDetailManager {
public:
    struct SSwingValue {float rot1,rot2,amp1,amp2,speed;
        void lerp(const SSwingValue& a,const SSwingValue& b,float t){rot1=a.rot1+(b.rot1-a.rot1)*t;rot2=a.rot2+(b.rot2-a.rot2)*t;amp1=a.amp1+(b.amp1-a.amp1)*t;amp2=a.amp2+(b.amp2-a.amp2)*t;speed=a.speed+(b.speed-a.speed)*t;}}swing_current,swing_desc[2];
    volatile u32 m_frame_calc=0,m_frame_rendered=0;
    struct SlotItem {float scale_calculated;Fmatrix mRotY;float c_sun,c_hemi;float scale;u32 vis_ID;};
    typedef Items<SlotItem*> SlotItemVec;
    struct SlotPart {u32 id;SlotItemVec items,r_items[3];SlotPart():id(DetailSlot::ID_Empty){}};
    enum SlotType {stReady=0,stPending};
    struct Slot {bool empty;SlotType type;int sx,sz;struct Vis {struct Sphere {Fvector P;}sphere;}vis;SlotPart G[dm_obj_in_slot];Slot():empty(true),type(stReady),sx(0),sz(0),vis(){}};
    typedef std::vector<Items<SlotItemVec*> > vis_list;
    std::vector<CDetail*> objects;vis_list m_visibles[3];
    Slot* cache[dm_cache_line][dm_cache_line];
    Slot cache_pool[dm_cache_size];
    CDetailManager(){for(int z=0;z<dm_cache_line;++z)for(int x=0;x<dm_cache_line;++x)cache[z][x]=&cache_pool[z*dm_cache_line+x];}
    static float FadeRadius();
};
struct DetailDevice {uint32_t dwFrame;float fTimeGlobal;Fvector vCameraPosition;};
extern DetailDevice Device;
extern float ps_r__Detail_radius,r_ssaDISCARD;
inline float CDetailManager::FadeRadius(){return ps_r__Detail_radius;}
