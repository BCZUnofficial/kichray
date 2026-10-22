#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include "RenderSnapshot.h"
struct Frect {
 union {float x1;float left;};union {float y1;float top;};union {float x2;float right;};union {float y2;float bottom;};
 Frect(float a=0,float b=0,float c=100,float d=100):x1(a),y1(b),x2(c),y2(d){}
 bool intersection(const Frect& a,const Frect& b){x1=std::max(a.x1,b.x1);y1=std::max(a.y1,b.y1);x2=std::min(a.x2,b.x2);y2=std::min(a.y2,b.y2);return x1<x2 && y1<y2;}
};
typedef std::string shared_str;
class CUIWindow {
public:
 typedef std::vector<CUIWindow*> WINDOW_LIST;typedef WINDOW_LIST::iterator WINDOW_LIST_it;
 WINDOW_LIST m_ChildWndList;CUIWindow* parent=NULL;std::string name;bool shown=true,enabled=true;Frect rect;
 virtual ~CUIWindow(){}
 virtual void Draw();
 CUIWindow* GetParent(){return parent;}WINDOW_LIST& GetChildWndList(){return m_ChildWndList;}
 shared_str WindowName(){return name;}bool IsShown(){return shown;}bool IsEnabled(){return enabled;}
 unsigned GetChildNum(){return (unsigned)m_ChildWndList.size();}bool GetPPMode(){return false;}
 void GetAbsoluteRect(Frect& out){out=rect;}Frect GetWndRect(){return rect;}
 void Add(CUIWindow& child){m_ChildWndList.push_back(&child);child.parent=this;}
};
struct FakeUI {unsigned pushes=0,pops=0;void PushScissor(const Frect&){++pushes;}void PopScissor(){++pops;}};
extern FakeUI ui;
inline FakeUI* UI(){return &ui;}
class CUIStatic:public CUIWindow {
public:
 typedef CUIWindow inherited;bool m_bClipper=true;Frect m_ClipRect;unsigned textures=0,texts=0;RenderSnapshotProvenance texture_provenance={};
 void Draw();void DrawTexture(){++textures;RenderSnapshot_GetProvenance(&texture_provenance);
  RenderSnapshotDraw draw={};draw.struct_size=sizeof(draw);draw.submitted=1;draw.topology=4;draw.vertex_count=3;draw.vertex_count_known=1;draw.instance_count=1;RenderSnapshot_Draw(&draw);}
 void DrawText(){++texts;}
};
typedef const char* LPCSTR;
struct IWriter {std::string saved;void w_printf(const char* fmt,const char* name,unsigned key){char out[128];snprintf(out,sizeof(out),fmt,name,key);saved=out;}};
struct IConsole_Command {const char* cName;explicit IConsole_Command(LPCSTR name):cName(name){};virtual ~IConsole_Command(){};};
#define xr_strcmp strcmp
inline void Msg(const char*,...){}
struct KeyEvent {uint32_t dwOfs,dwData;};
struct Receiver {unsigned press=0,release=0;void IR_OnKeyboardPress(unsigned){++press;}void IR_OnKeyboardRelease(unsigned){++release;}};
