#include "KichR4NativePrelude.h"
#include "KichR4NativeHOM.h"
#include "KichR4Occlusion.h"
#include "xrRender/HOM.h"
#include "xrRender/occRasterizer.h"

namespace {
struct HOMLock {
    xrCriticalSection& lock;
    explicit HOMLock(xrCriticalSection& value):lock(value){lock.Enter();}
    ~HOMLock(){lock.Leave();}
};
bool HOMGameReady(){
    return g_pGameLevel && g_pGameLevel->bReady && g_pGamePersistent &&
        !(g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
}
bool ValidHOMFile(){
    string_path path;FS.update_path(path,"$level$","level.hom");if(!FS.exist(path))return false;
    IReader* file=FS.r_open(path);if(!file)return false;
    IReader* data=file->open_chunk(1);bool valid=data && data->length()>=80 && data->length()%40==0 && data->length()/40<=1000000;
    if(valid)while(!data->eof()){
        float vertices[9];data->r(vertices,sizeof(vertices));data->r_u32();
        for(unsigned i=0;i<9;++i)if(!kich_r4_occlusion::Finite(vertices[i]) || fabsf(vertices[i])>1.e6f)valid=false;
        const double ax=double(vertices[3])-vertices[0],ay=double(vertices[4])-vertices[1],az=double(vertices[5])-vertices[2];
        const double bx=double(vertices[6])-vertices[0],by=double(vertices[7])-vertices[1],bz=double(vertices[8])-vertices[2];
        const double nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
        if(nx*nx+ny*ny+nz*nz<=1.e-12)valid=false;
        if(!valid)break;
    }
    if(data)data->close();
    FS.r_close(file);return valid;
}
}

KichR4NativeHOM::KichR4NativeHOM():core(NULL),loaded(false),ready(false),ready_frame(u32(-1)){
    prepared_view.identity();prepared_camera.set(0,0,0);
}
KichR4NativeHOM::~KichR4NativeHOM(){Unload();xr_delete(core);}
void KichR4NativeHOM::Invalidate(){HOMLock lock(guard);ready=false;}
void KichR4NativeHOM::Unload(){HOMLock lock(guard);ready=false;ready_frame=u32(-1);loaded=false;if(core)core->Unload();}
void KichR4NativeHOM::Load(){
    Unload();HOMLock lock(guard);
    if(!ValidHOMFile()){Msg("[R4 E1.3 HOM] missing or invalid level.hom; light visibility remains conservative");return;}
    if(!core)core=xr_new<CHOM>();
    core->Load();loaded=true;
    Msg("[R4 E1.3 HOM] R2 CPU occlusion map loaded; current-frame light-volume tests enabled");
}
void KichR4NativeHOM::Prepare(CFrustum& view){
    HOMLock lock(guard);ready=false;
    if(!loaded || !core || !HOMGameReady())return;
    core->Enable();core->Render(view);
    prepared_view=Device.mFullTransform;prepared_camera=Device.vCameraPosition;
    ready_frame=Device.dwFrame;ready=true;
}
BOOL KichR4NativeHOM::Test(const float* low,const float* high,const float* view){
    if(!ready || !loaded || ready_frame!=Device.dwFrame || !HOMGameReady() || !view ||
        memcmp(view,&prepared_view._11,sizeof(prepared_view)) ||
        memcmp(&Device.mFullTransform._11,&prepared_view._11,sizeof(prepared_view)))return TRUE;
    kich_r4_occlusion::Rect rect;
    if(!kich_r4_occlusion::ProjectBox(low,high,&prepared_camera.x,view,rect))return TRUE;
    return Raster.test(rect.left,rect.top,rect.right,rect.bottom,rect.depth);
}
BOOL KichR4NativeHOM::visible(vis_data& value){return visible(value.box);}
BOOL KichR4NativeHOM::visible(Fbox& box){HOMLock lock(guard);return Test(&box.min.x,&box.max.x,&Device.mFullTransform._11);}
BOOL KichR4NativeHOM::LightVisible(const float* position,float range,const float* view){
    float low[3],high[3];if(!kich_r4_occlusion::RangeBounds(position,range,low,high))return TRUE;
    HOMLock lock(guard);return Test(low,high,view);
}
BOOL KichR4NativeHOM::BoxVisible(const float* low,const float* high,const float* view){HOMLock lock(guard);return Test(low,high,view);}
