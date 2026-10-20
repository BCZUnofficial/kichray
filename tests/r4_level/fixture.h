#ifndef KICH_R4_LEVEL_FIXTURE_H
#define KICH_R4_LEVEL_FIXTURE_H
#include <vector>
#include <string>
#include <stdint.h>
#include <string.h>
namespace fixture {
typedef std::vector<unsigned char> Bytes;
inline void U16(Bytes& b,uint16_t v){b.push_back(static_cast<unsigned char>(v));b.push_back(static_cast<unsigned char>(v>>8));}
inline void U32(Bytes& b,uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<unsigned char>(v>>(i*8)));}
inline void Set(Bytes& b,unsigned at,uint32_t v){for(unsigned i=0;i<4;++i)b[at+i]=static_cast<unsigned char>(v>>(i*8));}
inline void Float(Bytes& b,float f){uint32_t n;memcpy(&n,&f,4);U32(b,n);}
inline void Z(Bytes& b,const std::string& s){b.insert(b.end(),s.begin(),s.end());b.push_back(0);}
inline void Chunk(Bytes& b,uint32_t id,const Bytes& data){U32(b,id);U32(b,static_cast<uint32_t>(data.size()));b.insert(b.end(),data.begin(),data.end());}
inline Bytes Header(){Bytes h;U16(h,14);U16(h,0);return h;}
inline void Element(Bytes& d,uint16_t offset,unsigned char type,unsigned char usage){U16(d,0);U16(d,offset);d.push_back(type);d.push_back(0);d.push_back(usage);d.push_back(0);}
inline void End(Bytes& d){U16(d,255);U16(d,0);d.push_back(17);d.push_back(0);d.push_back(0);d.push_back(0);}
inline Bytes Window(){Bytes w(16,0);U32(w,1);U32(w,3);U16(w,1);U16(w,3);return w;}
inline Bytes Visual(unsigned type,unsigned shader,unsigned vbase,unsigned ibase,unsigned count,bool separate=false){Bytes v,h;
    h.push_back(4);h.push_back(static_cast<unsigned char>(type));U16(h,static_cast<uint16_t>(shader));
    Float(h,type>=7?9.f:-1.f);Float(h,-1);Float(h,4);Float(h,type>=7?11.f:1.f);Float(h,1);Float(h,6);
    for(unsigned i=0;i<4;++i)Float(h,1);
    Chunk(v,1,h);
    if(type==1 || type==6)return v;
    Bytes g;U32(g,type>=7?1:0);U32(g,vbase);U32(g,3);
    if(separate){Chunk(v,7,g);g.clear();}
    U32(g,0);U32(g,ibase);U32(g,count);Chunk(v,separate?8:21,g);
    if(type==2)Chunk(v,6,Window());
    if(type>=7){Bytes t;for(unsigned i=0;i<16;++i)Float(t,i==12?10.f:(i%5==0?1.f:0.f));for(unsigned i=0;i<10;++i)Float(t,0);Chunk(v,12,t);}
    if(type==11){Bytes link;U32(link,0);Chunk(v,20,link);}
    return v;
}
struct Level {
    Bytes level,geometry;
    explicit Level(bool separate=false){Chunk(level,1,Header());Chunk(geometry,1,Header());
        Bytes shaders;U32(shaders,3);Z(shaders,"");Z(shaders,"default/base");Z(shaders,"flora/leaf");Chunk(level,2,shaders);
        Bytes vb;U32(vb,2);
        for(unsigned b=0;b<2;++b){Element(vb,0,2,0);Element(vb,12,4,3);Element(vb,16,4,6);Element(vb,20,4,7);Element(vb,24,b?7:6,5);
            if(!b)Element(vb,28,6,5); /* replace the second TEXCOORD index below */
            if(!b)vb.back()=1;
            End(vb);const unsigned n=b?3:7;U32(vb,n);
            for(unsigned i=0;i<n;++i){Float(vb,float(i%3)-1);Float(vb,i%3==1?1.f:-1.f);Float(vb,5);
                U32(vb,0);U32(vb,0x80000000u);U32(vb,0xff000000u);
                U16(vb,b?1024:static_cast<uint16_t>(-1024));U16(vb,2048);U16(vb,0);U16(vb,0);}}
        Chunk(geometry,9,vb);
        Bytes ib;U32(ib,1);U32(ib,16);const uint16_t indices[]={65535,0,1,2,0,0,0,0,1,2,0,0,0,0,1,2};for(unsigned i=0;i<16;++i)U16(ib,indices[i]);Chunk(geometry,10,ib);
        Bytes swi;U32(swi,1);const Bytes window=Window();swi.insert(swi.end(),window.begin(),window.end());Chunk(geometry,11,swi);
        Bytes visuals;Chunk(visuals,0,Visual(0,1,1,1,3,separate));Chunk(visuals,1,Visual(2,1,4,4,6,separate));
        Chunk(visuals,2,Visual(7,2,0,1,3));Chunk(visuals,3,Visual(11,2,0,10,6));
        Chunk(visuals,4,Visual(1,0,0,0,0));Chunk(visuals,5,Visual(6,0,0,0,0));Chunk(level,3,visuals);
    }
};
inline Bytes Dds(uint32_t w=8,uint32_t h=8,uint32_t mips=1,uint32_t code=0){Bytes d(128,0);Set(d,0,0x20534444);Set(d,4,124);Set(d,8,0x1007);
    Set(d,12,h);Set(d,16,w);Set(d,28,mips);Set(d,76,32);Set(d,80,code?4:0x41);Set(d,84,code);Set(d,88,32);
    Set(d,92,0xff);Set(d,96,0xff00);Set(d,100,0xff0000);Set(d,104,0xff000000);Set(d,108,0x1000);
    for(uint32_t i=0;i<mips;++i){const uint32_t size=code?((w+3)/4)*((h+3)/4)*(code==0x31545844?8:16):w*h*4;
        d.resize(d.size()+size,255);w=w>1?w/2:1;h=h>1?h/2:1;}return d;}
}
#endif
