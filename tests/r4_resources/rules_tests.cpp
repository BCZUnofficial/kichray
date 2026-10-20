#include "../../xr_3da/xrRender_R4/R4ResourceRules.h"
#include <stdio.h>
#include <stdexcept>
#include <stddef.h>
using namespace kich_r4;
namespace {
int checks=0, live=0, destroyed=0;
void Require(bool value,const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
struct Owned {
    Owned() { ++live; }
    ~Owned() { --live; ++destroyed; }
};
std::unique_ptr<Owned> New() { return std::unique_ptr<Owned>(new Owned()); }
void Textures()
{
    for (uint32_t f=0;f<8;++f) for (uint32_t b=0;b<16;++b) {
        KichR4TextureDesc d={sizeof(d),64,32,f,b};
        const bool format=f>=1 && f<=6, depth=f==5 || f==6;
        const bool allowed=format && b>0 && b<8 && !(b&(depth ? 2u : 4u));
        Require(ValidTexture(d)==allowed,"format/view compatibility");
    }
    KichR4TextureDesc d={sizeof(d),1,16384,KICH_R4_RGBA16F,KICH_R4_SHADER_READ};
    Require(ValidTexture(d),"FL11 max dimension");
    d.width=0; Require(!ValidTexture(d),"zero width");
    d.width=16385; Require(!ValidTexture(d),"width beyond FL11");
    d.width=1;d.height=16385;Require(!ValidTexture(d),"height beyond FL11");
    d.height=1;--d.struct_size;Require(!ValidTexture(d),"wrong descriptor ABI");
    Require(PixelBytes(KICH_R4_RGBA16F)==8 && PixelBytes(KICH_R4_DEPTH32F)==4 && PixelBytes(99)==0,"logical bytes");
}
void Buffers()
{
    const int data=0;
    KichR4BufferDesc d={sizeof(d),KICH_R4_CONSTANT_BUFFER,16,0};
    Require(ValidBuffer(d,NULL,0),"aligned empty CB");
    Require(ValidBuffer(d,&data,16),"complete initial data contract");
    Require(!ValidBuffer(d,&data,15),"short data");
    Require(!ValidBuffer(d,NULL,16),"null data with nonzero length");
    d.byte_size=65536;Require(ValidBuffer(d,NULL,0),"max CB");
    d.byte_size=65552;Require(!ValidBuffer(d,NULL,0),"CB too large");
    d.byte_size=15;Require(!ValidBuffer(d,NULL,0),"CB alignment");
    d.byte_size=16;d.stride=4;Require(!ValidBuffer(d,NULL,0),"CB no vertex stride");
    d.kind=KICH_R4_VERTEX_BUFFER;Require(ValidBuffer(d,NULL,0),"VB valid");
    d.stride=0;Require(!ValidBuffer(d,NULL,0),"VB zero stride");
    d.stride=2049;Require(!ValidBuffer(d,NULL,0),"VB stride limit");
    d.stride=3;Require(!ValidBuffer(d,NULL,0),"VB incomplete vertex");
    d.kind=KICH_R4_INDEX_BUFFER;d.stride=2;Require(ValidBuffer(d,NULL,0),"16 bit indices");
    d.stride=4;Require(ValidBuffer(d,NULL,0),"32 bit indices");
    d.stride=1;Require(!ValidBuffer(d,NULL,0),"bad index format");
    d.stride=2;d.byte_size=0;Require(!ValidBuffer(d,NULL,0),"zero allocation");
    d.byte_size=64u*1024u*1024u;Require(ValidBuffer(d,NULL,0),"v1 allocation limit");
    d.byte_size+=2;Require(!ValidBuffer(d,NULL,0),"allocation above v1 limit");
    d.byte_size=16;d.kind=KICH_R4_TEXTURE_2D;Require(!ValidBuffer(d,NULL,0),"texture kind in buffer API");
    d.kind=KICH_R4_INDEX_BUFFER;--d.struct_size;Require(!ValidBuffer(d,NULL,0),"buffer ABI mismatch");
}
void Ownership()
{
    IdSource ids;
    KichR4Handle stale;
    {
        ObjectStore<Owned> a(ids),b(ids);
        stale=a.Insert(New());
        const KichR4Handle other=b.Insert(New());
        Require(a.Find(stale) && !b.Find(stale) && !a.Find(other),"owners never overlap");
        Require(!a.Erase(other) && live==2,"wrong owner never destroys resource");
        Require(a.Erase(stale) && !a.Erase(stale) && !a.Find(stale) && live==1,"release exactly once");
        const KichR4Handle newer=a.Insert(New());
        Require(newer!=stale && !a.Find(stale),"stale handle never aliases new allocation");
        a.Clear();a.Clear();Require(live==1,"idempotent bulk clear");
        Require(!a.Find(0) && !a.Erase(0),"null is not a resource");
    }
    Require(!live,"device destruction releases outstanding children");
    ObjectStore<Owned> next_device(ids);
    Require(next_device.Insert(New())!=stale && !next_device.Find(stale),"destroy/recreate cannot revive stale handle");
    next_device.Clear();
    const int before=destroyed;
    IdSource exhausted(UINT64_MAX);
    ObjectStore<Owned> end(exhausted);
    Require(end.Insert(New())==UINT64_MAX,"last handle valid");
    bool threw=false;
    try { end.Insert(New()); } catch (const std::overflow_error&) { threw=true; }
    Require(threw && live==1 && destroyed==before+1,"failed insert retains no ownership");
    end.Clear();Require(!live,"exhausted store cleanup");
}
}
int main()
{
    try {
        static_assert(offsetof(KichR4ResourceStats,payload_bytes)==16,"ABI alignment");
        static_assert(offsetof(KichR4ResourceAPI,create_device)==16,"function table prefix");
        Textures();Buffers();Ownership();
        printf("B2 production rules/ownership: %d checks passed\n",checks);return 0;
    } catch (const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
