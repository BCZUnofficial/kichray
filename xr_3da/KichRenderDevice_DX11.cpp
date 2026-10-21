#include "stdafx.h"
#include "KichRenderDevice.h"
#include "KichR4Native.h"
#include "KichR4Capture.h"
#include "ResourceManager.h"
#include "KichR4LevelFS.inl"
#include <DXSDK/d3dx9.h>
#include <set>
#include <string>
extern xr_token* vid_mode_token;
extern void free_vid_mode_list();
void KichR4_NativePresentEngineFrame();
namespace {
void NativeLog(const char* v){Msg("%s",v);}
int32_t NativeFileOpen(const char* alias,const char* name,KichR4FileView* out){if(!alias || !name || !out)return static_cast<int32_t>(0x80070057u);if(strcmp(alias,"$r4_absolute$"))return kich_r4_level_files.open(alias,name,out);
    if(!out || !name || !FS.exist(name))return static_cast<int32_t>(0x80070002u);out->data=NULL;out->bytes=0;out->owner=NULL;IReader* reader=FS.r_open(name);if(!reader)return static_cast<int32_t>(0x80070002u);
    if(!reader->length() || reader->length()>256u*1024u*1024u){FS.r_close(reader);return static_cast<int32_t>(0x80070057u);}out->data=static_cast<const unsigned char*>(reader->pointer());out->bytes=reader->length();out->owner=reader;return 0;
}
const KichR4LevelFiles native_files={NativeFileOpen,KichR4_CloseLevelFile};
void Modes(){if(vid_mode_token)return;std::set<std::string> modes;DEVMODEW mode={};mode.dmSize=sizeof(mode);
    for(DWORD i=0;EnumDisplaySettingsW(NULL,i,&mode);++i){if(mode.dmPelsWidth<800 || mode.dmPelsHeight<600)continue;char name[32];sprintf_s(name,sizeof(name),"%lux%lu",mode.dmPelsWidth,mode.dmPelsHeight);modes.insert(name);}
    if(modes.empty())modes.insert("1024x768");vid_mode_token=xr_alloc<xr_token>(modes.size()+1);u32 i=0;for(std::set<std::string>::const_iterator m=modes.begin();m!=modes.end();++m,++i){vid_mode_token[i].id=i;vid_mode_token[i].name=xr_strdup(m->c_str());}vid_mode_token[i].id=-1;vid_mode_token[i].name=NULL;
}
void Window(void* window,KichRenderDeviceSize& size){HWND h=static_cast<HWND>(window);MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);R_ASSERT(GetMonitorInfo(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor));
    size.width=psCurrentVidMode[0];size.height=psCurrentVidMode[1];if(size.width<640 || size.height<480){size.width=1024;size.height=768;}
    const bool fullscreen=psDeviceFlags.test(rsFullscreen);const LONG style=fullscreen?WS_POPUP:(WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX);
    SetWindowLong(h,GWL_STYLE,style);SetWindowLong(h,GWL_EXSTYLE,WS_EX_APPWINDOW);
    RECT rect={0,0,static_cast<LONG>(size.width),static_cast<LONG>(size.height)};if(!fullscreen)AdjustWindowRectEx(&rect,style,FALSE,WS_EX_APPWINDOW);
    const RECT& bounds=fullscreen?monitor.rcMonitor:monitor.rcWork;const int width=fullscreen?bounds.right-bounds.left:rect.right-rect.left,height=fullscreen?bounds.bottom-bounds.top:rect.bottom-rect.top;
    const int left=bounds.left+(bounds.right-bounds.left-width)/2,top=bounds.top+(bounds.bottom-bounds.top-height)/2;
    SetWindowPos(h,HWND_NOTOPMOST,left,top,width,height,SWP_FRAMECHANGED|SWP_SHOWWINDOW);ShowWindow(h,SW_SHOW);UpdateWindow(h);
    HW.DevPP.BackBufferWidth=size.width;HW.DevPP.BackBufferHeight=size.height;HW.DevPP.Windowed=TRUE;
    SetCursor(NULL);Msg("[R4 native B5] window=%s render=%ux%u client=%dx%d at=(%d,%d)",fullscreen?"fullscreen borderless":"windowed",size.width,size.height,width,height,left,top);
}
class Device11:public IKichRenderDevice {
public:
    void SetupGPU(bool,bool,bool){}bool IsReferenceDevice()const{return false;}
    void CreateHW(void* window,KichRenderDeviceSize& size){Modes();Window(window,size);R_ASSERT(!HW.pDevice && !HW.pD3D);
        ZeroMemory(&HW.Caps,sizeof(HW.Caps));HW.Caps.geometry.dwRegisters=22;HW.Caps.geometry.dwVertexCache=24;HW.Caps.raster.dwStages=8;HW.Caps.raster_major=3;HW.Caps.raster.bCubemap=TRUE;HW.Caps.raster.bNonPow2=TRUE;HW.Caps.bScissor=TRUE;
        KichR4_CaptureEnable(true,NativeLog);R_ASSERT2(KichR4_NativeCreate(window,size.width,size.height,native_files,NativeLog),"R4 native initialization failed; see log. No DX9 fallback in native mode.");
    }
    void ResetHW(void* window,KichRenderDeviceSize& size){Window(window,size);R_ASSERT2(KichR4_NativeResize(size.width,size.height),"R4 native resize failed");}
    void ValidateHW(){R_ASSERT(!HW.pDevice && !HW.pD3D);}
    void DestroyHW(){KichR4_NativeDestroy();KichR4_CaptureEnable(false);if(vid_mode_token)free_vid_mode_list();}
    void SetupStates(){RCache.set_xform_world(Fidentity);RCache.set_xform_view(Device.mView);RCache.set_xform_project(Device.mProject);}
    void OnDeviceCreate(){RCache.OnDeviceCreate();}void OnDeviceDestroy(){RCache.OnDeviceDestroy();}
    void AllocateResources(){Device.Resources=xr_new<CResourceManager>();}void ReleaseResources(){xr_delete(Device.Resources);}
    void OnResourcesCreate(const char* path){Device.Resources->OnDeviceCreate(path);}void OnResourcesDestroy(bool keep){Device.Resources->OnDeviceDestroy(keep);}
    void BeginResourcesReset(){Device.Resources->reset_begin();}void EndResourcesReset(){Device.Resources->reset_end();}
    void DestroyNecessaryTextures(){Device.Resources->DestroyNecessaryTextures();}void TraceResetReferences(bool){}
    KichRenderDeviceState GetDeviceState(){return Device.b_is_Active?KichDeviceReady:KichDeviceLost;}
    void BeginFrame(){KichR4_NativeBegin(Device.dwWidth,Device.dwHeight,Device.dwFrame);RCache.OnFrameBegin();}
    void Clear(bool backbuffer){if(backbuffer)KichR4_NativeClear(0xff000000);}
    void ClearTarget(){KichR4_NativeClear(0xff000000);}
    void PrepareFrameEnd(uint32_t,uint32_t){}void EndFrame(){KichR4_NativeSetInterval(psDeviceFlags.test(rsVSync));KichR4_NativePresentEngineFrame();RCache.OnFrameEnd();}
    void OverdrawBegin(){}void OverdrawEnd(uint32_t,uint32_t){}
    void SetCameraMatrices(const Fmatrix& view,const Fmatrix& projection,const Fmatrix& full,Fmatrix& inverse){RCache.set_xform_view(view);RCache.set_xform_project(projection);D3DXMatrixInverse(reinterpret_cast<D3DXMATRIX*>(&inverse),NULL,reinterpret_cast<const D3DXMATRIX*>(&full));}
    void BeginFrameTiming(CStatTimer& real){real.FrameStart();real.Begin();}void EndFrameTiming(CStatTimer& real,CStatTimer& total){real.End();real.FrameEnd();total.accum=real.accum;}
};
}
IKichRenderDevice& KichR4_NativeDevice(){static Device11 device;return device;}
