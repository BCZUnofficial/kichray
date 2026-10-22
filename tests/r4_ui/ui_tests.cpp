#include <fstream>
#include <sstream>
#include <stdexcept>
FakeUI ui;
unsigned checks=0;
void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
std::string Read(){std::ifstream input(RenderSnapshot_LastPath());std::ostringstream text;text<<input.rdbuf();return text.str();}
int main(int argc,char** argv){try{
 Require(argc==2,"output directory");RenderSnapshotInfo info={};strcpy(info.scene,"GAME");strcpy(info.level,"test");strcpy(info.directory,argv[1]);
 CUIWindow root;root.name="inventory";CUIStatic visible,hidden;visible.name="item";hidden.name="hidden";hidden.shown=false;root.Add(visible);root.Add(hidden);
 char short_path[8];Require(!snapshot_ui::Path(&visible,short_path,sizeof(short_path)),"truncated widget path explicitly rejected as incomplete");
 CUIWindow deep[18];for(unsigned i=1;i<18;++i)deep[i-1].Add(deep[i]);char deep_path[192];Require(!snapshot_ui::Path(&deep[17],deep_path,sizeof(deep_path)),"depth-capped path explicitly incomplete");
 root.Draw();Require(visible.textures==1 && visible.texts==1 && hidden.textures==0,"idle instrumentation preserves actual widget traversal");
 Require(!RenderSnapshot_Wanted(),"ordinary UI does not arm captures");
 CCC_SnapshotHotkey hotkey("d_snapshot_key");hotkey.Execute("88");Require(RenderSnapshot_GetHotkey()==88,"assign physical key");hotkey.Execute("256");Require(RenderSnapshot_GetHotkey()==88,"reject invalid key");hotkey.Execute("88 garbage");Require(RenderSnapshot_GetHotkey()==88,"reject trailing key garbage");
 IWriter writer;hotkey.Save(&writer);Require(writer.saved=="d_snapshot_key 88\r\n","persist assignable hotkey");
 Receiver receiver;std::vector<Receiver*> receivers(1,&receiver);unsigned char state[256]={};KeyEvent events[]={{88,128},{88,0},{30,128}};Dispatch(events,3,state,receivers);
 Require(receiver.press==2 && receiver.release==1,"snapshot observer never consumes ordinary key press/release");Require(RenderSnapshot_Wanted(),"assigned key arms snapshot behind modal UI");
 RenderSnapshot_UIWindow("INVENTORY","root",true);RenderSnapshot_UIState("INVENTORY","root","receiver",true,false,true);RenderSnapshot_Begin(info);
 RenderSnapshot_Object("MODEL","prior_mesh","prior_material","prior",0);RenderSnapshotProvenance prior;RenderSnapshot_GetProvenance(&prior);
 root.Draw();RenderSnapshotProvenance after;RenderSnapshot_GetProvenance(&after);
 Require(!strcmp(after.semantic_type,"MODEL") && !strcmp(after.mesh_id,"prior_mesh"),"nested UI scopes restore prior source provenance");
 Require(!strcmp(visible.texture_provenance.ui_widget,"/inventory[0]/item[0]"),"actual texture draw receives widget ancestry");
 Require(!strcmp(visible.texture_provenance.semantic_type,"UI"),"UI source survives into actual draw");
 Require(visible.textures==2 && hidden.textures==0 && ui.pushes==ui.pops,"capture preserves visible traversal and clipping balance");
 RenderSnapshot_End("COMPLETE");std::string result=Read();
 Require(result.find("/inventory[0]/item[0]")!=std::string::npos && result.find("widget_type=STATIC")!=std::string::npos,"specific widget type survives inherited window Draw");
 Require(result.find("widget_type=WINDOW")!=std::string::npos,"hierarchy root represented separately");
 Require(result.find("ui_widget=/inventory[0]/item[0]")!=std::string::npos,"draw evidence carries widget identity beyond dictionary");
 hotkey.Execute("0");KeyEvent disabled[]={{88,128}};Dispatch(disabled,1,state,receivers);Require(!RenderSnapshot_Wanted() && receiver.press==3,"disabled hotkey leaves normal input unchanged");
 CCC_SnapshotUICycle cycle("d_snapshot_ui");RenderSnapshot_UIWindow("INVENTORY","root",false);cycle.Execute("pda");RenderSnapshot_UIState("GAME","NONE","receiver",true,false,false);Require(RenderSnapshot_Wanted(),"explicit UI command schedules real baseline");
 RenderSnapshot_Begin(info);RenderSnapshot_End("COMPLETE");RenderSnapshot_UIWindow("PDA","pda",true);RenderSnapshot_UIState("PDA","pda","receiver",true,false,true);Require(RenderSnapshot_Wanted(),"paused actual PDA state schedules open frame");
 RenderSnapshot_Begin(info);RenderSnapshot_End("COMPLETE");RenderSnapshot_UIWindow("PDA","pda",false);RenderSnapshot_UIState("GAME","NONE","receiver",true,false,false);Require(RenderSnapshot_Wanted(),"actual return to GAME schedules recovery");RenderSnapshot_Begin(info);RenderSnapshot_End("COMPLETE");
 CCC_SnapshotProfile profile("d_snapshot_profile");profile.Execute("0");Require(!RenderSnapshot_Wanted(),"reject zero-length profile");profile.Execute("121");Require(!RenderSnapshot_Wanted(),"reject unbounded profile");
 printf("G3b.1 actual UI draw/helper, console commands and physical input dispatch: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
