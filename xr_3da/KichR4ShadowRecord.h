#ifndef KICH_R4_SHADOW_RECORD_H
#define KICH_R4_SHADOW_RECORD_H
#include "KichR4RecordControl.h"
#include <stdint.h>
#include <string.h>

// Numeric, allocation-free observations of successful shadow submissions.
// Signatures detect a changed set/range; they cannot identify an added caster
// by themselves and do not measure shadowed pixels. Process identities and
// resource handles are comparable only within the current device/level epoch.
namespace kich_r4_shadow_record {
enum Kind {Static,Model,Detail,Stream,KindCount};
inline uint64_t Mix(uint64_t v){v^=v>>30;v*=UINT64_C(0xbf58476d1ce4e5b9);v^=v>>27;v*=UINT64_C(0x94d049bb133111eb);return v^(v>>31);}
inline uint64_t FloatBits(float v){uint32_t bits;memcpy(&bits,&v,sizeof(bits));return bits;}
inline uint64_t DetailID(uint32_t mesh,float x,float y,float z){return Mix(mesh)^Mix(FloatBits(x)+1)^Mix(FloatBits(y)+2)^Mix(FloatBits(z)+3);}
struct Work {
    uint64_t draws,casters,indices,id_sum,id_xor,range_sum,unknown_ids;
    Work():draws(0),casters(0),indices(0),id_sum(0),id_xor(0),range_sum(0),unknown_ids(0){}
    void Caster(uint64_t id,uint32_t first,uint32_t count,bool known=true){
        ++casters;if(!known){++unknown_ids;return;}
        const uint64_t mixed=Mix(id);id_sum+=mixed;id_xor^=mixed;
        range_sum+=Mix(mixed^Mix((uint64_t(first)<<32)|count));
    }
    void Draw(uint32_t count,uint32_t instances=1){++draws;indices+=uint64_t(count)*instances;}
    void Add(const Work& v){draws+=v.draws;casters+=v.casters;indices+=v.indices;id_sum+=v.id_sum;id_xor^=v.id_xor;range_sum+=v.range_sum;unknown_ids+=v.unknown_ids;}
};
struct Pass {
    bool begun,completed,signatures;
    uint32_t resolution,frame,face;
    uint64_t map,generation,source;
    float span,texel_size,matrix[16];
    Work work[KindCount];
    Pass():begun(false),completed(false),signatures(false),resolution(0),frame(0),face(0),map(0),generation(0),source(0),span(0),texel_size(0),matrix(){}
    void Submitted(Kind kind,uint64_t id,uint32_t first,uint32_t count,bool known=true){work[kind].Caster(id,first,count,known && signatures);work[kind].Draw(count);}
};
inline Pass*& Current(){static Pass* pass=NULL;return pass;}
inline Pass* Active(){return Current();}
struct Scope {
    Pass* previous;
    explicit Scope(Pass* pass):previous(Current()){Current()=pass;}
    ~Scope(){Current()=previous;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
struct Frame {
    bool valid;
    uint32_t frame,local_attempted,local_completed,local_resolution;
    uint64_t local_generation;
    Pass sun[2],local_pass;
    Work local[KindCount];
    Frame():valid(false),frame(0),local_attempted(0),local_completed(0),local_resolution(0),local_generation(0){}
    void Begin(uint32_t value){
        valid=kich_r4_record::Enabled();if(!valid)return;
        frame=value;local_attempted=local_completed=local_resolution=0;local_generation=0;
        sun[0]=Pass();sun[1]=Pass();local_pass=Pass();for(unsigned k=0;k<KindCount;++k)local[k]=Work();
    }
    static void Configure(Pass& p,uint32_t frame,uint32_t face,uint64_t map,uint64_t generation,uint32_t resolution,float span,const float* matrix,uint64_t source){
        p=Pass();p.frame=frame;p.face=face;p.map=map;p.generation=generation;p.resolution=resolution;p.span=span;
        p.texel_size=span>0 && resolution?span/resolution:0;p.signatures=span>0;p.source=source;memcpy(p.matrix,matrix,sizeof(p.matrix));
    }
    Pass* Sun(unsigned cascade,uint64_t map,uint64_t generation,uint32_t resolution,float span,const float* matrix){
        if(!valid || cascade>=2)return NULL;
        Configure(sun[cascade],frame,cascade,map,generation,resolution,span,matrix,0);return &sun[cascade];
    }
    Pass* Local(uint32_t face,uint64_t map,uint64_t generation,uint32_t resolution,const float* matrix,uint64_t source){
        if(!valid)return NULL;
        ++local_attempted;local_resolution=resolution;local_generation=generation;
        Configure(local_pass,frame,face,map,generation,resolution,0,matrix,source);return &local_pass;
    }
    void FinishLocal(){if(!valid)return;local_pass.completed=true;++local_completed;for(unsigned k=0;k<KindCount;++k)local[k].Add(local_pass.work[k]);}
};
}
#endif
