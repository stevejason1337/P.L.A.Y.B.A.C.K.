#pragma once
// ================================================================
// Rendercommon.h — типы и константы общие для GL и DX11.
// Не включай сюда ничего DX11/GL специфичного!
//
// ИСПРАВЛЕНО: убран "enum class RenderAPI { OpenGL, DX11 }" —
// он конфликтовал с классом RenderAPI из Renderapi.h.
// Вместо него используй RenderBackend из Settings.h.
// ================================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Settings.h"   // RenderBackend, SCR_W, SCR_H

// ── Lighting parameters (одинаковые для GLSL и HLSL) ──────────
// Единственный источник истины для всего освещения.
// Renderapi.h / Renderer.h читают эти константы — НЕ дублируют!
inline constexpr glm::vec3 LIGHT_DIR = { 0.4f, -1.0f, 0.3f };
inline constexpr glm::vec3 FOG_COLOR = { 0.68f, 0.65f, 0.60f };
inline constexpr float     FOG_START = 15.f;
inline constexpr float     FOG_END = 60.f;
inline constexpr glm::vec3 MAP_BASE_COLOR = { 0.75f, 0.72f, 0.65f };

// ── Lighting multipliers ────────────────────────────────────────
inline constexpr glm::vec3 LIGHT_AMBIENT = { 0.30f, 0.28f, 0.25f };
inline constexpr glm::vec3 LIGHT_DIFFUSE = { 1.05f, 0.95f, 0.80f };
inline constexpr float     LIGHT_DIFF_STR = 0.85f;
inline constexpr glm::vec3 LIGHT_SKY = { 0.55f, 0.70f, 0.90f };
inline constexpr float     LIGHT_SKY_STR = 0.25f;
inline constexpr glm::vec3 LIGHT_GROUND = { 0.40f, 0.35f, 0.28f };
inline constexpr float     LIGHT_GND_STR = 0.12f;
inline constexpr float     LIGHT_SPEC_STR = 0.15f;
inline constexpr float     LIGHT_SAT = 1.2f;
inline constexpr float     LIGHT_ACES_EXP = 0.8f;

// ── Screen / shadow ────────────────────────────────────────────
inline constexpr int SCR_W = 1280;
inline constexpr int SCR_H = 720;
inline constexpr int SHADOW_RES = 2048;