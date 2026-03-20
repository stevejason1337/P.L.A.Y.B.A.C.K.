#pragma once
// ================================================================
//  ShaderTranspiler.h — HLSL → GLSL через glslang + SPIRV-Cross
//
//  Цепочка: HLSL → SPIR-V (glslang) → GLSL 330 (spirv-cross)
//  Это тот же путь что использует Godot Engine и MoltenVK.
//
//  Зависимости (vcpkg x64-windows):
//    glslang, spirv-cross
//  Подключи в Project Settings → Linker:
//    glslang.lib, SPIRV.lib, glslang-default-resource-limits.lib,
//    spirv-cross-core.lib, spirv-cross-glsl.lib, spirv-cross-hlsl.lib
// ================================================================

// glslang — компилятор HLSL/GLSL → SPIR-V
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>

// SPIRV-Cross — конвертер SPIR-V → GLSL/MSL/HLSL
#include <spirv_cross/spirv_glsl.hpp>

#include <string>
#include <vector>
#include <cstdio>

namespace ShaderTranspiler {

    // ── Инициализация (вызови один раз при старте) ────────────────
    inline void init() {
        glslang::InitializeProcess();
    }
    inline void shutdown() {
        glslang::FinalizeProcess();
    }

    // ── Компиляция HLSL → SPIR-V ──────────────────────────────────
    static std::vector<uint32_t> hlslToSpirv(
        const char* hlslSrc,
        EShLanguage stage,          // EShLangVertex или EShLangFragment
        const char* entryPoint)     // "VSMain" или "PSMain"
    {
        glslang::TShader shader(stage);

        const char* sources[] = { hlslSrc };
        shader.setStrings(sources, 1);
        shader.setEnvInput(glslang::EShSourceHlsl, stage,
            glslang::EShClientOpenGL, 330);
        shader.setEnvClient(glslang::EShClientOpenGL,
            glslang::EShTargetOpenGL_450);
        shader.setEnvTarget(glslang::EShTargetSpv,
            glslang::EShTargetSpv_1_0);
        shader.setEntryPoint(entryPoint);
        shader.setSourceEntryPoint(entryPoint);
        shader.setAutoMapBindings(true);
        shader.setAutoMapLocations(true);

        const TBuiltInResource* resources = GetDefaultResources();
        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgReadHlsl);

        if (!shader.parse(resources, 330, false, messages)) {
            printf("[ShaderTranspiler] HLSL parse error (%s):\n%s\n",
                entryPoint, shader.getInfoLog());
            return {};
        }

        glslang::TProgram program;
        program.addShader(&shader);

        if (!program.link(messages)) {
            printf("[ShaderTranspiler] Link error:\n%s\n",
                program.getInfoLog());
            return {};
        }

        std::vector<uint32_t> spirv;
        spv::SpvBuildLogger logger;
        glslang::SpvOptions spvOptions;
        spvOptions.generateDebugInfo = false;
        spvOptions.optimizeSize = true;

        glslang::GlslangToSpv(*program.getIntermediate(stage),
            spirv, &logger, &spvOptions);

        if (spirv.empty()) {
            printf("[ShaderTranspiler] SPIR-V generation failed\n");
        }
        return spirv;
    }

    // ── SPIR-V → GLSL 330 через SPIRV-Cross ──────────────────────
    static std::string spirvToGlsl(const std::vector<uint32_t>& spirv) {
        if (spirv.empty()) return "";

        spirv_cross::CompilerGLSL glsl(spirv);

        spirv_cross::CompilerGLSL::Options opts;
        opts.version = 330;
        opts.es = false;
        opts.vulkan_semantics = false;
        opts.enable_420pack_extension = false;
        opts.emit_push_constant_as_uniform_buffer = true;
        glsl.set_common_options(opts);

        try {
            return glsl.compile();
        }
        catch (const spirv_cross::CompilerError& e) {
            printf("[ShaderTranspiler] SPIRV-Cross error: %s\n", e.what());
            return "";
        }
    }

    // ── Главные функции ────────────────────────────────────────────
    // Возвращает GLSL строку для vertex shader
    inline std::string vsFromHLSL(const char* hlsl) {
        auto spirv = hlslToSpirv(hlsl, EShLangVertex, "VSMain");
        return spirvToGlsl(spirv);
    }

    // Возвращает GLSL строку для pixel/fragment shader
    inline std::string psFromHLSL(const char* hlsl) {
        auto spirv = hlslToSpirv(hlsl, EShLangFragment, "PSMain");
        return spirvToGlsl(spirv);
    }

} // namespace ShaderTranspiler