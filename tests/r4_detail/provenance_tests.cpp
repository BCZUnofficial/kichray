#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

// Only the resource containers are faked. The identity/scoping helper and
// recorder below are production code, with independent live resource objects.
template <size_t N> int sprintf_s(char (&out)[N], const char* format, ...)
{
    va_list args; va_start(args, format);
    const int result = vsnprintf(out, N, format, args);
    va_end(args); return result;
}
template <size_t N> void strcpy_s(char (&out)[N], const char* source)
{
    snprintf(out, N, "%s", source);
}
template <class T> struct Ref
{
    T* ptr;
    Ref(T* value = NULL) : ptr(value) {}
    T* _get() const { return ptr; }
    T* operator->() const { return ptr; }
    operator bool() const { return ptr != NULL; }
};
struct NamedResource { std::string cName; explicit NamedResource(const char* n) : cName(n) {} };
typedef std::vector<std::pair<uint32_t, Ref<NamedResource> > > STextureList;
struct SPass { Ref<NamedResource> vs, ps; Ref<STextureList> T; };
struct ShaderElement { std::vector<Ref<SPass> > passes; };
struct Shader { Ref<ShaderElement> E[6]; };
#include "../../xr_3da/xrRender/r__snapshot_provenance.h"

static unsigned checks = 0;
static void Require(bool condition, const char* reason)
{
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "output directory required");
        NamedResource vs1("model_vs"), ps1("model_ps"), texture1("stone");
        NamedResource vs2("model_vs"), ps2("model_ps"), texture2("stone");
        STextureList textures1, textures2;
        textures1.push_back(std::make_pair(0u, Ref<NamedResource>(&texture1)));
        textures2.push_back(std::make_pair(0u, Ref<NamedResource>(&texture2)));
        SPass pass1, pass2;
        pass1.vs = &vs1; pass1.ps = &ps1; pass1.T = &textures1;
        pass2.vs = &vs2; pass2.ps = &ps2; pass2.T = &textures2;
        ShaderElement element1, element2;
        element1.passes.push_back(&pass1); element2.passes.push_back(&pass2);
        Shader shader1, shader2; shader1.E[0] = &element1; shader2.E[0] = &element2;
        char key1[160], key2[160];
        RenderSnapshotShaderMaterial(&shader1, key1);
        RenderSnapshotShaderMaterial(&shader2, key2);
        Require(strcmp(key1, key2) == 0, "material identity must not contain process addresses");
        texture2.cName = "wood";
        RenderSnapshotShaderMaterial(&shader2, key2);
        Require(strcmp(key1, key2) != 0, "original texture resource affects identity");
        texture2.cName = "stone"; textures2[0].first = 1;
        RenderSnapshotShaderMaterial(&shader2, key2);
        Require(strcmp(key1, key2) != 0, "texture stage affects identity");
        textures2[0].first = 0; vs2.cName = "other_vs";
        RenderSnapshotShaderMaterial(&shader2, key2);
        Require(strcmp(key1, key2) != 0, "original shader resource affects identity");
        vs2.cName = "model_vs"; shader2.E[5] = &element2;
        RenderSnapshotShaderMaterial(&shader2, key2);
        Require(strcmp(key1, key2) != 0, "additional shader elements affect identity");
        RenderSnapshotShaderMaterial(NULL, key2);
        Require(strcmp(key2, "UNAVAILABLE_SHADER_TEXTURE_SET") == 0, "missing identity is explicit");
        const std::string long_name(200, 'x');
        RenderSnapshotNamedMaterial("particle", (long_name + "a").c_str(), key1);
        RenderSnapshotNamedMaterial("particle", (long_name + "b").c_str(), key2);
        Require(strcmp(key1, key2) != 0, "long source identifiers retain suffix identity after display truncation");

        RenderSnapshotInfo info = {};
        snprintf(info.directory, sizeof(info.directory), "%s", argv[1]);
        RenderSnapshot_Request(); RenderSnapshot_Begin(info);
        RenderSnapshot_Pass("FORWARD_TRANSPARENCY");
        RenderSnapshot_Context("STATIC", "stone_mesh", "stone_material");
        RenderSnapshot_Object("STATIC", "stone_mesh", "stone_material", "SOURCE", 0);
        RenderSnapshot_Queue("DSGRAPH_SORTED");
        RenderSnapshot_PassMask(RENDER_SNAPSHOT_PASS_FORWARD_TRANSPARENCY);
        RenderSnapshotProvenance original = {}, current = {};
        RenderSnapshot_GetProvenance(&original);
        {
            RenderSnapshotSourceScope scope;
            RenderSnapshot_Object("PARTICLE", "effect_definition", key1, "PARTICLE_SOURCE", RENDER_SNAPSHOT_PARTICLE);
            const float uv[] = {.25f, .5f, .5f, .75f};
            RenderSnapshot_UV(uv, 2, "CPU_GENERATED_VERTICES");
            RenderSnapshot_GetProvenance(&current);
            Require(current.flags == RENDER_SNAPSHOT_PARTICLE && current.uv_count == 2, "source override records explicit particle flag and UVs");
            Require(strcmp(current.queue, "DSGRAPH_SORTED") == 0 && current.pass_mask == original.pass_mask,
                "particle source preserves parent queue and pass provenance");
            {
                RenderSnapshotSourceScope nested;
                RenderSnapshot_Object("STATIC", "nested", "material", "SOURCE", 0);
            }
            RenderSnapshot_GetProvenance(&current);
            Require(current.flags == RENDER_SNAPSHOT_PARTICLE && current.uv_count == 2, "nested source scope restores outer source");
        }
        RenderSnapshot_GetProvenance(&current);
        Require(memcmp(&current, &original, sizeof(current)) == 0, "particle flags UVs and identity cannot leak to later generic draws");
        RenderSnapshot_End("COMPLETE");
        {
            RenderSnapshotSourceScope inactive;
            Require(!g_render_snapshot_active, "inactive source scope cannot start capture");
        }
        printf("G3a.1 shared source provenance: %u checks passed\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
