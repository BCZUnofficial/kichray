#include "native_fixture.h"
#include <cstdio>
#include <limits>
MemoryFixture Memory;RendererFixture RImplementation;
DeviceFixture Device={};MenuFixture menu={false};PersistentFixture persistent={&menu};LevelFixture level={true};
PersistentFixture* g_pGamePersistent=&persistent;LevelFixture* g_pGameLevel=&level;
FilesFixture FS;bool fixture_occluded=true;unsigned fixture_renders=0;
unsigned checks=0;void Check(bool value){++checks;assert(value);}
int main(){
    Device.dwFrame=1;Device.vCameraPosition.set(0,0,0);Device.mFullTransform.identity();
    Device.mFullTransform._33=100.f/99.8f;Device.mFullTransform._34=1;
    Device.mFullTransform._43=-20.f/99.8f;Device.mFullTransform._44=0;
    const float position[3]={0,0,10};CFrustum view;KichR4NativeHOM hom;
    Check(hom.LightVisible(position,1,&Device.mFullTransform._11));hom.Prepare(view);Check(fixture_renders==0);
    hom.Load();hom.Prepare(view);Check(hom.LightVisible(position,1,&Device.mFullTransform._11));
    FS.exists=true;FS.contents.resize(79);hom.Load();hom.Prepare(view);Check(fixture_renders==0);
    FS.contents.resize(80);hom.Load();hom.Prepare(view);Check(fixture_renders==0);
    const float triangle[9]={0,0,1,1,0,1,0,1,1};memcpy(FS.contents.data(),triangle,sizeof(triangle));memcpy(FS.contents.data()+40,triangle,sizeof(triangle));
    hom.Load();Check(hom.LightVisible(position,1,&Device.mFullTransform._11));
    hom.Prepare(view);Check(fixture_renders==1);Check(!hom.LightVisible(position,1,&Device.mFullTransform._11));
    Check(hom.LightVisible(position,11,&Device.mFullTransform._11));
    Fmatrix other=Device.mFullTransform;other._41=.1f;Check(hom.LightVisible(position,1,&other._11));
    Device.mFullTransform._41=.1f;Check(hom.LightVisible(position,1,&Device.mFullTransform._11));Device.mFullTransform._41=0;
    ++Device.dwFrame;Check(hom.LightVisible(position,1,&Device.mFullTransform._11));hom.Prepare(view);Check(!hom.LightVisible(position,1,&Device.mFullTransform._11));
    menu.active=true;Check(hom.LightVisible(position,1,&Device.mFullTransform._11));hom.Prepare(view);menu.active=false;Check(hom.LightVisible(position,1,&Device.mFullTransform._11));
    hom.Prepare(view);hom.Invalidate();Check(hom.LightVisible(position,1,&Device.mFullTransform._11));
    fixture_occluded=false;hom.Prepare(view);Check(hom.LightVisible(position,1,&Device.mFullTransform._11));
    fixture_occluded=true;hom.Prepare(view);hom.Unload();Check(hom.LightVisible(position,1,&Device.mFullTransform._11));
    level.bReady=false;hom.Load();hom.Prepare(view);Check(hom.LightVisible(position,1,&Device.mFullTransform._11));level.bReady=true;
    hom.Prepare(view);Check(!hom.LightVisible(position,1,&Device.mFullTransform._11));
    std::printf("E1.3 production native HOM adapter lifecycle/stale camera/menu/fail-open: %u checks passed\n",checks);
}
