#pragma once
#include "UIWindow.h"
#include "../../RenderSnapshot.h"
#include <stdio.h>
#include <string.h>
/* Diagnostic-only widget identity. No UI text is read. Anonymous sibling paths
 * are layout-local, and process handles are explicitly not persistent IDs. */
namespace snapshot_ui {
inline void Handle(const void* p,char* out,size_t capacity) {
    snprintf(out,capacity,"PROCESS_HANDLE:%llX",(unsigned long long)(uintptr_t)p);
}
inline bool Path(CUIWindow* window,char* out,size_t capacity) {
    if(!out || !capacity)return false;
    CUIWindow* chain[16];unsigned count=0;CUIWindow* ancestor=window;
    for(;ancestor && count<16;ancestor=ancestor->GetParent())chain[count++]=ancestor;
    bool complete=ancestor==NULL;out[0]=0;size_t at=0;
    while(count && at+1<capacity){CUIWindow* w=chain[--count];unsigned sibling=0;
        if(w->GetParent()){CUIWindow::WINDOW_LIST& children=w->GetParent()->GetChildWndList();
            for(CUIWindow::WINDOW_LIST_it i=children.begin();i!=children.end() && *i!=w;++i)++sibling;}
        const shared_str name=w->WindowName();char part[80];int part_length;
        if(!w->GetParent() && !name.size())part_length=snprintf(part,sizeof(part),"unnamed_ROOT_PROCESS_%llX",(unsigned long long)(uintptr_t)w);
        else part_length=snprintf(part,sizeof(part),"%s[%u]",name.size()?name.c_str():"unnamed",sibling);
        if(part_length<0 || (size_t)part_length>=sizeof(part))complete=false;
        const int written=snprintf(out+at,capacity-at,"/%s",part);
        if(written<0){complete=false;break;}
        if((size_t)written>=capacity-at){out[capacity-1]=0;complete=false;break;}at+=(size_t)written;}
    return complete && !count;
}
inline void WindowState(CUIWindow* window,const char* kind,bool shown) {
    char id[64];Handle(window,id,sizeof(id));RenderSnapshot_UIWindow(kind,id,shown);
}
inline void Event(CUIWindow* window,const char* action,const char* result) {
    if(!RenderSnapshot_DiagnosticActive())return;
    char id[192];Path(window,id,sizeof(id));RenderSnapshot_Event("UI",id,action,result);
}
inline void Rect(const char* id,const char* key,const Frect& r) {
    char value[128];snprintf(value,sizeof(value),"%.7g,%.7g,%.7g,%.7g",r.x1,r.y1,r.x2,r.y2);RenderSnapshot_Record("UI",id,key,value);
}
inline void Number(const char* id,const char* key,double n) {
    char value[64];snprintf(value,sizeof(value),"%.9g",n);RenderSnapshot_Record("UI",id,key,value);
}
inline uint64_t NextDrawOrder() {
    static uint64_t capture=0,ordinal=0;const uint64_t now=RenderSnapshot_CaptureID();
    if(capture!=now){capture=now;ordinal=0;}return ++ordinal;
}
class Scope {
    RenderSnapshotProvenance previous;bool active;
public:
    char id[192];
    Scope(CUIWindow* window,const char* type):active(RenderSnapshot_Detailed()) {
        id[0]=0;if(!active)return;const bool path_complete=Path(window,id,sizeof(id));RenderSnapshot_GetProvenance(&previous);
        RenderSnapshot_UIProvenance(id,path_complete?"SCOPED_WIDGET_PATH":"TRUNCATED_WIDGET_PATH");
        RenderSnapshot_Record("UI",id,"path_status",path_complete?"COMPLETE":"TRUNCATED_DEPTH_OR_LENGTH");
        RenderSnapshot_Record("UI",id,"identity_scope","LAYOUT_LOCAL_SIBLING_PATH;UNNAMED_ROOT_PROCESS_LOCAL");
        RenderSnapshot_Object("UI",id,"UNKNOWN","WIDGET_DRAW_CALL",0);RenderSnapshot_Queue("UI_WIDGET");
        RenderSnapshot_PassMask((!strcmp(RenderSnapshot_UIScreen(),"MENU") || !strcmp(RenderSnapshot_UIScreen(),"START_MENU"))?RENDER_SNAPSHOT_PASS_MENU_UI:RENDER_SNAPSHOT_PASS_HUD_UI);
        if(strcmp(previous.ui_widget,id)){RenderSnapshot_Record("UI",id,"widget_type",type);
            RenderSnapshot_RecordU64("UI",id,"last_widget_draw_cpu_ordinal",NextDrawOrder());}
        char handle[64];Handle(window,handle,sizeof(handle));RenderSnapshot_Record("UI",id,"process_handle",handle);
        RenderSnapshot_Record("UI",id,"visible",window->IsShown()?"YES":"NO");
        RenderSnapshot_Record("UI",id,"enabled",window->IsEnabled()?"YES":"NO");
        RenderSnapshot_RecordU64("UI",id,"children_attached",window->GetChildNum());
        RenderSnapshot_RecordU64("UI",id,"postprocess_mode",window->GetPPMode()?1:0);
        Frect absolute;window->GetAbsoluteRect(absolute);Rect(id,"absolute_rect_ui_units",absolute);Rect(id,"local_rect_ui_units",window->GetWndRect());
        if(window->GetParent()){char parent[192];Path(window->GetParent(),parent,sizeof(parent));RenderSnapshot_Record("UI",id,"parent",parent);}
        else RenderSnapshot_Record("UI",id,"parent","NONE");
    }
    ~Scope(){if(active)RenderSnapshot_SetProvenance(&previous);}
};
}
