#ifndef KICH_R4_FRAME_CONTROLLER_H
#define KICH_R4_FRAME_CONTROLLER_H

#include "../KichR4Frame.h"
#include <string.h>

namespace kich_r4 {
/* Internal DLL implementation seam. The exact production state/ordering is
 * exercised with a recording driver in portable tests; not part of DLL ABI. */
class FrameDriver {
public:
    virtual ~FrameDriver() {}
    virtual int32_t Create(void*,uint32_t,uint32_t)=0;
    virtual void DropView()=0; /* Unbind/ClearState/Flush/Release RTV. */
    virtual int32_t Resize(uint32_t,uint32_t)=0;
    virtual int32_t MakeView()=0;
    virtual void Bind(uint32_t,uint32_t)=0;
    virtual void Clear(const float*)=0;
    virtual int32_t Present(uint32_t,bool)=0;
    virtual int32_t RemovedReason()=0;
    virtual void Close()=0;
};
class FrameController {
    FrameDriver& driver_;
    KichR4FrameStats stats_;
    bool attached_,view_,suspended_,hidden_,open_,cleared_,broken_;
    FrameController(const FrameController&);
    FrameController& operator=(const FrameController&);
    void DropView() {
        driver_.DropView();
        if (view_) { ++stats_.views_released; view_=false; }
    }
    int32_t NativeResult(int32_t hr) {
        stats_.last_hresult=hr;
        if (hr<0) { broken_=true;stats_.device_removed_reason=driver_.RemovedReason(); }
        return hr;
    }
    int32_t Ready() const {
        if (!attached_) return KICH_R4_E_STATE;
        if (broken_) return stats_.last_hresult;
        return KICH_R4_OK;
    }
public:
    explicit FrameController(FrameDriver& driver) : driver_(driver),attached_(false),view_(false),
        suspended_(false),hidden_(false),open_(false),cleared_(false),broken_(false) {
        memset(&stats_,0,sizeof(stats_));stats_.struct_size=sizeof(stats_);
    }
    static bool ValidSize(uint32_t w,uint32_t h) { return w && h && w<=16384 && h<=16384; }
    static bool ValidColor(const KichR4FrameColor& c) {
        if(c.struct_size!=sizeof(c)) return false;
        for(unsigned i=0;i<4;++i) if(!(c.rgba[i]>=0.f && c.rgba[i]<=1.f)) return false;
        return true;
    }
    int32_t Create(const KichR4FrameTargetDesc& desc) {
        if(desc.struct_size!=sizeof(desc) || !desc.window || !ValidSize(desc.width,desc.height)) return KICH_R4_E_ARGUMENT;
        if(attached_) return KICH_R4_E_STATE;
        broken_=false;stats_.device_removed_reason=0;
        int32_t hr=driver_.Create(desc.window,desc.width,desc.height);
        if(hr>=0) hr=driver_.MakeView();
        if(hr<0) { NativeResult(hr);driver_.Close();return hr; }
        attached_=view_=true;suspended_=hidden_=open_=cleared_=false;
        stats_.width=desc.width;stats_.height=desc.height;++stats_.views_created;
        return NativeResult(KICH_R4_OK);
    }
    void Close() {
        open_=cleared_=false;
        if(attached_ || view_) { DropView();driver_.Close(); }
        attached_=suspended_=hidden_=false;
    }
    int32_t Resize(uint32_t w,uint32_t h) {
        const int32_t ready=Ready();if(ready<0)return ready;
        if(open_)return KICH_R4_E_STATE;
        if(!w || !h) { suspended_=true;return KICH_R4_FRAME_SUSPENDED; }
        if(!ValidSize(w,h))return KICH_R4_E_ARGUMENT;
        suspended_=false;
        if(w==stats_.width && h==stats_.height && view_)return KICH_R4_OK;
        DropView();
        int32_t hr=driver_.Resize(w,h);
        if(hr<0)return NativeResult(hr);
        ++stats_.resizes;stats_.width=w;stats_.height=h;
        hr=driver_.MakeView();if(hr<0)return NativeResult(hr);
        view_=true;++stats_.views_created;
        return NativeResult(KICH_R4_OK);
    }
    int32_t Begin() {
        const int32_t ready=Ready();if(ready<0)return ready;
        if(open_)return KICH_R4_E_STATE;
        if(suspended_)return KICH_R4_FRAME_SUSPENDED;
        if(hidden_) {
            const int32_t hr=driver_.Present(0,true);NativeResult(hr);
            if(hr<0)return hr;
            if(hr>0)return KICH_R4_FRAME_OCCLUDED;
            hidden_=false;
        }
        if(!view_)return KICH_R4_E_STATE;
        driver_.Bind(stats_.width,stats_.height);open_=true;cleared_=false;
        return KICH_R4_OK;
    }
    int32_t Clear(const KichR4FrameColor& color) {
        if(!ValidColor(color))return KICH_R4_E_ARGUMENT;
        const int32_t ready=Ready();if(ready<0)return ready;
        if(!open_)return KICH_R4_E_STATE;
        driver_.Clear(color.rgba);cleared_=true;++stats_.clears;return KICH_R4_OK;
    }
    int32_t Present(uint32_t interval) {
        if(interval>1)return KICH_R4_E_ARGUMENT;
        const int32_t ready=Ready();if(ready<0)return ready;
        if(!open_ || !cleared_)return KICH_R4_E_STATE;
        open_=cleared_=false;++stats_.present_attempts;
        const int32_t hr=driver_.Present(interval,false);NativeResult(hr);
        if(hr<0)return hr;
        if(hr>0) { if(!hidden_)++stats_.occlusions;hidden_=true;return KICH_R4_FRAME_OCCLUDED; }
        hidden_=false;++stats_.presents;return KICH_R4_OK;
    }
    KichR4FrameStats Stats() const {
        KichR4FrameStats result=stats_;
        result.flags=(attached_?KICH_R4_FRAME_TARGET:0)|(view_?KICH_R4_FRAME_VIEW:0)|
            (suspended_?KICH_R4_FRAME_PAUSED:0)|(hidden_?KICH_R4_FRAME_HIDDEN:0)|
            (open_?KICH_R4_FRAME_OPEN:0)|(broken_&&attached_?KICH_R4_FRAME_FAILED:0);
        return result;
    }
};
}
#endif
