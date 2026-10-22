#ifndef KICH_R4_MODEL_RULES_H
#define KICH_R4_MODEL_RULES_H
#include "../KichR4Model.h"
#include <string.h>
namespace kich_r4 {
inline bool ModelFinite(float value){uint32_t bits;memcpy(&bits,&value,4);return (bits&0x7f800000u)!=0x7f800000u;}
inline bool ValidModelPose(const KichR4ModelPose& pose){
    if(pose.reserved || pose.skinned>1 || pose.bone_count>KICH_R4_MAX_MODEL_BONES || pose.skin_mode>2)return false;
    if(pose.skinned && (!pose.bone_count || !pose.skin_mode))return false;
    if(!pose.skinned && pose.bone_count)return false;
    for(uint32_t b=0;b<pose.bone_count;++b)for(unsigned i=0;i<16;++i)if(!ModelFinite(pose.bones[b][i]))return false;
    return true;
}
inline bool ValidModelRange(uint32_t first,uint32_t count,uint32_t available){return count && !(count%3) && first<=available && count<=available-first;}
}
#endif
