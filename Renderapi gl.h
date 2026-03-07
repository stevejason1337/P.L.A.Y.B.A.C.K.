#pragma once
// RenderAPI_GL.h - OpenGL 3.3 implementation of RenderAPI
// Include AFTER RenderAPI.h

#include "RenderAPI.h"
#include <glad/glad.h>
#include <unordered_map>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION_GUARD
#ifndef STBI_INCLUDE_STB_IMAGE_H
#include "stb_image.h"
#endif

class OpenGLRenderAPI : public RenderAPI
{
public:
    Backend backend()    const override { return Backend::OPENGL; }
    const char* backendName() const override { return "OpenGL 3.3"; }

    // ── Init ────────────────────────────────────────────
    bool init(void* windowHandle, int w, int h) override
    {
        _w = w; _h = h;
        // Glad должен быть уже загружен через glfwMakeContextCurrent
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        printf("[GL] OpenGL %s\n", glGetString(GL_VERSION));
        return true;
    }

    void shutdown() override
    {
        for (auto& [id, vao] : _meshes) { glDeleteVertexArrays(1, &vao.vao); glDeleteBuffers(1, &vao.vbo); glDeleteBuffers(1, &vao.ebo); }
        for (auto& [id, tex] : _textures) glDeleteTextures(1, &tex);
        for (auto& [id, sh] : _shaders)  glDeleteProgram(sh);
        _meshes.clear(); _textures.clear(); _shaders.clear();
    }

    void resize(int w, int h) override
    {
        _w = w; _h = h;
        glViewport(0, 0, w, h);
    }

    // ── Mesh ────────────────────────────────────────────
    MeshHandle createMesh(const MeshDesc& desc) override
    {
        GLMesh m;
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ebo);

        glBindVertexArray(m.vao);

        // Vertex buffer
        uint32_t stride = _strideForLayout(desc.layout);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, desc.vertCount * stride * sizeof(float),
            desc.vertices, GL_STATIC_DRAW);

        // Index buffer
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, desc.idxCount * sizeof(uint32_t),
            desc.indices, GL_STATIC_DRAW);

        // Attributes
        _setupAttribs(desc.layout, stride);

        glBindVertexArray(0);
        m.indexCount = desc.idxCount;
        m.skinned = desc.skinned;

        uint32_t id = ++_nextId;
        _meshes[id] = m;
        return { id };
    }

    void destroyMesh(MeshHandle h) override
    {
        auto it = _meshes.find(h.id);
        if (it == _meshes.end()) return;
        glDeleteVertexArrays(1, &it->second.vao);
        glDeleteBuffers(1, &it->second.vbo);
        glDeleteBuffers(1, &it->second.ebo);
        _meshes.erase(it);
    }

    // ── Texture ─────────────────────────────────────────
    TextureHandle createTexture(int w, int h, TextureFormat fmt,
        const void* data) override
    {
        GLuint tex; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        GLenum internalFmt = GL_RGBA8, srcFmt = GL_RGBA, type = GL_UNSIGNED_BYTE;
        if (fmt == TextureFormat::DEPTH24) { internalFmt = GL_DEPTH_COMPONENT24; srcFmt = GL_DEPTH_COMPONENT; type = GL_UNSIGNED_INT; }
        if (fmt == TextureFormat::DEPTH32F) { internalFmt = GL_DEPTH_COMPONENT32F; srcFmt = GL_DEPTH_COMPONENT; type = GL_FLOAT; }
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, srcFmt, type, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uint32_t id = ++_nextId;
        _textures[id] = tex;
        return { id };
    }

    TextureHandle loadTexture(const std::string& path) override
    {
        // Check cache
        auto it = _texCache.find(path);
        if (it != _texCache.end()) return it->second;

        int w, h, ch;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            printf("[GL] Texture not found: %s\n", path.c_str());
            return { 0 };
        }
        TextureHandle th = createTexture(w, h, TextureFormat::RGBA8, pixels);
        stbi_image_free(pixels);

        // Generate mipmaps for better quality
        GLuint rawTex = _textures[th.id];
        glBindTexture(GL_TEXTURE_2D, rawTex);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        _texCache[path] = th;
        printf("[GL] Loaded texture: %s (%dx%d)\n", path.c_str(), w, h);
        return th;
    }

    void destroyTexture(TextureHandle h) override
    {
        auto it = _textures.find(h.id);
        if (it == _textures.end()) return;
        glDeleteTextures(1, &it->second);
        _textures.erase(it);
    }

    TextureHandle createRenderTarget(int w, int h, TextureFormat fmt) override
    {
        TextureHandle th = createTexture(w, h, fmt, nullptr);

        // Attach to a new FBO
        GLuint fbo; glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        GLuint rawTex = _textures[th.id];
        if (fmt == TextureFormat::RGBA8) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, rawTex, 0);
        }
        else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D, rawTex, 0);
            glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        _fbos[th.id] = fbo;
        return th;
    }

    // ── Shader ──────────────────────────────────────────
    ShaderHandle createShader(const ShaderSource& src) override
    {
        GLuint prog = _compileProgram(src.glsl_vert, src.glsl_frag);
        if (!prog) return { 0 };
        uint32_t id = ++_nextId;
        _shaders[id] = prog;
        return { id };
    }

    void destroyShader(ShaderHandle h) override
    {
        auto it = _shaders.find(h.id);
        if (it == _shaders.end()) return;
        glDeleteProgram(it->second);
        _shaders.erase(it);
    }

    // ── Frame ────────────────────────────────────────────
    void beginFrame() override
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void beginPass(const RenderPassDesc& pass) override
    {
        // Bind FBO if render target provided
        if (pass.renderTarget.valid()) {
            auto it = _fbos.find(pass.renderTarget.id);
            if (it != _fbos.end())
                glBindFramebuffer(GL_FRAMEBUFFER, it->second);
        }
        else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        uint32_t w = pass.width ? pass.width : _w;
        uint32_t h = pass.height ? pass.height : _h;
        glViewport(0, 0, w, h);

        GLbitfield mask = 0;
        if (pass.clearColorBuf) {
            glClearColor(pass.clearColor[0], pass.clearColor[1],
                pass.clearColor[2], pass.clearColor[3]);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (pass.clearDepth) mask |= GL_DEPTH_BUFFER_BIT;
        if (mask) glClear(mask);

        glEnable(GL_DEPTH_TEST);
    }

    void submit(const DrawCall& dc) override
    {
        auto shIt = _shaders.find(dc.shader.id);
        auto mhIt = _meshes.find(dc.mesh.id);
        if (shIt == _shaders.end() || mhIt == _meshes.end()) return;

        GLuint prog = shIt->second;
        glUseProgram(prog);

        // Depth / cull state
        dc.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        dc.depthWrite ? glDepthMask(GL_TRUE) : glDepthMask(GL_FALSE);
        dc.cullBackface ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        if (dc.blendAlpha) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
        else { glDisable(GL_BLEND); }

        // Set uniforms
        for (auto& u : dc.uniforms)
            _setUniformGL(prog, u);

        // Bone matrices
        if (dc.boneMatrices && dc.boneCount > 0) {
            int loc = glGetUniformLocation(prog, "bones");
            if (loc >= 0) glUniformMatrix4fv(loc, dc.boneCount, GL_FALSE, dc.boneMatrices);
            int sloc = glGetUniformLocation(prog, "skinned");
            if (sloc >= 0) glUniform1i(sloc, 1);
        }
        else {
            int sloc = glGetUniformLocation(prog, "skinned");
            if (sloc >= 0) glUniform1i(sloc, 0);
        }

        // Draw
        const GLMesh& m = mhIt->second;
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void endPass() override { /* nothing needed in GL */ }

    void endFrame() override
    {
        // glfwSwapBuffers called by main loop
    }

    void setViewport(int x, int y, int w, int h) override
    {
        glViewport(x, y, w, h);
    }

    void readPixels(int x, int y, int w, int h, void* out) override
    {
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
    }

    void bindShader(ShaderHandle sh) override
    {
        auto it = _shaders.find(sh.id);
        if (it != _shaders.end()) { glUseProgram(it->second); _boundShader = it->second; }
    }

    void setUniform(ShaderHandle sh, const UniformData& u) override
    {
        auto it = _shaders.find(sh.id);
        if (it != _shaders.end()) _setUniformGL(it->second, u);
    }

    void drawMesh(MeshHandle mesh) override
    {
        auto it = _meshes.find(mesh.id);
        if (it == _meshes.end()) return;
        glBindVertexArray(it->second.vao);
        glDrawElements(GL_TRIANGLES, it->second.indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    // ── GL-specific: get raw GL IDs (for legacy code) ──
    GLuint getRawTexture(TextureHandle h) const
    {
        auto it = _textures.find(h.id); return it != _textures.end() ? it->second : 0;
    }

    GLuint getRawShader(ShaderHandle h) const
    {
        auto it = _shaders.find(h.id); return it != _shaders.end() ? it->second : 0;
    }

    GLuint getRawVAO(MeshHandle h) const
    {
        auto it = _meshes.find(h.id); return it != _meshes.end() ? it->second.vao : 0;
    }

    GLuint getRawFBO(TextureHandle h) const
    {
        auto it = _fbos.find(h.id); return it != _fbos.end() ? it->second : 0;
    }

private:
    struct GLMesh { GLuint vao = 0, vbo = 0, ebo = 0; int indexCount = 0; bool skinned = false; };

    std::unordered_map<uint32_t, GLMesh>  _meshes;
    std::unordered_map<uint32_t, GLuint>  _textures;
    std::unordered_map<uint32_t, GLuint>  _fbos;
    std::unordered_map<uint32_t, GLuint>  _shaders;
    std::unordered_map<std::string, TextureHandle> _texCache;
    uint32_t _nextId = 0;
    GLuint   _boundShader = 0;

    static uint32_t _strideForLayout(VertexLayout l)
    {
        switch (l) {
        case VertexLayout::POS3:           return 3;
        case VertexLayout::POS3_UV2:       return 5;
        case VertexLayout::POS3_NORM3_UV2: return 8;
        case VertexLayout::SKINNED:        return 16; // 3+3+2+4+4
        }
        return 8;
    }

    static void _setupAttribs(VertexLayout l, uint32_t stride)
    {
        const uint32_t F = sizeof(float);
        switch (l) {
        case VertexLayout::POS3:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * F, (void*)0);
            glEnableVertexAttribArray(0);
            break;
        case VertexLayout::POS3_UV2:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * F, (void*)0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride * F, (void*)(3 * F));
            glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
            break;
        case VertexLayout::POS3_NORM3_UV2:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * F, (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride * F, (void*)(3 * F));
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride * F, (void*)(6 * F));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            break;
        case VertexLayout::SKINNED:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * F, (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride * F, (void*)(3 * F));
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride * F, (void*)(6 * F));
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride * F, (void*)(8 * F));
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride * F, (void*)(12 * F));
            for (int i = 0; i < 5; i++) glEnableVertexAttribArray(i);
            break;
        }
    }

    static GLuint _compileShader(GLenum type, const char* src)
    {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        int ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(sh, 512, nullptr, log);
            printf("[GL] Shader error: %s\n", log);
        }
        return sh;
    }

    static GLuint _compileProgram(const char* vert, const char* frag)
    {
        if (!vert || !frag) return 0;
        GLuint vs = _compileShader(GL_VERTEX_SHADER, vert);
        GLuint fs = _compileShader(GL_FRAGMENT_SHADER, frag);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs);
        glLinkProgram(prog);
        int ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log); printf("[GL] Link error: %s\n", log); }
        glDeleteShader(vs); glDeleteShader(fs);
        return prog;
    }

    static void _setUniformGL(GLuint prog, const UniformData& u)
    {
        int loc = glGetUniformLocation(prog, u.name.c_str());
        if (loc < 0) return;
        switch (u.type) {
        case UniformData::FLOAT:   glUniform1f(loc, u.f); break;
        case UniformData::INT:     glUniform1i(loc, u.i); break;
        case UniformData::VEC2:    glUniform2fv(loc, 1, u.v2); break;
        case UniformData::VEC3:    glUniform3fv(loc, 1, u.v3); break;
        case UniformData::VEC4:    glUniform4fv(loc, 1, u.v4); break;
        case UniformData::MAT3:    glUniformMatrix3fv(loc, 1, GL_FALSE, u.m3); break;
        case UniformData::MAT4:    glUniformMatrix4fv(loc, 1, GL_FALSE, u.m4); break;
        case UniformData::SAMPLER:
            // bind already done outside — just set slot index
            glUniform1i(loc, u.slot);
            break;
        }
    }
};

// Factory implementation for OpenGL
// (DX11 factory is in RenderAPI_DX11.h)
inline std::unique_ptr<RenderAPI> RenderAPI::create(Backend backend)
{
    if (backend == Backend::OPENGL)
        return std::make_unique<OpenGLRenderAPI>();
#ifdef _WIN32
    // DX11 — include RenderAPI_DX11.h for full impl
    // For now fall back to OpenGL if DX11 not compiled in
#ifdef RENDER_DX11_INCLUDED
    if (backend == Backend::DX11)
        return std::make_unique<DX11RenderAPI>();
#else
    if (backend == Backend::DX11) {
        printf("[RenderAPI] DX11 not compiled in, using OpenGL\n");
        return std::make_unique<OpenGLRenderAPI>();
    }
#endif
#endif
    return std::make_unique<OpenGLRenderAPI>();
}