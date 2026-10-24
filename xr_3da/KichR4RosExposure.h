#ifndef KICH_R4_ROS_EXPOSURE_H
#define KICH_R4_ROS_EXPOSURE_H
#include <stdint.h>
#include <algorithm>
namespace kich_r4_ros {
// The R2 LightTrack hemisphere, sampled incrementally. Five rays per admitted
// refresh cover every direction; smoothing itself never performs a ray test.
inline const float* HemisphereDirection(unsigned index){
    static const float directions[26][3]={
        {0,1,0},{.52573f,.85065f,0},{.16246f,.85065f,.5f},
        {-.42533f,.85065f,.30902f},{-.42533f,.85065f,-.30902f},{.16246f,.85065f,-.5f},
        {.89443f,.44721f,0},{.27639f,.44721f,.85065f},{-.72361f,.44721f,.52573f},
        {-.72361f,.44721f,-.52573f},{.27639f,.44721f,-.85065f},{.68819f,.52573f,.5f},
        {-.26287f,.52573f,.80902f},{-.85065f,.52573f,0},{-.26287f,.52573f,-.80902f},
        {.68819f,.52573f,-.5f},{.95106f,0,.30902f},{.58779f,0,.80902f},
        {0,0,1},{-.58779f,0,.80902f},{-.95106f,0,.30902f},
        {-.95106f,0,-.30902f},{-.58779f,0,-.80902f},{0,0,-1},
        {.58779f,0,-.80902f},{.95106f,0,-.30902f}};
    return directions[index%26];
}
inline float ExposureUnit(float value){return value>0?(value<1?value:1):0;}
inline float RaisedSampleY(float center,float radius,float low,float high){
    // R2 raises the probe by .3 * sphere radius. Limit the rise by the current
    // transformed box height so a flattened ragdoll does not probe above its body.
    if(!(radius>0) || !(high>low))return center;
    const float rise=(std::min)(radius*.3f,(high-low)*.25f);
    return (std::min)(center+rise,(std::max)(center,high-.01f));
}
struct VisualExposure {
    bool samples[26];unsigned count,next,visible;
    float hemi_target,sun_target,hemi,sun,origin[3];
    uint32_t sampled_at,smoothed_frame;
    bool initialized,origin_valid,smoothed;
    VisualExposure():count(0),next(0),visible(0),hemi_target(1),sun_target(1),hemi(1),sun(1),
        sampled_at(0),smoothed_frame(0),initialized(false),origin_valid(false),smoothed(false){
        std::fill(samples,samples+26,false);std::fill(origin,origin+3,0.f);
    }
    void Reset(){*this=VisualExposure();}
    void Begin(const float* position,float radius,uint32_t now){
        float distance=0;for(unsigned axis=0;axis<3;++axis){const float d=position[axis]-origin[axis];distance+=d*d;}
        const float teleport=(std::max)(4.f,radius*4.f);
        const bool discontinuity=!origin_valid || uint32_t(now-sampled_at)>2000 || distance>teleport*teleport;
        const float movement=(std::max)(.5f,radius*.75f);
        if(discontinuity || distance>movement*movement){
            count=visible=0;std::fill(samples,samples+26,false);
            std::copy(position,position+3,origin);origin_valid=true;
            // Ordinary motion discards spatially stale ray history while
            // retaining interpolation. Re-entry/teleport initializes directly.
            // Keep rotating directions during ordinary motion; restarting at
            // the zenith on every step would never sample the lower hemisphere.
            if(discontinuity){next=0;initialized=false;smoothed=false;}
        }
        sampled_at=now;
    }
    unsigned NextDirection()const{return next;}
    void Sample(bool exposed){
        if(count==26)visible-=samples[next]?1u:0u;else ++count;
        samples[next]=exposed;visible+=exposed?1u:0u;next=(next+1)%26;
    }
    void Commit(bool sun_exposed){
        hemi_target=count?float(visible)/float(count):1.f;sun_target=sun_exposed?1.f:0.f;
        if(!initialized){hemi=hemi_target;sun=sun_target;initialized=true;}
    }
    void Smooth(uint32_t frame,float delta,float speed){
        if(smoothed && smoothed_frame==frame)return;
        smoothed=true;smoothed_frame=frame;
        const float amount=ExposureUnit(delta*speed);
        hemi+=amount*(hemi_target-hemi);sun+=amount*(sun_target-sun);
        hemi=ExposureUnit(hemi);sun=ExposureUnit(sun);
    }
};
}
#endif
