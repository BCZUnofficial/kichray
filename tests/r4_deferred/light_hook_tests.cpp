#include "../../xr_3da/KichR4Lighting.h"
#include <stdio.h>
#include <stdexcept>
struct V {float x,y,z;};struct C {float r,g,b,a;};
struct light {struct {unsigned type,bActive;} flags;V position,direction;float range,cone;C color;void CaptureR4() const;};
unsigned calls=0;const void* identity=NULL;bool active=false;KichR4LocalLight captured={};
void KichR4_CaptureLight(const void* id,const KichR4LocalLight& value,bool enabled){++calls;identity=id;active=enabled;captured=value;}
// PRODUCTION_HOOK
int main(){try{light l={{2,1},{2,3,4},{0,0,1},20,1,{.5f,.6f,.7f,1}};l.CaptureR4();
#if defined(_EDITOR)
    if(calls)throw std::runtime_error("editor must not call EXE bridge");
#else
    if(calls!=1 || identity!=&l || !active || captured.type!=2 || captured.position[1]!=3 || captured.direction[2]!=1 || captured.color[2]!=.7f || captured.range!=20 || captured.cone!=1)
        throw std::runtime_error("production hook copies light values by field without engine layout assumptions");
    l.flags.bActive=0;l.CaptureR4();if(active || calls!=2)throw std::runtime_error("deactivation forwarded");
#endif
    puts("PASS: production light snapshot hook");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
