/* Exercise the real EXE-side smoke test with a deterministic API model.
 * Failure injection at every call checks cleanup; this is not a GPU emulator. */
#include <windows.h>
#include "../../xr_3da/KichR4ResourceProbe.h"
#include <map>
#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>
#include <stdexcept>

namespace {
struct Item { KichR4ResourceInfo info; };
struct Owner {
    std::map<KichR4Handle,Item> items;
    KichR4ResourceStats stats;
    Owner() { memset(&stats,0,sizeof(stats));stats.struct_size=sizeof(stats); }
};
std::map<KichR4Handle,Owner> owners;
std::vector<std::string> logs;
KichR4Handle next_id=1;
int checks=0,calls=0,fail_at=0,corrupt=0,table_requests=0;
bool missing_export=false;
const int32_t injected=static_cast<int32_t>(UINT32_C(0x80004005));
bool Fail() { return ++calls==fail_at; }
void Require(bool value,const char* text) { ++checks;if (!value) throw std::runtime_error(text); }
void Log(const char* text) { logs.push_back(text); }
bool HasLog(const char* text) { for (size_t i=0;i<logs.size();++i) if (logs[i].find(text)!=std::string::npos) return true;return false; }
void Reset() { Require(owners.empty(),"prior run cleaned owners");logs.clear();calls=0;fail_at=0;corrupt=0;next_id=1;missing_export=false;table_requests=0; }
Item* Find(KichR4Handle d,KichR4Handle h) {
    std::map<KichR4Handle,Owner>::iterator o=owners.find(d);if(o==owners.end())return NULL;
    std::map<KichR4Handle,Item>::iterator r=o->second.items.find(h);return r==o->second.items.end()?NULL:&r->second;
}
int32_t Add(KichR4Handle d,const Item& item,KichR4Handle* out) {
    if (!owners.count(d))return KICH_R4_E_HANDLE;
    Owner& o=owners[d];*out=next_id++;o.items[*out]=item;++o.stats.live_resources;++o.stats.created;
    o.stats.payload_bytes+=item.info.byte_size;return 0;
}
int32_t Create(KichR4Handle* out) { *out=0;if(Fail())return injected;*out=next_id++;owners[*out]=Owner();return 0; }
int32_t Destroy(KichR4Handle d,KichR4ResourceStats* stats,uint32_t) {
    if(Fail())return injected;
    if(!owners.count(d))return KICH_R4_E_HANDLE;
    Owner& o=owners[d];o.stats.released+=o.stats.live_resources;o.stats.live_resources=0;o.stats.payload_bytes=0;
    if(stats)*stats=o.stats;
    owners.erase(d);return 0;
}
int32_t Texture(KichR4Handle d,const KichR4TextureDesc* desc,KichR4Handle* out) {
    *out=0;if(Fail())return injected;
    if(desc->format==KICH_R4_DEPTH32F && desc->bindings==KICH_R4_COLOR_TARGET)return KICH_R4_E_ARGUMENT;
    Item r={};r.info.struct_size=sizeof(r.info);r.info.kind=KICH_R4_TEXTURE_2D;r.info.format=desc->format;
    r.info.width=desc->width;r.info.height=desc->height;r.info.bindings=desc->bindings;
    r.info.byte_size=desc->width*desc->height*(desc->format==KICH_R4_RGBA16F ? 8u : 4u);
    return Add(d,r,out);
}
int32_t Buffer(KichR4Handle d,const KichR4BufferDesc* desc,const void*,uint32_t,KichR4Handle* out) {
    *out=0;if(Fail())return injected;
    if(desc->kind==KICH_R4_CONSTANT_BUFFER && desc->byte_size%16)return KICH_R4_E_ARGUMENT;
    Item r={};r.info.struct_size=sizeof(r.info);r.info.kind=desc->kind;r.info.byte_size=desc->byte_size;
    return Add(d,r,out);
}
int32_t Shader(KichR4Handle d,const KichR4ShaderDesc* desc,const char*,uint32_t,KichR4Handle* out,char*,uint32_t) {
    *out=0;if(Fail())return injected;
    Item r={};r.info.struct_size=sizeof(r.info);r.info.kind=desc->kind;
    r.info.binding_count=desc->kind==KICH_R4_VERTEX_SHADER?1:3;r.info.variable_count=desc->kind==KICH_R4_VERTEX_SHADER?2:1;
    return Add(d,r,out);
}
int32_t Info(KichR4Handle d,KichR4Handle h,KichR4ResourceInfo* out,uint32_t) {
    if(Fail())return injected;
    Item* r=Find(d,h);if(!r)return KICH_R4_E_HANDLE;*out=r->info;
    if(corrupt==1)++out->struct_size;
    if(corrupt==2)++out->byte_size;
    if(corrupt==3 && out->kind==KICH_R4_VERTEX_SHADER)++out->binding_count;
    return 0;
}
int32_t Binding(KichR4Handle d,KichR4Handle h,uint32_t i,KichR4ShaderBinding* out,uint32_t) {
    if(Fail())return injected;
    Item* r=Find(d,h);if(!r)return KICH_R4_E_HANDLE;
    memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);out->count=1;
    if(!i) { out->kind=KICH_R4_BIND_CONSTANTS;out->slot=2;strcpy(out->name,"Frame");
        out->byte_size=r->info.kind==KICH_R4_VERTEX_SHADER?80:16;out->variable_count=r->info.variable_count;
        if(corrupt==4)++out->slot;
    } else {out->kind=i==1?KICH_R4_BIND_TEXTURE:KICH_R4_BIND_SAMPLER;out->slot=i==1?3:1;
        strcpy(out->name,i==1?"image":"linear_sampler");}
    return 0;
}
int32_t Variable(KichR4Handle d,KichR4Handle h,uint32_t i,KichR4ShaderVariable* out,uint32_t) {
    if(Fail())return injected;
    Item* r=Find(d,h);if(!r)return KICH_R4_E_HANDLE;
    const bool vertex=r->info.kind==KICH_R4_VERTEX_SHADER,matrix=vertex && !i;
    memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);out->scalar_type=KICH_R4_FLOAT;
    out->byte_size=matrix?64:16;out->byte_offset=matrix?0:vertex?64:0;
    out->rows=matrix?4:1;out->columns=4;out->matrix_layout=matrix?KICH_R4_MATRIX_ROWS:KICH_R4_NOT_MATRIX;
    strcpy(out->name,matrix?"transform":"tint");
    if(corrupt==5)++out->byte_offset;
    if(corrupt==6)out->matrix_layout=KICH_R4_MATRIX_COLUMNS;
    return 0;
}
int32_t Drop(KichR4Handle d,KichR4Handle h) {
    if(Fail())return injected;
    Item* r=Find(d,h);if(!r)return KICH_R4_E_HANDLE;
    Owner& o=owners[d];o.stats.payload_bytes-=r->info.byte_size;--o.stats.live_resources;++o.stats.released;
    o.items.erase(h);return 0;
}
int32_t Stats(KichR4Handle d,KichR4ResourceStats* out,uint32_t) {
    if(Fail())return injected;
    if(!owners.count(d))return KICH_R4_E_HANDLE;
    *out=owners[d].stats;if(corrupt==7)++out->live_resources;return 0;
}
KichR4ResourceAPI API() {
    const KichR4ResourceAPI api={sizeof(api),KICH_R4_RESOURCE_ABI,sizeof(void*),0,
        Create,Destroy,Texture,Buffer,Shader,Info,Binding,Variable,Drop,Stats};return api;
}
int32_t GetAPI(uint32_t version,KichR4ResourceAPI* out,uint32_t bytes) {
    ++table_requests;
    if(version!=KICH_R4_RESOURCE_ABI || bytes!=sizeof(*out))return KICH_R4_E_ARGUMENT;
    *out=API();if(corrupt==8)++out->abi_version;return 0;
}
void Tests() {
    Reset();const KichR4ResourceAPI api=API();
    Require(KichR4_ResourceSelfTest(api,Log),"successful smoke");
    Require(owners.empty() && HasLog("created=11 released=11 live=0"),"success owns/releases all children");
    const int count=calls;
    for(int i=1;i<=count;++i) {
        Reset();fail_at=i;
        Require(!KichR4_ResourceSelfTest(api,Log),"failure propagates");
        Require(owners.empty() && !HasLog("PASS:"),"every failed call cleans outstanding owners/children");
    }
    for(int i=1;i<=7;++i) {
        Reset();corrupt=i;
        Require(!KichR4_ResourceSelfTest(api,Log) && owners.empty(),"corrupt report rejected and cleaned");
    }
    for(int i=0;i<5;++i) {
        Reset();KichR4ResourceAPI bad=api;
        switch(i) {case 0:++bad.abi_version;break;case 1:--bad.struct_size;break;
            case 2:++bad.pointer_size;break;case 3:bad.get_variable=NULL;break;case 4:bad.flags=1;break;}
        Require(!KichR4_ResourceSelfTest(bad,Log) && !calls,"malformed API makes no calls");
    }
    Reset();Require(!KichR4_ResourceSelfTest(api,NULL) && !calls,"null logger safe");
    Reset();missing_export=true;KichR4_CheckResourceBridge(reinterpret_cast<void*>(1),Log);
    Require(!table_requests && !calls && HasLog("resource export missing"),"old B0 DLL remains supported");
    Reset();corrupt=8;KichR4_CheckResourceBridge(reinterpret_cast<void*>(1),Log);
    Require(table_requests==1 && !calls && HasLog("ABI/table mismatch"),"resource table ABI checked before device");
    Reset();KichR4_CheckResourceBridge(reinterpret_cast<void*>(1),Log);
    Require(table_requests==1 && owners.empty() && HasLog("PASS:"),"export handshake and full smoke");
    printf("B2 resource probe: %d checks; %d call failure positions; 7 corrupt reports passed\n",checks,count);
}
}
FARPROC GetProcAddress(HMODULE,const char* name) {
    return !missing_export && !strcmp(name,KICH_R4_EXPORT_RESOURCES)?reinterpret_cast<FARPROC>(GetAPI):NULL;
}
int main() {
    try {Tests();return 0;} catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
