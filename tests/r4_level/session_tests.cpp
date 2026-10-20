#include "recording.h"
int main(){try{const int calls=Scenario(0);for(int i=1;i<=calls;++i)Scenario(i);
    Scenario(3,7);Scenario(5,7);Scenario(6,7);Scenario(7,7);Scenario(calls,7);
    Scenario(0,error,1);Scenario(0,error,2);Scenario(0,error,3);Scenario(0,error,0,5);
    KichR4LevelAPI a={sizeof(a),KICH_R4_LEVEL_ABI,sizeof(void*),0,Texture,Pipeline,Draw};Require(KichR4LevelSession::ValidAPI(a),"valid level table");
    for(unsigned i=0;i<7;++i){KichR4LevelAPI bad=a;switch(i){case 0:++bad.struct_size;break;case 1:++bad.abi_version;break;case 2:++bad.pointer_size;break;case 3:bad.flags=1;break;
        case 4:bad.create_texture_dds=NULL;break;case 5:bad.create_pipeline=NULL;break;case 6:bad.draw_textured=NULL;break;}Require(!KichR4LevelSession::ValidAPI(bad),"bad level API rejected");}
    printf("B4.2 production EXE level session: %d checks; %d API failure positions passed\n",checks,calls);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
