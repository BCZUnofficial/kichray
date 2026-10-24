#pragma once
#include "raster_fixture.h"
#include "../../xr_3da/xrRender/occRasterizer.h"
#include <mutex>
#include <vector>
#include <cstdio>
struct Fmatrix {
    float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44;
    void identity(){std::fill_n(&_11,16,0.f);_11=_22=_33=_44=1;}
};
static_assert(sizeof(Fmatrix)==64,"engine matrix seam");
struct Fbox {Fvector min,max;};
struct vis_data {Fbox box;};
struct CFrustum {};
struct xrCriticalSection {std::mutex mutex;void Enter(){mutex.lock();}void Leave(){mutex.unlock();}};
struct DeviceFixture {u32 dwFrame;Fmatrix mFullTransform;Fvector vCameraPosition;};
struct MenuFixture {bool active;bool IsActive(){return active;}};
struct PersistentFixture {MenuFixture* m_pMainMenu;};
struct LevelFixture {bool bReady;};
extern DeviceFixture Device;
extern PersistentFixture* g_pGamePersistent;
extern LevelFixture* g_pGameLevel;
extern bool fixture_occluded;
extern unsigned fixture_renders;
class CHOM {
public:
    void Load(){}void Unload(){}void Enable(){}
    void Render(CFrustum&){++fixture_renders;Raster.clear();Raster.propagade();
        if(fixture_occluded)std::fill_n(Raster.get_depth_level(0),occ_dim_0*occ_dim_0,occD(.5f*occQ_s32));}
};
typedef char string_path[260];
class IReader {
    std::vector<unsigned char> bytes;size_t at=0;
public:
    explicit IReader(const std::vector<unsigned char>& data):bytes(data){}
    IReader* open_chunk(u32){return new IReader(bytes);}
    size_t length()const{return bytes.size();}
    bool eof()const{return at==bytes.size();}
    void r(void* dst,size_t count){assert(at+count<=bytes.size());memcpy(dst,bytes.data()+at,count);at+=count;}
    u32 r_u32(){u32 value;r(&value,sizeof(value));return value;}
    void close(){delete this;}
};
struct FilesFixture {
    bool exists=false;std::vector<unsigned char> contents;
    void update_path(string_path& out,const char*,const char*){std::strcpy(out,"level.hom");}
    bool exist(const char*){return exists;}
    IReader* r_open(const char*){return exists?new IReader(contents):NULL;}
    void r_close(IReader*& reader){delete reader;reader=NULL;}
};
extern FilesFixture FS;
template<class T> T* xr_new(){return new T();}
template<class T> void xr_delete(T*& ptr){delete ptr;ptr=NULL;}
inline void Msg(const char*,...){}
#include "../../xr_3da/KichR4NativeHOM.h"
