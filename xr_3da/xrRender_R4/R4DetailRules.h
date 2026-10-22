#ifndef KICH_R4_DETAIL_RULES_H
#define KICH_R4_DETAIL_RULES_H
#include "../KichR4Detail.h"
namespace kich_r4 {
inline bool ValidDetailCapacity(uint32_t n){return n && n<=KICH_R4_MAX_DETAIL_INSTANCES;}
inline bool ValidDetailRange(uint32_t first,uint32_t count,uint32_t uploaded){return count && first<=uploaded && count<=uploaded-first;}
}
#endif
