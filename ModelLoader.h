#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <string>
#include <map>
#include <cstdint>

#include "AnimatedModel.h"
#include "Triangle.h"

// ──────────────────────────────────────────────
//  Collision triangles (definition in ModelLoader.cpp)
// ──────────────────────────────────────────────
extern std::vector<Triangle> colTris;

// ──────────────────────────────────────────────
//  Texture cache (definition in ModelLoader.cpp)
// ──────────────────────────────────────────────
extern std::map<std::string, unsigned int> texCache;

// Флаг: грузить GL текстуры или только пиксели (для DX11)
// Устанавливается в main.cpp перед loadModel()
extern bool gLoadGLTextures;

// Пиксельные данные для DX11 — хранятся до uploadMeshesToDX11()
struct TexPixels {
    std::vector<uint8_t> data;
    int w = 0, h = 0;
};
extern std::map<std::string, TexPixels> texPixelCache;

// ──────────────────────────────────────────────
//  Public API
// ──────────────────────────────────────────────
unsigned int loadTexture(const std::string& path);

std::vector<GPUMesh> loadModel(const std::string& path,
    const std::string& texDir,
    glm::mat4 T,
    bool buildCol = false);

void uploadMeshesToDX11(std::vector<GPUMesh>& meshes, void* dev_);

void drawMeshes(const std::vector<GPUMesh>& meshes, unsigned int shader);