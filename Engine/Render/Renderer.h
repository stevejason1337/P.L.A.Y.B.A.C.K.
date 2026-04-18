#pragma once
// ============================================================
//  Engine/Render/Renderer.h  —  ДВИЖОК, не трогаешь
//  OpenGL рендерер — синглтон.
//  Заменяет старый DX11 Renderer полностью.
//  Движок ничего не знает про Player/Enemy/Weapon.
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>

// Forward declare — чтобы не создавать circular includes
class Scene;
class MeshRenderer;
class AnimatedModel;

// ── Данные камеры ─────────────────────────────────────────
struct CameraData {
    glm::vec3 position  = glm::vec3(0.f, 2.f,  5.f);
    glm::vec3 front     = glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 up        = glm::vec3(0.f, 1.f,  0.f);
    float     fov       = 75.f;
    float     nearPlane = 0.1f;
    float     farPlane  = 500.f;

    glm::mat4 getView() const {
        return glm::lookAt(position, position + front, up);
    }
    glm::mat4 getProjection(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    }
};

// ── Данные солнечного света ───────────────────────────────
struct SunLight {
    glm::vec3 direction = glm::normalize(glm::vec3(-0.4f, -1.f, -0.6f));
    glm::vec3 color     = glm::vec3(1.f, 0.95f, 0.85f);
    float     intensity = 1.2f;
};

// ── Главный рендерер ──────────────────────────────────────
class Renderer {
public:
    static Renderer& get() {
        static Renderer instance;
        return instance;
    }

    // Публичные данные — PlayerController и другие пишут сюда
    CameraData  camera;
    SunLight    sun;
    glm::vec4   clearColor = glm::vec4(0.45f, 0.6f, 0.75f, 1.f);

    // ── Инициализация ─────────────────────────────────────
    bool init(GLFWwindow* win) {
        window = win;

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Renderer] GLAD failed to load OpenGL\n";
            return false;
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        std::cout << "[Renderer] OpenGL " << glGetString(GL_VERSION)
                  << " | " << glGetString(GL_RENDERER) << "\n";

        staticShader = _compileStaticShader();
        animShader   = _compileAnimShader();

        if (!staticShader || !animShader) {
            std::cerr << "[Renderer] Shader compilation failed\n";
            return false;
        }

        return true;
    }

    // ── Начало кадра ──────────────────────────────────────
    void beginFrame() {
        glfwGetFramebufferSize(window, &screenW, &screenH);
        if (screenW == 0 || screenH == 0) return;

        glViewport(0, 0, screenW, screenH);
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // ── Рендер всей сцены ─────────────────────────────────
    // Вызывается из Engine::run() — игра не вызывает напрямую
    void drawScene(Scene& scene);   // реализация ниже, после includes

    // ── Конец кадра ───────────────────────────────────────
    void endFrame() {
        glfwSwapBuffers(window);
    }

    // ── Геттеры ───────────────────────────────────────────
    GLFWwindow* getWindow()  const { return window;  }
    int         getWidth()   const { return screenW; }
    int         getHeight()  const { return screenH; }
    float       getAspect()  const {
        return screenH > 0 ? (float)screenW / (float)screenH : 1.f;
    }

    unsigned int getStaticShader() const { return staticShader; }
    unsigned int getAnimShader()   const { return animShader;   }

    // ── Утилиты установки uniform'ов ──────────────────────
    static void setMat4(unsigned int sh, const char* name, const glm::mat4& m) {
        glUniformMatrix4fv(glGetUniformLocation(sh, name), 1, GL_FALSE, glm::value_ptr(m));
    }
    static void setVec3(unsigned int sh, const char* name, const glm::vec3& v) {
        glUniform3fv(glGetUniformLocation(sh, name), 1, glm::value_ptr(v));
    }
    static void setInt(unsigned int sh, const char* name, int v) {
        glUniform1i(glGetUniformLocation(sh, name), v);
    }
    static void setFloat(unsigned int sh, const char* name, float v) {
        glUniform1f(glGetUniformLocation(sh, name), v);
    }

private:
    GLFWwindow*  window       = nullptr;
    unsigned int staticShader = 0;
    unsigned int animShader   = 0;
    int          screenW      = 1280;
    int          screenH      = 720;

    Renderer()  = default;
    ~Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ── Шейдер для статических мешей ─────────────────────
    static unsigned int _compileStaticShader() {
        const char* vert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aTangent;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos   = model * vec4(aPos, 1.0);
    vFragPos        = worldPos.xyz;
    vNormal         = mat3(transpose(inverse(model))) * aNormal;
    vUV             = aUV;
    gl_Position     = projection * view * worldPos;
}
)";
        const char* frag = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D texture_diffuse0;
uniform int       hasTexture;
uniform vec3      lightDir;
uniform vec3      lightColor;
uniform vec3      viewPos;

void main() {
    vec3 base = (hasTexture > 0)
        ? texture(texture_diffuse0, vUV).rgb
        : vec3(0.75, 0.73, 0.70);

    vec3 n        = normalize(vNormal);
    vec3 l        = normalize(-lightDir);
    float diff    = max(dot(n, l), 0.0);

    // Hemispheric ambient (небо + земля)
    vec3 sky      = vec3(0.55, 0.70, 0.90) * 0.25;
    vec3 ground   = vec3(0.40, 0.35, 0.28) * 0.12;
    vec3 ambient  = mix(ground, sky, n.y * 0.5 + 0.5);

    vec3 col      = base * (ambient + lightColor * diff * 0.85);

    // Простой туман
    float dist    = length(viewPos - vFragPos);
    float fog     = clamp((dist - 15.0) / (60.0 - 15.0), 0.0, 1.0);
    vec3 fogColor = vec3(0.68, 0.65, 0.60);
    col           = mix(col, fogColor, fog);

    FragColor = vec4(col, 1.0);
}
)";
        return _linkProgram(vert, frag);
    }

    // ── Шейдер для анимированных мешей (GPU skinning) ────
    static unsigned int _compileAnimShader() {
        const char* vert = R"(
#version 330 core
layout(location=0) in vec3  aPos;
layout(location=1) in vec3  aNormal;
layout(location=2) in vec2  aUV;
layout(location=3) in ivec4 boneIDs;
layout(location=4) in vec4  weights;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 finalBonesMatrices[100];

void main() {
    vec4 skinnedPos = vec4(0.0);
    vec3 skinnedNrm = vec3(0.0);

    for (int i = 0; i < 4; i++) {
        if (boneIDs[i] < 0) continue;
        mat4 boneMat = finalBonesMatrices[boneIDs[i]];
        skinnedPos  += weights[i] * (boneMat * vec4(aPos, 1.0));
        skinnedNrm  += weights[i] * mat3(boneMat) * aNormal;
    }

    vec4 worldPos = model * skinnedPos;
    vFragPos      = worldPos.xyz;
    vNormal       = mat3(transpose(inverse(model))) * skinnedNrm;
    vUV           = aUV;
    gl_Position   = projection * view * worldPos;
}
)";
        const char* frag = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D texture_diffuse0;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 viewPos;

void main() {
    vec3 base  = texture(texture_diffuse0, vUV).rgb;
    vec3 n     = normalize(vNormal);
    vec3 l     = normalize(-lightDir);
    float diff = max(dot(n, l), 0.0);
    vec3 col   = base * (0.3 + 0.7 * diff) * lightColor;

    float dist    = length(viewPos - vFragPos);
    float fog     = clamp((dist - 15.0) / 45.0, 0.0, 1.0);
    col           = mix(col, vec3(0.68, 0.65, 0.60), fog);

    FragColor = vec4(col, 1.0);
}
)";
        return _linkProgram(vert, frag);
    }

    static unsigned int _compileShader(unsigned int type, const char* src) {
        unsigned int sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        int ok;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(sh, 1024, nullptr, log);
            std::cerr << "[Shader] Compile error:\n" << log << "\n";
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    }

    static unsigned int _linkProgram(const char* vert, const char* frag) {
        unsigned int vs = _compileShader(GL_VERTEX_SHADER,   vert);
        unsigned int fs = _compileShader(GL_FRAGMENT_SHADER, frag);
        if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return 0; }

        unsigned int p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);

        int ok;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(p, 1024, nullptr, log);
            std::cerr << "[Shader] Link error:\n" << log << "\n";
            glDeleteProgram(p);
            p = 0;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
        return p;
    }
};
