#pragma once
// ================================================================
//  ShaderTranspiler.h  —  декларации
//
//  Один набор шейдеров для всех API:
//    OpenGL  → buildProgram() → нативный GLSL 330
//    DX11    → get*HLSL()    → HLSL через D3DCompile (в Renderer.h)
//    Vulkan  → vsSpirv() / psSpirv() (будущее)
//
//  Реализация в ShaderTranspiler.cpp
//  Подключай ТОЛЬКО этот .h файл в остальных единицах трансляции.
// ================================================================

#include <glad/glad.h>
#include <string>
#include <vector>

namespace ShaderTranspiler {

    // ── Lifecycle ──────────────────────────────────────────────
    void init();
    void shutdown();

    // ── OpenGL: HLSL строка → скомпилированная GL программа ───
    // Внутри определяет тип шейдера и компилирует нужный GLSL
    GLuint buildProgram(const char* hlsl);

    // ── DX11: геттеры HLSL исходников ─────────────────────────
    // Renderer.h передаёт их в D3DCompile напрямую
    const char* getWorldHLSL();
    const char* getGunHLSL();
    const char* getPostHLSL();
    const char* getShadowHLSL();
    const char* getDotHLSL();

    // ── Vulkan (будущее): SPIR-V бинарь ───────────────────────
    std::vector<uint32_t> vsSpirv(const char* hlsl);
    std::vector<uint32_t> psSpirv(const char* hlsl);

    // ── Утилиты ────────────────────────────────────────────────
    void clearCache();   // hot-reload шейдеров
    void reloadAll();    // пересобрать все программы

} // namespace ShaderTranspiler