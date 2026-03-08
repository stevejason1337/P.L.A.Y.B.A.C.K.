#pragma once
// ================================================================
//  RenderCommon.h — типи і константи спільні для GL і DX11
//  Не включай сюди нічого DX11/GL специфічного!
// ================================================================
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ── Lighting parameters (однакові для GLSL і HLSL) ────────────
// Щоб змінити освітлення — міняй тільки тут, обидва API підхоплять
inline constexpr glm::vec3 LIGHT_DIR = { 0.4f, -1.0f,  0.3f };
inline constexpr glm::vec3 FOG_COLOR = { 0.68f, 0.65f, 0.60f };
inline constexpr float     FOG_START = 15.f;
inline constexpr float     FOG_END = 60.f;
inline constexpr glm::vec3 MAP_BASE_COLOR = { 0.75f, 0.72f, 0.65f };

// Lighting multipliers (ambient/diffuse/sky/ground/specular)
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

// ── API enum ───────────────────────────────────────────────────
enum class RenderAPI { OpenGL, DX11 };