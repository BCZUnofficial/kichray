#pragma once
#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <utility>
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
};
class CDetailManager {
public:
    struct SSwingValue {float rot1,rot2,amp1,amp2,speed;}swing_current;
    struct SlotItem {float scale_calculated;Fmatrix mRotY;float c_sun,c_hemi;};
    typedef Items<SlotItem*> SlotItemVec;
    typedef std::vector<Items<SlotItemVec*> > vis_list;
    std::vector<CDetail*> objects;vis_list m_visibles[3];
};
struct DetailDevice {uint32_t dwFrame;float fTimeGlobal;};
extern DetailDevice Device;
