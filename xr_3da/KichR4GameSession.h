#ifndef KICH_R4_GAME_SESSION_H
#define KICH_R4_GAME_SESSION_H
#include "KichR4Game.h"
#include "KichR4LevelSession.h"

namespace kich_r4_game {
inline bool Requested(const char* params) {
    if(!params)return false;
    const char flag[]="-kich_r4_game";
    while(*params) {
        while(*params==' ' || *params=='\t')++params;
        bool quoted=false,match=true;size_t length=0;
        while(*params && (quoted || (*params!=' ' && *params!='\t'))) {
            const char ch=*params++;
            if(ch=='"')quoted=!quoted;
            else {if(length>=sizeof(flag)-1 || ch!=flag[length])match=false;++length;}
        }
        if(!quoted && match && length==sizeof(flag)-1)return true;
    }
    return false;
}
inline bool LevelName(const char* name) {
    if(!name || !*name)return false;
    size_t length=0;
    for(;*name;++name,++length) {
        const unsigned char c=static_cast<unsigned char>(*name);
        if(length>=63 || !((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-'))return false;
    }
    return true;
}
struct APIs {
    KichR4FrameAPI frame;
    KichR4ResourceAPI resources;
    KichR4GeometryAPI geometry;
    KichR4LevelAPI level;
    bool Valid()const {return KichR4FrameSession::ValidAPI(frame) &&
        KichR4GeometrySession::ValidResources(resources) && KichR4GeometrySession::ValidAPI(geometry) &&
        KichR4LevelSession::ValidAPI(level);}
};

/* Device-thread state, owned by the EXE. Legacy and R4 never exchange GPU
 * objects. Render() returns 1 only when it owns presentation for this frame,
 * 0 when the caller should present DX9, or a negative HRESULT on failure. */
struct Session {
    KichR4FrameSession frame;
    KichR4LevelSession level;
    char name[64];
    void* window;
    uint32_t width,height;
    bool loaded,enabled,key_down;
    uint64_t legacy_frames;
    explicit Session(const APIs& a,const KichR4LevelFiles& fs,const char* level_name,KichR4LogFn log):
        window(NULL),width(0),height(0),loaded(false),enabled(true),key_down(false),legacy_frames(0) {
        memset(&frame,0,sizeof(frame));memset(name,0,sizeof(name));
        frame.api=a.frame;level.bridge.resources=a.resources;level.bridge.api=a.geometry;
        level.api=a.level;level.files=fs;level.log=log;
        if(LevelName(level_name))memcpy(name,level_name,strlen(level_name)+1);
        else frame.Record("B4.3a level name",KICH_R4_E_ARGUMENT);
    }
    int32_t Render(const KichR4GameFrame& input) {
        const bool rising=input.toggle_down && !key_down;
        key_down=input.toggle_down;
        if(input.active && input.allowed && rising)enabled=!enabled;
        if(!input.active || !input.allowed || !enabled) {++legacy_frames;return 0;}
        if(frame.failure)return frame.failure;
        if(!input.window || !input.width || !input.height || input.width>16384 || input.height>16384)
            return frame.Record("B4.3a viewport",KICH_R4_E_ARGUMENT);
        kich_r4_scene::Matrix vp={};memcpy(vp.m,input.view_projection,sizeof(vp.m));
        for(unsigned i=0;i<16;++i)if(!kich_r4_level::Finite(vp.m[i]))
            return frame.Record("B4.3a camera matrix",KICH_R4_E_ARGUMENT);
        if(!frame.device) {
            if(frame.Start(input.window,input.width,input.height)<0)return frame.failure;
            window=input.window;width=input.width;height=input.height;
        }else if(window!=input.window)return frame.Record("B4.3a HWND changed",KICH_R4_E_STATE);
        if(!loaded) {
            const int32_t hr=level.Start(frame,name);
            if(hr!=0)return frame.Record("B4.3a load scene",hr<0?hr:KICH_R4_E_STATE);
            loaded=true;
        }
        if(width!=input.width || height!=input.height) {
            const int32_t resized=frame.Resize(input.width,input.height);
            if(resized!=0)return frame.Record("B4.3a resize viewport",resized<0?resized:KICH_R4_E_STATE);
            width=input.width;height=input.height;
        }
        const KichR4FrameColor color={sizeof(color),{.08f,.10f,.13f,1.f}};
        int32_t hr=frame.BeginClear(color);
        if(hr==KICH_R4_FRAME_SUSPENDED || hr==KICH_R4_FRAME_OCCLUDED)return 1;
        if(hr==0)hr=level.DrawWithMatrix(frame,width,height,vp);
        if(hr==0)hr=frame.Present();
        return hr==0 || hr==KICH_R4_FRAME_OCCLUDED?1:hr;
    }
    static bool Balanced(const KichR4GeometryReport& r) {
        return r.struct_size==sizeof(r) && r.frame.struct_size==sizeof(r.frame) &&
            r.resources.struct_size==sizeof(r.resources) && r.geometry.struct_size==sizeof(r.geometry) &&
            !r.frame.flags && r.frame.views_created==r.frame.views_released &&
            !r.resources.live_resources && !r.resources.payload_bytes && r.resources.created==r.resources.released &&
            r.geometry.pipelines_created==r.geometry.pipelines_released && r.geometry.depth_created==r.geometry.depth_released;
    }
    int32_t Stop(KichR4GeometryReport& report) {
        const bool had_device=frame.device!=0;memset(&report,0,sizeof(report));
        const int32_t hr=level.bridge.Stop(frame,&report);
        if(hr!=0)return hr;
        loaded=false;
        return had_device && !Balanced(report)?frame.Record("B4.3a final ownership report",KICH_R4_E_STATE):0;
    }
};
}
#endif
