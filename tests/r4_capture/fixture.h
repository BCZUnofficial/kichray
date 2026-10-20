#ifndef KICH_R4_CAPTURE_FIXTURE_H
#define KICH_R4_CAPTURE_FIXTURE_H
#include "../r4_level/fixture.h"
namespace capture_fixture {
using fixture::Bytes;using fixture::U16;using fixture::U32;using fixture::Float;using fixture::Chunk;
inline Bytes Model(unsigned weights=1){Bytes out,h,v,indices,t;h.push_back(4);h.push_back(weights?5:0);h.resize(44,0);Chunk(out,1,h);
    fixture::Z(t,"BASE.dds");fixture::Z(t,"models\\model");Chunk(out,2,t);
    U32(v,weights==1?0x12071980u:weights==2?0x240e3300u:0x112u);U32(v,3);
    for(unsigned i=0;i<3;++i){if(weights==2){U16(v,0);U16(v,1);}Float(v,float(i));Float(v,float(i==1));Float(v,5);
        for(unsigned a=0;a<(weights?9u:3u);++a)Float(v,0);
        if(weights==2){Float(v,.25f);}Float(v,i==2?1.f:0.f);Float(v,i==1?1.f:0.f);if(weights==1){U32(v,1);}}
    Chunk(out,3,v);U32(indices,6);U16(indices,0);U16(indices,0);U16(indices,0);U16(indices,0);U16(indices,1);U16(indices,2);Chunk(out,4,indices);return out;}
}
#endif
