#pragma once

class CHOM;

// Native CPU occlusion uses the same level.hom rasterizer as R2. Its result is
// valid only for the explicitly prepared camera and frame; readers never cause
// implicit rendering or consume the previous frame's depth map.
class KichR4NativeHOM {
    CHOM* core;
    xrCriticalSection guard;
    bool loaded,ready;
    u32 ready_frame;
    Fmatrix prepared_view;
    Fvector prepared_camera;
    BOOL Test(const float* low,const float* high,const float* view);
public:
    KichR4NativeHOM();
    ~KichR4NativeHOM();
    void Load();
    void Unload();
    void Invalidate();
    // Builds the map once per frame and camera; a second call is a no-op.
    void Prepare(CFrustum& view);
    // P0-S11 PERF02 (R2 CHOM::MT_RENDER): the same preparation on the engine's
    // parallel thread, from the frame's final mFullTransform.
    void __stdcall MT_Prepare();
    BOOL visible(vis_data& value);
    BOOL visible(Fbox& box);
    BOOL LightVisible(const float* position,float range,const float* view);
    // P0-S7: a local shadow face's lit box, same current-frame contract.
    BOOL BoxVisible(const float* low,const float* high,const float* view);
    // Raster.test calls this in the legacy implementation. Prepare/Test hold
    // our lock already; rerendering from this callback would invalidate depth.
    void MT_SYNC(){}
};
