#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#ifndef STB_IMAGE_IMPLEMENTATION_DONE
#define STB_IMAGE_IMPLEMENTATION_DONE
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

#include <iostream>
#include <vector>
#include <string>
#include <map>

#include "AnimatedModel.h"
#include "Triangle.h"

// ──────────────────────────────────────────────
//  Collision triangle
// ──────────────────────────────────────────────
inline std::vector<Triangle> colTris;

// ──────────────────────────────────────────────
//  Texture cache
// ──────────────────────────────────────────────
inline std::map<std::string, unsigned int> texCache;

// Флаг: грузить GL текстуры или только пиксели (для DX11)
// Устанавливается в main.cpp: gLoadGLTextures = true/false
inline bool gLoadGLTextures = true;

// Пиксельные данные для DX11 — хранятся до uploadMeshesToDX11()
struct TexPixels {
    std::vector<uint8_t> data;
    int w = 0, h = 0;
};
inline std::map<std::string, TexPixels> texPixelCache;

inline unsigned int loadTexture(const std::string& path)
{
    if (texCache.count(path)) return texCache[path];
    unsigned int id = 0;

    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4); // всегда 4 канала
    if (!data) {
        std::cerr << "[TEX] Failed: " << path << "\n";
        texCache[path] = 0;
        return 0;
    }

    if (gLoadGLTextures) {
        // OpenGL путь
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        std::cout << "[TEX] GL OK: " << path << "\n";
    }
    else {
        // DX11 путь — сохраняем пиксели, texID=0 (uploadMeshesToDX11 создаст SRV)
        TexPixels px;
        px.data.assign(data, data + w * h * 4);
        px.w = w; px.h = h;
        texPixelCache[path] = std::move(px);
        id = 0; // GL текстуры нет
        std::cout << "[TEX] DX11 pixels cached: " << path << "\n";
    }

    stbi_image_free(data);
    texCache[path] = id;
    return id;
}

// ──────────────────────────────────────────────
//  Static model loader (map)
// ──────────────────────────────────────────────
inline std::vector<GPUMesh> loadModel(const std::string& path,
    const std::string& texDir,
    glm::mat4 T,
    bool buildCol = false)
{
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_OptimizeMeshes);
    if (!sc || !sc->mRootNode) {
        std::cerr << "[MODEL] Cannot load: " << path << "\n"; return {};
    }

    std::vector<GPUMesh> meshes;
    for (unsigned int m = 0; m < sc->mNumMeshes; m++) {
        aiMesh* am = sc->mMeshes[m];
        std::vector<float> verts;
        std::vector<unsigned int> idx;

        if (!am->mVertices) continue;  // ← добавь эту строку
        for (unsigned int i = 0; i < am->mNumVertices; i++) {
            verts.push_back(am->mVertices[i].x);
            verts.push_back(am->mVertices[i].y);
            verts.push_back(am->mVertices[i].z);
            if (am->HasNormals()) {
                verts.push_back(am->mNormals[i].x);
                verts.push_back(am->mNormals[i].y);
                verts.push_back(am->mNormals[i].z);
            }
            else { verts.push_back(0); verts.push_back(1); verts.push_back(0); }
            if (am->HasTextureCoords(0)) {
                verts.push_back(am->mTextureCoords[0][i].x);
                verts.push_back(am->mTextureCoords[0][i].y);
            }
            else { verts.push_back(0); verts.push_back(0); }
        }

        for (unsigned int i = 0; i < am->mNumFaces; i++) {
            aiFace& f = am->mFaces[i];
            for (unsigned int j = 0; j < f.mNumIndices; j++) idx.push_back(f.mIndices[j]);
        }

        if (buildCol) {
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                auto gv = [&](unsigned int id)->glm::vec3 {
                    unsigned int b = id * 8;
                    return glm::vec3(T * glm::vec4(verts[b], verts[b + 1], verts[b + 2], 1.f));
                    };
                colTris.push_back({ gv(idx[i]),gv(idx[i + 1]),gv(idx[i + 2]) });
            }
        }

        unsigned int texID = 0;
        if (am->mMaterialIndex < sc->mNumMaterials) {
            aiMaterial* mat = sc->mMaterials[am->mMaterialIndex];
            aiString tp;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tp) == AI_SUCCESS) {
                texID = loadTexture(texDir + "/" + tp.C_Str());
                if (!texID) {
                    std::string n = tp.C_Str();
                    size_t sl = n.find_last_of("/\\");
                    if (sl != std::string::npos) n = n.substr(sl + 1);
                    texID = loadTexture(texDir + "/" + n);
                }
            }
        }

        GPUMesh mesh;
        mesh.indexCount = (unsigned int)idx.size();
        mesh.texID = texID; mesh.skinned = false;
        // Сохраняем путь к текстуре для DX11 texPixelCache lookup
        // Нормализуем так же как loadTexture — берём basename чтобы ключи совпадали
        if (am->mMaterialIndex < sc->mNumMaterials) {
            aiMaterial* mat2 = sc->mMaterials[am->mMaterialIndex];
            aiString tp2;
            if (mat2->GetTexture(aiTextureType_DIFFUSE, 0, &tp2) == AI_SUCCESS) {
                std::string rawName = tp2.C_Str();
                // Normalize backslashes
                for (auto& ch : rawName) if (ch == '\\') ch = '/';
                size_t sl = rawName.find_last_of("/");
                std::string baseName = (sl != std::string::npos) ? rawName.substr(sl + 1) : rawName;
                // Try full path first, then basename — same priority as loadTexture
                std::string fullPath = texDir + "/" + rawName;
                std::string basePath = texDir + "/" + baseName;
                // Store whichever is actually in the cache (or basePath as fallback)
                mesh.texPath = (texPixelCache.count(fullPath)) ? fullPath : basePath;
            }
        }

        if (gLoadGLTextures) {
            // OpenGL: сразу грузим на GPU
            glGenVertexArrays(1, &mesh.VAO);
            glGenBuffers(1, &mesh.VBO);
            glGenBuffers(1, &mesh.EBO);
            glBindVertexArray(mesh.VAO);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glBindVertexArray(0);
        }
        else {
            // DX11: сохраняем данные в памяти для uploadMeshesToDX11()
            mesh.cpuVerts.assign((uint8_t*)verts.data(),
                (uint8_t*)verts.data() + verts.size() * sizeof(float));
            mesh.cpuIdx.assign((uint8_t*)idx.data(),
                (uint8_t*)idx.data() + idx.size() * sizeof(unsigned int));
        }
        meshes.push_back(mesh);
    }
    std::cout << "[MODEL] Loaded: " << path << " meshes:" << meshes.size() << "\n";
    return meshes;
}

// ── Загрузка DX11 буферов + текстур из CPU памяти (нет GL зависимости) ──
#ifdef _WIN32
#include <d3d11.h>
inline void uploadMeshesToDX11(std::vector<GPUMesh>& meshes, ID3D11Device* dev)
{
    for (auto& m : meshes)
    {
        if (m.dxVB) continue;

        // ── Данные берём из CPU буферов (заполняются в loadModel на DX11 пути) ──
        if (m.cpuVerts.empty() || m.cpuIdx.empty()) {
            printf("[DX11] cpuVerts/cpuIdx empty — mesh skipped\n"); continue;
        }

        m.dxStride = m.skinned ? (16 * sizeof(float)) : (8 * sizeof(float));

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = (UINT)m.cpuVerts.size();
        D3D11_SUBRESOURCE_DATA sd = { m.cpuVerts.data() };
        dev->CreateBuffer(&bd, &sd, (ID3D11Buffer**)&m.dxVB);

        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = (UINT)m.cpuIdx.size();
        D3D11_SUBRESOURCE_DATA sd2 = { m.cpuIdx.data() };
        dev->CreateBuffer(&bd, &sd2, (ID3D11Buffer**)&m.dxIB);

        // ── Текстура: берём пиксели из texPixelCache (нет GL) ─────────────────
        if (!m.dxSRV && !m.texPath.empty()) {
            // Try exact path, then basename fallback
            auto it = texPixelCache.find(m.texPath);
            if (it == texPixelCache.end()) {
                size_t sl = m.texPath.find_last_of("/\\");
                std::string base = (sl != std::string::npos) ? m.texPath.substr(sl + 1) : m.texPath;
                for (auto& kv : texPixelCache) {
                    size_t ksl = kv.first.find_last_of("/\\");
                    std::string kbase = (ksl != std::string::npos) ? kv.first.substr(ksl + 1) : kv.first;
                    if (kbase == base) { it = texPixelCache.find(kv.first); break; }
                }
            }
            if (it != texPixelCache.end()) {
                auto& px = it->second;
                D3D11_TEXTURE2D_DESC td = {};
                td.Width = px.w; td.Height = px.h; td.MipLevels = 0; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                ID3D11Texture2D* tex2d = nullptr;
                if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex2d))) {
                    ID3D11DeviceContext* ctx = nullptr;
                    dev->GetImmediateContext(&ctx);
                    ctx->UpdateSubresource(tex2d, 0, nullptr, px.data.data(), px.w * 4, 0);
                    ID3D11ShaderResourceView* srv = nullptr;
                    if (SUCCEEDED(dev->CreateShaderResourceView(tex2d, nullptr, &srv))) {
                        ctx->GenerateMips(srv);
                        m.dxSRV = srv;
                    }
                    ctx->Release();
                    tex2d->Release();
                }
            }
        }

        // CPU буферы больше не нужны — освобождаем память
        m.cpuVerts.clear(); m.cpuVerts.shrink_to_fit();
        m.cpuIdx.clear();   m.cpuIdx.shrink_to_fit();

        if (!m.dxVB || !m.dxIB)
            printf("[DX11] upload FAILED (VB=%p IB=%p)\n", m.dxVB, m.dxIB);
        else
            printf("[DX11] mesh OK (stride=%u idx=%u)\n", m.dxStride, m.indexCount);
    }
    printf("[DX11] Uploaded %zu meshes\n", meshes.size());
}
#endif

inline void drawMeshes(const std::vector<GPUMesh>& meshes, unsigned int shader)
{
    for (const GPUMesh& m : meshes) {
        if (m.texID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m.texID);
            glUniform1i(glGetUniformLocation(shader, "hasTexture"), 1);
            glUniform1i(glGetUniformLocation(shader, "tex"), 0);
        }
        else {
            glUniform1i(glGetUniformLocation(shader, "hasTexture"), 0);
        }
        glBindVertexArray(m.VAO);
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
    }
}