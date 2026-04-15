#pragma once

// ============================================================
//  Engine/Renderer.h  —  ДВИЖОК, не трогаешь
//  Чистый OpenGL рендерер. Знает только о:
//   - MeshRenderer компонентах
//   - Scene (список объектов)
//  НЕ знает о Player, Enemy, Weapon — никогда.
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#include "Scene.h"
#include "MeshRenderer.h"
#include "AnimatedModel.h"

// ── Компилятор шейдеров ────────────────────────────────────
inline unsigned int compileShader(const std::string& vertSrc, const std::string& fragSrc) {
    auto compile = [](const char* src, GLenum type) {
        unsigned int id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);
        int ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(id, 512, nullptr, log);
            std::cerr << "[Shader] " << log << "\n";
        }
        return id;
    };
    unsigned int vs = compile(vertSrc.c_str(), GL_VERTEX_SHADER);
    unsigned int fs = compile(fragSrc.c_str(), GL_FRAGMENT_SHADER);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

inline unsigned int loadShaderFromFiles(const std::string& vPath, const std::string& fPath) {
    auto read = [](const std::string& p) {
        std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str();
    };
    return compileShader(read(vPath), read(fPath));
}

// ── Структура света ────────────────────────────────────────
struct DirLight {
    glm::vec3 direction = glm::normalize(glm::vec3(-0.5f, -1.f, -0.5f));
    glm::vec3 color     = glm::vec3(1.f);
    float     intensity = 1.f;
};

// ── Структура камеры (данные, не объект) ───────────────────
struct CameraData {
    glm::vec3 position = glm::vec3(0.f, 1.8f, 0.f);
    glm::vec3 front    = glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 up       = glm::vec3(0.f, 1.f, 0.f);
    float     fov      = 70.f;
    float     near_    = 0.05f;
    float     far_     = 500.f;

    glm::mat4 getView() const {
        return glm::lookAt(position, position + front, up);
    }
    glm::mat4 getProjection(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, near_, far_);
    }
};

// ── Renderer ───────────────────────────────────────────────
class Renderer {
public:
    static Renderer& get() {
        static Renderer instance;
        return instance;
    }

    CameraData  camera;
    DirLight    sun;
    glm::vec4   clearColor = glm::vec4(0.05f, 0.05f, 0.07f, 1.f);

    bool init(GLFWwindow* window) {
        win = window;
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Renderer] Failed to init GLAD\n";
            return false;
        }
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // Встроенный базовый шейдер
        mainShader = compileShader(defaultVertSrc, defaultFragSrc);
        std::cout << "[Renderer] OpenGL " << glGetString(GL_VERSION) << "\n";
        return true;
    }

    // Установить пользовательский шейдер (опционально)
    void setMainShader(unsigned int shaderID) { mainShader = shaderID; }

    // Вызывается Core в начале кадра
    void beginFrame() {
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        if (h == 0) h = 1;
        glViewport(0, 0, w, h);
        aspect = (float)w / (float)h;
    }

    // Рисует все MeshRenderer и AnimatedModelComponent в сцене
    void drawScene(const Scene& scene) {
        glUseProgram(mainShader);

        // Матрицы
        glm::mat4 view  = camera.getView();
        glm::mat4 proj  = camera.getProjection(aspect);
        setUniform("view",       view);
        setUniform("projection", proj);
        setUniform("viewPos",    camera.position);

        // Свет
        setUniform("sunDir",     sun.direction);
        setUniform("sunColor",   sun.color * sun.intensity);

        // Рисуем все MeshRenderer
        for (auto& obj : scene.getAll()) {
            if (!obj->active) continue;
            if (auto* mr = obj->getComponent<MeshRenderer>())
                mr->draw(mainShader);
            // AnimatedModel рисует себя сам через свой шейдер
            // если нужно — добавь AnimatedModelComponent аналогично
        }
    }

    // Вызывается Core в конце кадра
    void endFrame() {
        glfwSwapBuffers(win);
    }

    float getAspect() const { return aspect; }
    GLFWwindow* getWindow() const { return win; }

private:
    GLFWwindow*  win        = nullptr;
    unsigned int mainShader = 0;
    float        aspect     = 16.f/9.f;

    void setUniform(const char* name, const glm::mat4& v) {
        glUniformMatrix4fv(glGetUniformLocation(mainShader, name), 1, GL_FALSE, glm::value_ptr(v));
    }
    void setUniform(const char* name, const glm::vec3& v) {
        glUniform3fv(glGetUniformLocation(mainShader, name), 1, glm::value_ptr(v));
    }
    void setUniform(const char* name, float v) {
        glUniform1f(glGetUniformLocation(mainShader, name), v);
    }

    // ── Встроенные шейдеры (минималистичные) ──────────────
    const char* defaultVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 fragPos;
out vec3 normal;
out vec2 texCoord;

void main() {
    vec4 world = model * vec4(aPos, 1.0);
    fragPos    = world.xyz;
    normal     = mat3(transpose(inverse(model))) * aNormal;
    texCoord   = aTexCoord;
    gl_Position = projection * view * world;
}
)";

    const char* defaultFragSrc = R"(
#version 330 core
in vec3 fragPos;
in vec3 normal;
in vec2 texCoord;

uniform sampler2D texture_diffuse0;
uniform vec3  sunDir;
uniform vec3  sunColor;
uniform vec3  viewPos;

out vec4 FragColor;

void main() {
    vec4 albedo = texture(texture_diffuse0, texCoord);
    if (albedo.a < 0.1) discard;

    vec3 n    = normalize(normal);
    vec3 l    = normalize(-sunDir);
    float diff = max(dot(n, l), 0.0);

    vec3 ambient  = 0.15 * sunColor;
    vec3 diffuse  = diff * sunColor;

    // Specular (Blinn-Phong)
    vec3 v   = normalize(viewPos - fragPos);
    vec3 h   = normalize(l + v);
    float sp = pow(max(dot(n, h), 0.0), 32.0);
    vec3 spec = 0.3 * sp * sunColor;

    FragColor = vec4((ambient + diffuse + spec) * albedo.rgb, albedo.a);
}
)";
};
