#pragma once
// RenderAPI.h - Abstract rendering interface
// Supports OpenGL and DirectX 11

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// -------------------------------------------------------
// HANDLES
// -------------------------------------------------------
struct BufferHandle { uint32_t id = 0; bool valid() const { return id != 0; } };
struct TextureHandle { uint32_t id = 0; bool valid() const { return id != 0; } };
struct ShaderHandle { uint32_t id = 0; bool valid() const { return id != 0; } };
struct MeshHandle { uint32_t id = 0; bool valid() const { return id != 0; } };

// -------------------------------------------------------
// VERTEX LAYOUTS
// -------------------------------------------------------
enum class VertexLayout {
    POS3,            // vec3 pos
    POS3_UV2,        // vec3 pos + vec2 uv
    POS3_NORM3_UV2,  // standard static mesh  (stride=8)
    SKINNED,         // pos3+norm3+uv2+boneIDs4+weights4 (stride=16)
};

// -------------------------------------------------------
// TEXTURE FORMATS
// -------------------------------------------------------
enum class TextureFormat { RGBA8, DEPTH24, DEPTH32F };

// -------------------------------------------------------
// SHADER SOURCE
// Provide both GLSL (GL) and HLSL (DX11). The HLSL string
// must contain both VSMain and PSMain entry points.
// -------------------------------------------------------
struct ShaderSource {
    const char* glsl_vert = nullptr;
    const char* glsl_frag = nullptr;
    const char* hlsl_combined = nullptr; // VSMain + PSMain in one string
};

// -------------------------------------------------------
// SCENE UNIFORMS  —  single source of truth
//
// GL reads named uniforms from this struct.
// DX11 maps this struct directly into cbuffer slot b0.
//
// DX11 cbuffer b0 layout (must stay in sync with HLSL):
//   mat4  view          offset   0  (64b)
//   mat4  projection    offset  64  (64b)
//   mat4  model         offset 128  (64b)
//   mat4  normalMatrix  offset 192  (64b)
//   vec3  lightDir      offset 256  (12b) + float _pad0
//   vec3  fogColor      offset 272  (12b) + float fogStart
//   float fogEnd        offset 288  + float[3] _pad1
//   vec3  baseColor     offset 304  (12b) + int hasTexture
//   total = 320 bytes → pad to 320 (multiple of 16 ✓)
// -------------------------------------------------------
struct alignas(16) SceneUniforms {
    glm::mat4 view{ 1.f };   //   0
    glm::mat4 projection{ 1.f };   //  64
    glm::mat4 model{ 1.f };   // 128
    glm::mat4 normalMatrix{ 1.f };   // 192
    glm::vec3 lightDir{ 0.4f,-1.f,0.3f }; float _pad0 = 0.f; // 256
    glm::vec3 fogColor{ 0.68f,0.65f,0.60f }; float fogStart = 15.f; // 272
    float fogEnd = 60.f; float _pad1[3] = {};                   // 288
    glm::vec3 baseColor{ 0.75f,0.72f,0.65f }; int hasTexture = 0; // 304
};
static_assert(sizeof(SceneUniforms) == 320, "SceneUniforms size mismatch");

// -------------------------------------------------------
// MESH DESCRIPTOR
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
    float         clearColor[4] = { 0.68f,0.65f,0.60f,1.f };
    bool          clearDepth = true;
    bool          clearColorBuf = true;
    TextureHandle renderTarget;
    TextureHandle depthTarget;
    uint32_t      width = 0;
    uint32_t      height = 0;
};

// -------------------------------------------------------
// UNIFORM DATA  (legacy per-draw overrides)
// -------------------------------------------------------
struct UniformData {
    enum Type { FLOAT, VEC2, VEC3, VEC4, MAT3, MAT4, INT, SAMPLER } type;
    std::string name;
    union { float f; int i; float v2[2]; float v3[3]; float v4[4]; float m3[9]; float m4[16]; };
    TextureHandle texture;
    int           slot = 0;
};
inline UniformData makeUniform(const char* n, float v) { UniformData u; u.type = UniformData::FLOAT; u.name = n; u.f = v; return u; }
inline UniformData makeUniform(const char* n, int   v) { UniformData u; u.type = UniformData::INT;  u.name = n; u.i = v; return u; }
inline UniformData makeUniform(const char* n, const glm::vec3& v) { UniformData u; u.type = UniformData::VEC3; u.name = n; memcpy(u.v3, glm::value_ptr(v), 12); return u; }
inline UniformData makeUniform(const char* n, const glm::vec4& v) { UniformData u; u.type = UniformData::VEC4; u.name = n; memcpy(u.v4, glm::value_ptr(v), 16); return u; }
inline UniformData makeUniform(const char* n, const glm::mat4& m) { UniformData u; u.type = UniformData::MAT4; u.name = n; memcpy(u.m4, glm::value_ptr(m), 64); return u; }
inline UniformData makeUniform(const char* n, const glm::mat3& m) { UniformData u; u.type = UniformData::MAT3; u.name = n; memcpy(u.m3, glm::value_ptr(m), 36); return u; }
inline UniformData makeSampler(const char* n, TextureHandle t, int s) { UniformData u; u.type = UniformData::SAMPLER; u.name = n; u.texture = t; u.slot = s; return u; }

// -------------------------------------------------------
// DRAW CALL
// -------------------------------------------------------
struct DrawCall {
    MeshHandle               mesh;
    ShaderHandle             shader;
    std::vector<UniformData> uniforms;       // legacy overrides
    const SceneUniforms* sceneUniforms = nullptr; // preferred fast path
    TextureHandle            diffuseTexture;   // bound to slot 0
    const float* boneMatrices = nullptr;
    int                      boneCount = 0;
    bool depthTest = true;
    bool depthWrite = true;
    bool cullBackface = true;
    bool blendAlpha = false;
};

// -------------------------------------------------------
// ABSTRACT RENDER API
// -------------------------------------------------------
class RenderAPI
{
public:
    enum class Backend { OPENGL, DX11 };

    static std::unique_ptr<RenderAPI> create(Backend backend);
    static std::unique_ptr<RenderAPI> createDefault()
    {
#ifdef _WIN32
        return create(Backend::DX11);
#else
        return create(Backend::OPENGL);
#endif
    }

    virtual ~RenderAPI() = default;

    virtual bool        init(void* windowHandle, int w, int h) = 0;
    virtual void        shutdown() = 0;
    virtual void        resize(int w, int h) = 0;
    virtual Backend     backend()     const = 0;
    virtual const char* backendName() const = 0;

    virtual MeshHandle    createMesh(const MeshDesc& desc) = 0;
    virtual void          destroyMesh(MeshHandle h) = 0;
    virtual TextureHandle createTexture(int w, int h, TextureFormat fmt, const void* data = nullptr) = 0;
    virtual TextureHandle loadTexture(const std::string& path) = 0;
    virtual void          destroyTexture(TextureHandle h) = 0;
    virtual ShaderHandle  createShader(const ShaderSource& src) = 0;
    virtual void          destroyShader(ShaderHandle h) = 0;
    virtual TextureHandle createRenderTarget(int w, int h, TextureFormat fmt) = 0;

    virtual void beginFrame() = 0;
    virtual void beginPass(const RenderPassDesc& pass) = 0;
    virtual void submit(const DrawCall& dc) = 0;
    virtual void endPass() = 0;
    virtual void endFrame() = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void readPixels(int x, int y, int w, int h, void* out) = 0;
    virtual void bindShader(ShaderHandle sh) = 0;
    virtual void setUniform(ShaderHandle sh, const UniformData& u) = 0;
    virtual void drawMesh(MeshHandle mesh) = 0;

    int screenW() const { return _w; }
    int screenH() const { return _h; }
protected:
    int _w = 0, _h = 0;
};

class OpenGLRenderAPI;
class DX11RenderAPI;

inline std::unique_ptr<RenderAPI> gRenderAPI;
inline void initRenderAPI(RenderAPI::Backend backend, void* window, int w, int h)
{
    gRenderAPI = RenderAPI::create(backend);
    if (!gRenderAPI->init(window, w, h)) {
        printf("[RenderAPI] Failed to init %s, falling back to OpenGL\n", gRenderAPI->backendName());
        gRenderAPI = RenderAPI::create(RenderAPI::Backend::OPENGL);
        gRenderAPI->init(window, w, h);
    }
    printf("[RenderAPI] Using: %s\n", gRenderAPI->backendName());
}