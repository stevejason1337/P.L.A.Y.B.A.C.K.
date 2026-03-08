#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static const char* TEXT_VERT = R"(
#version 330 core
layout(location=0) in vec4 vertex;
out vec2 vUV;
uniform mat4 projection;
void main(){
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    vUV = vertex.zw;
})";

static const char* TEXT_FRAG = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D fontTex;
uniform vec4 color;
void main(){
    float a = texture(fontTex, vUV).r;
    FragColor = vec4(color.rgb, color.a * a);
})";

static const char* RECT_VERT = R"(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 projection;
void main(){
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
})";

static const char* RECT_FRAG = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 color;
void main(){ FragColor = color; })";

struct TextRenderer
{
    unsigned int textShader = 0, rectShader = 0;
    unsigned int VAO = 0, VBO = 0, rectVAO = 0, rectVBO = 0;
    unsigned int fontTex = 0;
    stbtt_bakedchar cdata[96];
    float fontH = 16.f;
    int scrW = 1280, scrH = 720;

    bool init(const std::string& path, float h, int sw, int sh)
    {
        scrW = sw; scrH = sh; fontH = h;

        // Проверка: если нет GL контекста (DX11 режим или вызов до gladLoadGL)
        // glCreateShader вернёт 0 -> glShaderSource упадёт с access violation
        if (!glGetString(GL_VERSION)) {
            std::cerr << "[TEXT] No OpenGL context — TextRenderer disabled\n";
            return false;
        }

        auto compile = [](unsigned int t, const char* s) -> unsigned int {
            unsigned int sh = glCreateShader(t);
            if (!sh) { std::cerr << "[TEXT] glCreateShader=0, no GL context?\n"; return 0; }
            glShaderSource(sh, 1, &s, NULL); glCompileShader(sh);
            int ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, NULL, l); std::cerr << "[TEXT] " << l; }
            return sh;
            };
        auto link = [&](const char* v, const char* f) -> unsigned int {
            unsigned int vs = compile(GL_VERTEX_SHADER, v);
            unsigned int fs = compile(GL_FRAGMENT_SHADER, f);
            if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
            unsigned int p = glCreateProgram();
            glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
            int ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) { char l[512]; glGetProgramInfoLog(p, 512, NULL, l); std::cerr << "[TEXT] Link: " << l; }
            glDeleteShader(vs); glDeleteShader(fs); return p;
            };
        textShader = link(TEXT_VERT, TEXT_FRAG);
        rectShader = link(RECT_VERT, RECT_FRAG);
        if (!textShader || !rectShader) {
            std::cerr << "[TEXT] Shader build failed\n";
            return false;
        }

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) { std::cerr << "[FONT] Cannot open: " << path << "\n"; return false; }
        size_t sz = f.tellg(); f.seekg(0);
        std::vector<unsigned char> buf(sz);
        f.read((char*)buf.data(), sz);

        std::vector<unsigned char> bmp(512 * 512);
        stbtt_BakeFontBitmap(buf.data(), 0, h, bmp.data(), 512, 512, 32, 96, cdata);

        glGenTextures(1, &fontTex);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 512, 512, 0, GL_RED, GL_UNSIGNED_BYTE, bmp.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        glGenVertexArrays(1, &rectVAO); glGenBuffers(1, &rectVBO);
        glBindVertexArray(rectVAO);
        glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, NULL, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        std::cout << "[FONT] Loaded: " << path << "\n";
        return true;
    }

    void drawRect(float x, float y, float w, float h, glm::vec4 col)
    {
        glm::mat4 proj = glm::ortho(0.f, (float)scrW, (float)scrH, 0.f);
        glUseProgram(rectShader);
        glUniformMatrix4fv(glGetUniformLocation(rectShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform4fv(glGetUniformLocation(rectShader, "color"), 1, glm::value_ptr(col));
        float v[] = { x,y, x + w,y, x + w,y + h, x,y, x + w,y + h, x,y + h };
        glBindVertexArray(rectVAO);
        glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    float drawText(const std::string& text, float x, float y, glm::vec4 col)
    {
        glm::mat4 proj = glm::ortho(0.f, (float)scrW, (float)scrH, 0.f);
        glUseProgram(textShader);
        glUniformMatrix4fv(glGetUniformLocation(textShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform4fv(glGetUniformLocation(textShader, "color"), 1, glm::value_ptr(col));
        glUniform1i(glGetUniformLocation(textShader, "fontTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(VAO);
        float cx = x;
        for (char c : text) {
            if (c < 32 || c>127) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 512, 512, c - 32, &cx, &y, &q, 1);
            float verts[6][4] = {
                {q.x0,q.y0,q.s0,q.t0},{q.x1,q.y0,q.s1,q.t0},{q.x1,q.y1,q.s1,q.t1},
                {q.x0,q.y0,q.s0,q.t0},{q.x1,q.y1,q.s1,q.t1},{q.x0,q.y1,q.s0,q.t1},
            };
            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        return cx;
    }

    float textWidth(const std::string& text)
    {
        float x = 0, y = 0;
        for (char c : text) {
            if (c < 32 || c>127) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 512, 512, c - 32, &x, &y, &q, 1);
        }
        return x;
    }
};

inline TextRenderer textRenderer;