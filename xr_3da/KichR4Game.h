#ifndef KICH_R4_GAME_H
#define KICH_R4_GAME_H
#include "KichR4Probe.h"
#include <stdint.h>
#include "KichR4Lighting.h"

/* EXE integration only. No changes to CRenderDevice or IRender_interface ABI. */
struct KichR4GameObservation {
    // Valid only while optional recording is enabled. CurrentEntity is the
    // controlled object, not a type-checked actor; the view object may differ.
    bool valid,current_entity_valid,view_entity_valid;
    bool demo_active,paused,console_visible;
    uint32_t current_entity_id,view_entity_id;
    float camera_direction[3],current_entity_position[3],view_entity_position[3];
    float detail_radius;
};
struct KichR4GameFrame {
    void* window;
    uint32_t width,height,engine_frame;
    float view_projection[16],position[3];
    // Actual engine projection, including the first-person near-plane change.
    // Kept separate for diagnostics; raster/reconstruction share full VP above.
    float camera_projection[16];
    bool allowed,active,toggle_down,debug_down;
    // EXE-only: collect/upload the actual scene during the existing precache
    // frames, while the loading/UI image remains the displayed output.
    bool warmup;
    KichR4LightingFrame lighting;
    float time,prepare_ms;
    float tree_wave[4],tree_wind[4],tree_sbc;
    // EXE-only progressive geometry policy: bias, SSA start/end, camera FOV.
    float geometry_lod[4];
    float postprocess[24];
    KichR4GameObservation observation;
};
bool KichR4_GameInitialize(const char*,KichR4LogFn,const KichR4LevelFiles*);
bool KichR4_GameWindowed();
void KichR4_GameLevelStart(const char*);
void KichR4_GameLevelStop();
void KichR4_GameBeforeReset();
void KichR4_GameShutdown();
void KichR4_GamePrepare(const KichR4GameFrame&);
bool KichR4_GamePresentPrepared();
void KichR4_GamePrepareEngineFrame();

/* One additive EXE export, called by the matching xrGame UI. */
#if defined(_WIN32)
#if defined(ENGINE_BUILD)
#define KICH_R4_GAME_EXPORT __declspec(dllexport)
#else
#define KICH_R4_GAME_EXPORT __declspec(dllimport)
#endif
#else
#define KICH_R4_GAME_EXPORT
#endif
KICH_R4_GAME_EXPORT void KichR4_GameSetModal(bool);
#undef KICH_R4_GAME_EXPORT
#endif
