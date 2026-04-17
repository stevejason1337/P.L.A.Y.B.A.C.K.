#pragma once
// Textrenderer.h — только объявление
// STB_TRUETYPE_IMPLEMENTATION определяется ТОЛЬКО в Textrenderer.cpp
// stb_truetype.h сюда НЕ включается вообще

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

struct TextRenderer
{
    unsigned int textShader = 0;
    unsigned int rectShader = 0;
    unsigned int VAO = 0, VBO = 0;
    unsigned int rectVAO = 0, rectVBO = 0;
    unsigned int fontTex = 0;
    float        fontH = 16.f;
    int          scrW = 1280;
    int          scrH = 720;

    // void* чтобы не тащить stb_truetype.h в хедер
    // В .cpp кастуем обратно в stbtt_bakedchar*
    void* cdata = nullptr;  // на самом деле stbtt_bakedchar[96]

    bool  init(const std::string& fontPath, float h, int sw, int sh);
    void  drawRect(float x, float y, float w, float h, glm::vec4 col);
    float drawText(const std::string& text, float x, float y, glm::vec4 col);
    float textWidth(const std::string& text);

    ~TextRenderer();
};

extern TextRenderer textRenderer;