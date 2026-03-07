#pragma once
// RenderAPI.h - Abstract rendering interface
// Supports OpenGL and DirectX 11
// Usage:
//   RenderAPI* api = RenderAPI::create(RenderAPI::Backend::DX11); // or OPENGL
//   api->init(hwnd, width, height);

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// -------------------------------------------------------
// HANDLES — type-safe IDs for GPU resources
// -------------------------------------------------------
struct BufferHandle { uint32_t id = 0; bool valid() const { return id != 0; } };
struct TextureHandle { uint32_t id = 0; bool valid() const { return id != 0; } };
struct ShaderHandle { uint32_t id = 0; bool valid() const { return id != 0; } };
struct MeshHandle { uint32_t id = 0; bool valid() const { return id != 0; } };

// -------------------------------------------------------
// VERTEX LAYOUTS
// -------------------------------------------------------
enum class VertexLayout {
    POS3,           // vec3 pos
    POS3_UV2,       // vec3 pos + vec2 uv
    POS3_NORM3_UV2, // standard static mesh
    SKINNED,        // pos3 + norm3 + uv2 + boneIDs4 + weights4
};

// -------------------------------------------------------
// TEXTURE FORMATS
// -------------------------------------------------------
enum class TextureFormat {
    RGBA8,
    DEPTH24,
    DEPTH32F,
};

// -------------------------------------------------------
// SHADER STAGE SOURCE
// Holds both GLSL and HLSL so we can switch at runtime
// -------------------------------------------------------
struct ShaderSource {
    const char* glsl_vert = nullptr;
    const char* glsl_frag = nullptr;
    const char* hlsl_vert = nullptr; // entry: "VSMain"
    const char* hlsl_frag = nullptr; // entry: "PSMain"
};

// -------------------------------------------------------
// MESH DESCRIPTOR — what we pass to createMesh()
// -------------------------------------------------------
struct MeshDesc {
    const float* vertices = nullptr;
    uint32_t        vertCount = 0;
    const uint32_t* indices = nullptr;
    uint32_t        idxCount = 0;
    VertexLayout    layout = VertexLayout::POS3_NORM3_UV2;
    bool            skinned = false;
};

// -------------------------------------------------------
// RENDER PASS DESCRIPTOR
// -------------------------------------------------------
struct RenderPassDesc {
    float        clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    bool         clearDepth = true;
    bool         clearColorBuf = true;
    TextureHandle renderTarget; // invalid = backbuffer
    TextureHandle depthTarget;  // invalid = default depth
    uint32_t     width = 0;   // 0 = use swapchain size
    uint32_t     height = 0;
};

// -------------------------------------------------------
// UNIFORM SETTER — type-erased uniform updates
// -------------------------------------------------------
struct UniformData {
    enum Type { FLOAT, VEC2, VEC3, VEC4, MAT3, MAT4, INT, SAMPLER } type;
    std::string     name;
    union {
        float   f;
        int     i;
        float   v2[2];
        float   v3[3];
        float   v4[4];
        float   m3[9];
        float   m4[16];
    };
    // For samplers
    TextureHandle texture;
    int           slot = 0;
};

inline UniformData makeUniform(const char* name, float v)
{
    UniformData u; u.type = UniformData::FLOAT; u.name = name; u.f = v; return u;
}

inline UniformData makeUniform(const char* name, int v)
{
    UniformData u; u.type = UniformData::INT; u.name = name; u.i = v; return u;
}

inline UniformData makeUniform(const char* name, const glm::vec3& v)
{
    UniformData u; u.type = UniformData::VEC3; u.name = name;
    memcpy(u.v3, glm::value_ptr(v), 12); return u;
}

inline UniformData makeUniform(const char* name, const glm::vec4& v)
{
    UniformData u; u.type = UniformData::VEC4; u.name = name;
    memcpy(u.v4, glm::value_ptr(v), 16); return u;
}

inline UniformData makeUniform(const char* name, const glm::mat4& m)
{
    UniformData u; u.type = UniformData::MAT4; u.name = name;
    memcpy(u.m4, glm::value_ptr(m), 64); return u;
}

inline UniformData makeUniform(const char* name, const glm::mat3& m)
{
    UniformData u; u.type = UniformData::MAT3; u.name = name;
    memcpy(u.m3, glm::value_ptr(m), 36); return u;
}

inline UniformData makeSampler(const char* name, TextureHandle tex, int slot)
{
    UniformData u; u.type = UniformData::SAMPLER; u.name = name;
    u.texture = tex; u.slot = slot; return u;
}

// -------------------------------------------------------
// DRAW CALL
// -------------------------------------------------------
struct DrawCall {
    MeshHandle  mesh;
    ShaderHandle shader;
    std::vector<UniformData> uniforms;
    // Skinning
    const float* boneMatrices = nullptr;
    int          boneCount = 0;
    // State
    bool         depthTest = true;
    bool         depthWrite = true;
    bool         cullBackface = true;
    bool         blendAlpha = false;
};

// -------------------------------------------------------
// ABSTRACT RENDER API
// -------------------------------------------------------
class RenderAPI
{
public:
    enum class Backend { OPENGL, DX11 };

    // Factory — creates the right backend
    static std::unique_ptr<RenderAPI> create(Backend backend);

    // Auto-select: DX11 on Windows, OpenGL elsewhere
    static std::unique_ptr<RenderAPI> createDefault()
    {
#ifdef _WIN32
        return create(Backend::DX11);
#else
        return create(Backend::OPENGL);
#endif
    }

    virtual ~RenderAPI() = default;

    // ── Lifecycle ───────────────────────────────────────
    // hwnd: Windows HWND for DX11, GLFWwindow* for OpenGL (cast to void*)
    virtual bool init(void* windowHandle, int width, int height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(int width, int height) = 0;
    virtual Backend backend() const = 0;
    virtual const char* backendName() const = 0;

    // ── Resource creation ───────────────────────────────
    virtual MeshHandle    createMesh(const MeshDesc& desc) = 0;
    virtual void          destroyMesh(MeshHandle h) = 0;

    virtual TextureHandle createTexture(int w, int h, TextureFormat fmt,
        const void* data = nullptr) = 0;
    virtual TextureHandle loadTexture(const std::string& path) = 0;
    virtual void          destroyTexture(TextureHandle h) = 0;

    virtual ShaderHandle  createShader(const ShaderSource& src) = 0;
    virtual void          destroyShader(ShaderHandle h) = 0;

    // ── Framebuffer / Offscreen ──────────────────────────
    // Creates a renderable texture (for shadow maps, FBOs, post-processing)
    virtual TextureHandle createRenderTarget(int w, int h, TextureFormat fmt) = 0;

    // ── Frame ────────────────────────────────────────────
    virtual void beginFrame() = 0;
    virtual void beginPass(const RenderPassDesc& pass) = 0;
    virtual void submit(const DrawCall& dc) = 0;
    virtual void endPass() = 0;
    virtual void endFrame() = 0;    // present / swap buffers

    // ── Utilities ────────────────────────────────────────
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void readPixels(int x, int y, int w, int h, void* out) = 0;

    // ── Immediate helpers (optional fast path) ───────────
    // Bind a shader and set a single uniform without a full DrawCall
    virtual void bindShader(ShaderHandle sh) = 0;
    virtual void setUniform(ShaderHandle sh, const UniformData& u) = 0;
    virtual void drawMesh(MeshHandle mesh) = 0;

    // Screen size
    int screenW() const { return _w; }
    int screenH() const { return _h; }

protected:
    int _w = 0, _h = 0;
};

// -------------------------------------------------------
// FORWARD DECLARATIONS of concrete implementations
// -------------------------------------------------------
class OpenGLRenderAPI;  // in RenderAPI_GL.h
class DX11RenderAPI;    // in RenderAPI_DX11.h  (Windows only)

// -------------------------------------------------------
// GLOBAL INSTANCE
// Usage:  gRenderAPI->submit(dc);
// -------------------------------------------------------
inline std::unique_ptr<RenderAPI> gRenderAPI;

inline void initRenderAPI(RenderAPI::Backend backend, void* window, int w, int h)
{
    gRenderAPI = RenderAPI::create(backend);
    if (!gRenderAPI->init(window, w, h)) {
        printf("[RenderAPI] Failed to init %s, falling back to OpenGL\n",
            gRenderAPI->backendName());
        gRenderAPI = RenderAPI::create(RenderAPI::Backend::OPENGL);
        gRenderAPI->init(window, w, h);
    }
    printf("[RenderAPI] Using: %s\n", gRenderAPI->backendName());
}