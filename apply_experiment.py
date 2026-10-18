#!/usr/bin/env python3
"""Apply experiment 1 (Clear Sky inspired DX9 R2 sun shafts) to kichray.
No network required. Validates every source anchor before writing and creates backups.
"""
from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent


def replace_one(text: str, anchor: str, replacement: str, name: str) -> str:
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f"{name}: expected one source anchor, got {count}: {anchor[:95]!r}")
    return text.replace(anchor, replacement, 1)


def build_patches(repo: Path) -> dict[Path, bytes]:
    source_files = [
        "xr_3da/xrRender/xrRender_console.h",
        "xr_3da/xrRender/xrRender_console.cpp",
        "xr_3da/xrRender_R2/r2_rendertarget.h",
        "xr_3da/xrRender_R2/r2_rendertarget.cpp",
        "xr_3da/xrRender_R2/r2_rendertarget_accum_direct.cpp",
    ]
    sources: dict[str, str] = {}
    line_endings: dict[str, bytes] = {}
    for name in source_files:
        raw = (repo / name).read_bytes()
        line_endings[name] = b"\r\n" if b"\r\n" in raw else b"\n"
        sources[name] = raw.replace(b"\r\n", b"\n").decode("latin-1")

    name = source_files[0]
    text = sources[name]
    anchor = "extern ECORE_API float ps_r2_soft_particles_distance;"
    text = replace_one(text, anchor, anchor + "\nextern ECORE_API int ps_r2_sun_shafts;\nextern ECORE_API float ps_r2_sun_shafts_intensity;", name)
    sources[name] = text

    name = source_files[1]
    text = sources[name]
    anchor = "float ps_r2_soft_particles_distance = 0.5f; // metres in view space"
    text = replace_one(text, anchor, anchor + "\nint ps_r2_sun_shafts = 0; // 0 disabled, 1=12, 2=20, 3=40 shadow samples\nfloat ps_r2_sun_shafts_intensity = 0.6f;", name)
    anchor = 'CMD3(CCC_Mask, "r2_sun_stable", &ps_r2_ls_flags, R2FLAG_SUN_STABLE);'
    text = replace_one(text, anchor, anchor + '\n\tCMD4(CCC_Integer, "r2_sun_shafts", &ps_r2_sun_shafts, 0, 3);\n\tCMD4(CCC_Float, "r2_sun_shafts_intensity", &ps_r2_sun_shafts_intensity, 0.f, 2.f);', name)
    sources[name] = text

    name = source_files[2]
    text = sources[name]
    anchor = "\tref_shader s_accum_direct;"
    text = replace_one(text, anchor, anchor + "\n\tref_shader s_accum_direct_volumetric;", name)
    anchor = "\tvoid accum_direct(u32 sub_phase);"
    text = replace_one(text, anchor, anchor + "\n\tvoid accum_direct_volumetric(u32 sub_phase, u32 Offset, const Fmatrix& m_shadow,\n\t\tconst Fvector& sun_dir, const Fvector& sun_color);", name)
    sources[name] = text

    name = source_files[3]
    text = sources[name]
    anchor = '\t\ts_accum_direct.create(b_accum_direct, "r2\\\\accum_direct");'
    # The constructor creates the same shader in two HW/SW shadow-map paths.
    count = text.count(anchor)
    if count != 2:
        raise RuntimeError(f"{name}: expected 2 direct shaders, got {count}")
    # Create the extra shader once AFTER the hardware/software shadow-map choice.
    anchor = '\n\t// POINT\n'
    text = replace_one(text, anchor, '\n\t// Sun shafts shader is available but remains disabled by default.\n\ts_accum_direct_volumetric.create("accum_volumetric_sun");\n' + anchor, name)
    sources[name] = text

    name = source_files[4]
    text = sources[name]
    anchor = "\t\tRCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);\n\n\t\t// Fetch4 : disable"
    text = replace_one(text, anchor, "\t\tRCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);\n\n\t\t// Add volumetric shafts while this sun cascade's shadow map is bound.\n\t\taccum_direct_volumetric(sub_phase, Offset, m_shadow, L_dir, L_clr);\n\n\t\t// Fetch4 : disable", name)
    if "void CRenderTarget::accum_direct_volumetric(" in text:
        raise RuntimeError(f"{name}: shafts implementation already present")
    method = '''
// Experiment 1: shadow-map based sun shafts, adapted from Clear Sky DX9 R2.
// Keep the original sun lighting path unchanged when the effect is disabled.
void CRenderTarget::accum_direct_volumetric(u32 sub_phase, u32 Offset, const Fmatrix& m_shadow,
                                            const Fvector& sun_dir, const Fvector& sun_color)
{
    if (ps_r2_sun_shafts == 0 || ps_r2_sun_shafts_intensity < 0.0001f ||
        !RImplementation.o.fp16_blend)
        return;
    if (sub_phase != SE_SUN_NEAR && sub_phase != SE_SUN_FAR)
        return;

    // The stock SoC accumulator lacks Clear Sky's separate volume RT.
    // We add the shafts to the existing FP16 HDR light buffer instead.
    phase_accumulator();
    RCache.set_CullMode(CULL_NONE);
    RCache.set_ColorWriteEnable();

    // Slot zero is the shadow map (see shadow.h); match the active R2 backend.
    LPCSTR smap_name = RImplementation.o.HW_smap ? r2_RT_smap_depth : r2_RT_smap_surf;
    STextureList* textures = &*s_accum_direct_volumetric->E[0]->passes[0]->T;
    for (STextureList::iterator it = textures->begin(); it != textures->end(); ++it)
    {
        if (it->first == 0)
            it->second.create(smap_name);
    }

    const float samples = (ps_r2_sun_shafts == 1) ? 12.f :
                          (ps_r2_sun_shafts == 2) ? 20.f : 40.f;
    extern float OLES_SUN_LIMIT_27_01_07;
    const float cascade_start = (sub_phase == SE_SUN_NEAR) ? 0.f : ps_r2_sun_near;
    const float cascade_end = (sub_phase == SE_SUN_NEAR) ? ps_r2_sun_near : OLES_SUN_LIMIT_27_01_07;

    RCache.set_Element(s_accum_direct_volumetric->E[0]);
    RCache.set_Geometry(g_combine_2UV);
    RCache.set_Stencil(FALSE);
    RCache.set_c("m_shadow", m_shadow);
    RCache.set_c("Ldynamic_dir", sun_dir.x, sun_dir.y, sun_dir.z, 0.f);
    RCache.set_c("Ldynamic_color", sun_color.x, sun_color.y, sun_color.z, 0.f);
    RCache.set_c("kich_sun_shafts_params", samples, ps_r2_sun_shafts_intensity,
                 cascade_start, cascade_end);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Restore the stencil contract expected by the remaining lighting passes.
    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0xff, 0x00);
}
'''
    sources[name] = text.rstrip() + "\n" + method

    pending: dict[Path, bytes] = {}
    for name in source_files:
        pending[repo / name] = sources[name].encode("latin-1").replace(b"\n", line_endings[name])
    for name in ("accum_volumetric_sun.ps", "accum_volumetric_sun.s"):
        pending[repo / "resources" / "shaders" / "r2" / name] = (
            ROOT / "resources" / "shaders" / "r2" / name
        ).read_bytes()
    return pending


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", help="Path to a local clone of BCZUnofficial/kichray")
    parser.add_argument("--check", action="store_true", help="Validate without changing any files")
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    if not (repo / "XR_3DA.sln").exists():
        print("ERROR: repository root not found (XR_3DA.sln missing)", file=sys.stderr)
        return 2
    try:
        pending = build_patches(repo)  # All anchors validated before ANY write.
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    if args.check:
        print("OK: anchors validated. Files to change:")
        for path in pending:
            print("  " + str(path.relative_to(repo)))
        return 0
    backup = repo / "_experiment_backups" / ("ex1_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    # Read old data to safeguard against partial backup failures.
    old = {path: path.read_bytes() if path.exists() else None for path in pending}
    for path, contents in old.items():
        if contents is not None:
            copy_to = backup / path.relative_to(repo)
            copy_to.parent.mkdir(parents=True, exist_ok=True)
            copy_to.write_bytes(contents)
    try:
        for path, contents in pending.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
    except Exception:
        # Restore the pre-apply state, including newly created shader files.
        for path, contents in old.items():
            if contents is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(contents)
        raise
    print("Applied experiment 1. Backups:", backup)
    print("Build xrGame and copy resources/shaders/r2/accum_volumetric_sun.* to game gamedata/shaders/r2/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
