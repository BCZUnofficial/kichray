#ifndef KICH_R4_FRAME_SESSION_H
#define KICH_R4_FRAME_SESSION_H
#include "KichR4Frame.h"
#include <string.h>

/* EXE-only state. No owning C++ destructor: the guarded window runner retains
 * this state for explicit cleanup even if an MSVC SEH exception interrupts it. */
struct KichR4FrameSession {
    KichR4FrameAPI api;
    KichR4Handle device;
    int32_t failure;
    const char* step;
    uint32_t rendered;
    static bool ValidAPI(const KichR4FrameAPI& a) {
        return a.struct_size==sizeof(a) && a.abi_version==KICH_R4_FRAME_ABI &&
            a.pointer_size==sizeof(void*) && !a.flags && a.create_device && a.destroy_device &&
            a.create_target && a.destroy_target && a.resize && a.begin_frame &&
            a.clear && a.present && a.get_stats;
    }
    int32_t Record(const char* operation,int32_t result) {
        if(result<0 && !failure){failure=result;step=operation;}
        return result;
    }
    int32_t Start(void* window,uint32_t w,uint32_t h) {
        if(device)return Record("duplicate start",KICH_R4_E_STATE);
        int32_t hr=api.create_device(&device);
        if(hr!=KICH_R4_OK)return Record("create hardware device",hr<0?hr:KICH_R4_E_STATE);
        if(!device)return Record("null device handle",KICH_R4_E_STATE);
        const KichR4FrameTargetDesc desc={sizeof(desc),w,h,window};
        hr=api.create_target(device,&desc);
        return Record("create swap chain / backbuffer view",hr==KICH_R4_OK?hr:hr<0?hr:KICH_R4_E_STATE);
    }
    int32_t Resize(uint32_t w,uint32_t h) {
        const int32_t hr=api.resize(device,w,h);
        return Record("resize",(hr<=0 || hr==KICH_R4_FRAME_SUSPENDED)?hr:KICH_R4_E_STATE);
    }
    int32_t Draw(const KichR4FrameColor& color) {
        int32_t hr=api.begin_frame(device);
        if(hr==KICH_R4_FRAME_SUSPENDED || hr==KICH_R4_FRAME_OCCLUDED)return hr;
        if(hr!=KICH_R4_OK)return Record("begin frame",hr<0?hr:KICH_R4_E_STATE);
        hr=api.clear(device,&color);
        if(hr!=KICH_R4_OK)return Record("clear",hr<0?hr:KICH_R4_E_STATE);
        hr=api.present(device,1);
        if(hr==KICH_R4_OK)++rendered;
        else if(hr!=KICH_R4_FRAME_OCCLUDED)Record("present",hr<0?hr:KICH_R4_E_STATE);
        return failure?failure:hr;
    }
    int32_t Stop(KichR4FrameStats* out) {
        if(!device)return KICH_R4_OK;
        const int32_t hr=api.destroy_device(device,out,out?sizeof(*out):0);
        if(hr==KICH_R4_OK)device=0;
        return Record("destroy device",hr==KICH_R4_OK?hr:hr<0?hr:KICH_R4_E_STATE);
    }
};
#endif
