#ifndef KICH_R4_TELEMETRY_RULES_H
#define KICH_R4_TELEMETRY_RULES_H
#include "../KichR4Telemetry.h"
#include <stddef.h>
#include <string.h>
namespace kich_r4 {
inline uint64_t TelemetryHash(const void* bytes,size_t count){const unsigned char* p=static_cast<const unsigned char*>(bytes);uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<count;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;}
inline bool TelemetryContextValid(const KichR4TelemetryContext& c){return c.struct_size==sizeof(c) && !(c.flags&~3u);}
/* This backend creates RTV/DSV mip0/slice0 only; full SRVs span all mips/slices.
 * The explicit intervals prevent the handle-equality assumption becoming a
 * false general-purpose alias test if the backend later adds subresources. */
inline bool TelemetrySubresourcesOverlap(uint32_t a_first,uint32_t a_count,uint32_t b_first,uint32_t b_count){
    return a_count && b_count && uint64_t(a_first)<uint64_t(b_first)+b_count && uint64_t(b_first)<uint64_t(a_first)+a_count;}
inline uint32_t TelemetryTimestampResult(bool disjoint,uint64_t frequency,const uint64_t* stamps,uint32_t mask,double* milliseconds){
    if(disjoint || !frequency)return KICH_R4_TIMING_DISJOINT;
    for(unsigned i=0;i<7;++i)if(mask&(1u<<i)){if(stamps[i+1]<stamps[i])return KICH_R4_TIMING_DISJOINT;milliseconds[i]=double(stamps[i+1]-stamps[i])*1000.0/double(frequency);}
    return KICH_R4_TIMING_READY;
}
}
#endif
