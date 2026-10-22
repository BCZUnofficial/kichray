#ifndef KICH_DS_GRAPH_SNAPSHOT_PROVENANCE_H
#define KICH_DS_GRAPH_SNAPSHOT_PROVENANCE_H

#include "../RenderSnapshot.h"

// Source-side overrides must not leak particle flags/UVs into the next draw.
// This saves only telemetry provenance; it never touches renderer state.
class RenderSnapshotSourceScope
{
    bool active;
    RenderSnapshotProvenance saved;
public:
    RenderSnapshotSourceScope() : active(g_render_snapshot_active)
    {
        if (active) RenderSnapshot_GetProvenance(&saved);
    }
    ~RenderSnapshotSourceScope()
    {
        if (active && g_render_snapshot_active) RenderSnapshot_SetProvenance(&saved);
    }
private:
    RenderSnapshotSourceScope(const RenderSnapshotSourceScope&);
    RenderSnapshotSourceScope& operator=(const RenderSnapshotSourceScope&);
};

inline void RenderSnapshotHashByte(uint64_t& hash, unsigned char value)
{
    hash ^= value;
    hash *= 1099511628211ULL;
}

inline void RenderSnapshotHashText(uint64_t& hash, const char* value)
{
    if (value) while (*value) RenderSnapshotHashByte(hash, static_cast<unsigned char>(*value++));
    RenderSnapshotHashByte(hash, 0);
}

inline void RenderSnapshotHashNumber(uint64_t& hash, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) RenderSnapshotHashByte(hash, static_cast<unsigned char>(value >> (i * 8)));
}

inline void RenderSnapshotNamedMaterial(const char* shader, const char* texture, char (&id)[160])
{
    uint64_t hash = 14695981039346656037ULL;
    RenderSnapshotHashText(hash, shader);
    RenderSnapshotHashText(hash, texture);
    sprintf_s(id, "source:s=%.50s;t=%.50s;fnv64=%08x%08x",
        shader ? shader : "UNAVAILABLE", texture ? texture : "UNAVAILABLE",
        static_cast<unsigned>(hash >> 32), static_cast<unsigned>(hash));
}

// The original blender name is not retained by Shader. Name this identity for
// the evidence actually available: all stored shader names and texture stages.
// No process address, GPU readback, or texture-name semantic heuristic is used.
inline void RenderSnapshotShaderMaterial(const Shader* shader, char (&id)[160])
{
    if (!shader)
    {
        strcpy_s(id, "UNAVAILABLE_SHADER_TEXTURE_SET");
        return;
    }
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned element = 0; element < 6; ++element)
    {
        RenderSnapshotHashNumber(hash, element);
        const ShaderElement* source = shader->E[element]._get();
        RenderSnapshotHashNumber(hash, source ? static_cast<uint32_t>(source->passes.size()) : 0);
        if (!source) continue;
        for (unsigned pass = 0; pass < source->passes.size(); ++pass)
        {
            const SPass* p = source->passes[pass]._get();
            RenderSnapshotHashText(hash, p && p->vs ? p->vs->cName.c_str() : "NONE");
            RenderSnapshotHashText(hash, p && p->ps ? p->ps->cName.c_str() : "NONE");
            const STextureList* textures = p ? p->T._get() : 0;
            RenderSnapshotHashNumber(hash, textures ? static_cast<uint32_t>(textures->size()) : 0);
            if (!textures) continue;
            for (unsigned texture = 0; texture < textures->size(); ++texture)
            {
                RenderSnapshotHashNumber(hash, (*textures)[texture].first);
                RenderSnapshotHashText(hash, (*textures)[texture].second ?
                    (*textures)[texture].second->cName.c_str() : "NONE");
            }
        }
    }
    sprintf_s(id, "shader_texture_set_fnv1a64:%08x%08x",
        static_cast<unsigned>(hash >> 32), static_cast<unsigned>(hash));
}

#endif
