#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <stdio.h>
#include "xr_3da/KichR4NativeTools.h"
#include "xr_3da/xrRender_R4/R4TelemetryRules.h"
using namespace kich_r4;
typedef int32_t HRESULT;typedef uint64_t UINT64;
#define S_OK 0
#define S_FALSE 1
#define FALSE 0
#define FAILED(x) ((x)<0)
#define D3D11_QUERY_TIMESTAMP_DISJOINT 10
#define D3D11_QUERY_TIMESTAMP 11
#define D3D11_ASYNC_GETDATA_DONOTFLUSH 1
struct D3D11_QUERY_DESC {uint32_t Query,MiscFlags;};
struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT {uint64_t Frequency;int Disjoint;};
struct ID3D11Query {uint32_t kind;uint64_t stamp;explicit ID3D11Query(uint32_t k):kind(k),stamp(0){}void Release(){delete this;}};
template<class T>void Release(T*& p){if(p)p->Release();p=NULL;}
struct Hardware {unsigned calls,fail_call;Hardware():calls(0),fail_call(0){}HRESULT CreateQuery(const D3D11_QUERY_DESC* d,ID3D11Query** q){if(++calls==fail_call)return -1;*q=new ID3D11Query(d->Query);return 0;}};
struct Context {bool ready,disjoint,failed;uint64_t frequency,stamp;unsigned reads,begins,ends;
    Context():ready(false),disjoint(false),failed(false),frequency(1000),stamp(0),reads(0),begins(0),ends(0){}
    void Begin(ID3D11Query*){++begins;}void End(ID3D11Query* q){q->stamp=stamp;stamp+=10;++ends;}
    HRESULT GetData(ID3D11Query* q,void* output,uint32_t,uint32_t flags){assert(flags==D3D11_ASYNC_GETDATA_DONOTFLUSH);++reads;if(failed)return -2;if(!ready)return S_FALSE;
        if(q->kind==D3D11_QUERY_TIMESTAMP_DISJOINT){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT* t=static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(output);t->Disjoint=disjoint;t->Frequency=frequency;}else *static_cast<uint64_t*>(output)=q->stamp;return S_OK;}
};
struct Device {
    struct ProfileSlot {ID3D11Query* disjoint;ID3D11Query* stamp[8];bool pending;KichR4TelemetryContext origin;uint32_t valid_mask,status;uint64_t poll_count;
        ProfileSlot():disjoint(NULL),pending(false),valid_mask(127),status(0),poll_count(0){memset(stamp,0,sizeof(stamp));memset(&origin,0,sizeof(origin));}
        void Reset(){Release(disjoint);for(unsigned i=0;i<8;++i)Release(stamp[i]);pending=false;memset(&origin,0,sizeof(origin));status=0;poll_count=0;}~ProfileSlot(){Reset();}} profile[4];
    Hardware hardware;Context commands;Hardware* hw;Context* context;KichR4TelemetryContext telemetry;bool capture_query_requested,profile_open;uint32_t profile_write,timing_count;KichR4TelemetryTiming timing_results[8];
    Device():hw(&hardware),context(&commands),capture_query_requested(false),profile_open(false),profile_write(0),timing_count(0){memset(&telemetry,0,sizeof(telemetry));memset(timing_results,0,sizeof(timing_results));}
    void Emit(const char*,const char*,const char*,const char*){}
    void TimingResult(const KichR4TelemetryContext& origin,uint32_t status,uint32_t mask,const double* ms=NULL){if(!origin.capture_id)return;assert(timing_count<8);KichR4TelemetryTiming& result=timing_results[timing_count++];memset(&result,0,sizeof(result));result.struct_size=sizeof(result);result.status=status;result.valid_mask=mask;result.capture_id=origin.capture_id;result.engine_frame=origin.engine_frame;result.render_frame=origin.render_frame;if(ms)memcpy(result.milliseconds,ms,sizeof(result.milliseconds));}
};
static Device* current=NULL;
Device* FindDevice(KichR4Handle handle,int32_t& hr){hr=handle==1?0:KICH_R4_E_HANDLE;return hr?NULL:current;}
template<class F>int32_t Protect(F f){return f();}
template<class T>bool OutputOK(T* out,uint32_t bytes){return out && bytes>=sizeof(T);}
