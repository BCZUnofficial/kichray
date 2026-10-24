#!/usr/bin/env python3
"""Count optimized production terrain samples; glslang is not FXC/GPU timing."""
from collections import Counter
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
shader = root / "resources/shaders/r4/bridge_deferred.hlsl"
tool = os.environ.get("GLSLANG", "glslangValidator")


def samples(source, target):
    result = subprocess.run([tool, "-D", "-V", "-Os", "--hlsl-iomap", "--hlsl-offsets",
                             "-H", "-S", "frag", "-e", "PSGeometry", str(source),
                             "-o", str(target)], text=True, capture_output=True, check=True)
    loads, combined, counts = {}, {}, Counter()
    for line in result.stdout.splitlines():
        match = re.search(r"(\d+):\s+\d+\s+Load \d+\((\w+)\)", line)
        if match:
            loads[match[1]] = match[2]
        match = re.search(r"(\d+):\s+\d+\s+SampledImage (\d+) \d+", line)
        if match:
            combined[match[1]] = loads[match[2]]
        match = re.search(r"ImageSampleImplicitLod (\d+) ", line)
        if match:
            counts[combined[match[1]]] += 1
    return counts


with tempfile.TemporaryDirectory(prefix="kich-terrain-samples-") as folder:
    folder = Path(folder)
    generic = samples(shader, folder / "generic.spv")
    # These two textures occur only in the terrain normal path. Generic model
    # t8/t9 reads have other mutually exclusive branches and are counted below.
    assert generic["shadow4"] == generic["shadow5"] == 1, generic
    source = shader.read_text()
    declaration = "float4 material,color_scale,color_bias,sun_scale_bias,tree_wave,tree_wind;"
    assert source.count(declaration) == 1
    source = source.replace(declaration, declaration.replace("material,", "material_uniform,"))
    source = source.replace("Texture2D image0:",
                            "static const float4 material=float4(30720,7,11,1);\nTexture2D image0:", 1)
    specialized = folder / "terrain.hlsl"
    specialized.write_text(source)
    actual = samples(specialized, folder / "terrain.spv")
    expected = Counter({name: 1 for name in ("image0", "image1", "shadow0", "shadow1", "shadow2", "shadow3",
                                            "shadow4", "shadow5", "model_bump", "model_bump_aux", "terrain_lmap")})
    assert actual == expected, (actual, expected)
print("PASS: optimized terrain PSGeometry: one mask, four normal samples shared by normal/gloss, 11 total texture reads (glslang; not FXC/GPU timing)")
