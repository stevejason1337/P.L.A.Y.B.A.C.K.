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

inline unsigned int loadTexture(const std::string& path)
{
    if (texCache.count(path)) return texCache[path];
    unsigned int id = 0;
    glGenTextures(1, &id);
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    if (data) {
        GLenum fmt = (ch == 4) ? GL_RGBA : (ch == 3) ? GL_RGB : GL_RED;
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        std::cout << "[TEX] OK: " << path << "\n";
    }
    else {
        std::cerr << "[TEX] Failed: " << path << "\n"; id = 0;
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
        meshes.push_back(mesh);
    }
    std::cout << "[MODEL] Loaded: " << path << " meshes:" << meshes.size() << "\n";
    return meshes;
}

// ── Загрузка DX11 буферов + текстур для уже загруженных мешей ──
#ifdef _WIN32
#include <d3d11.h>
inline void uploadMeshesToDX11(std::vector<GPUMesh>& meshes, ID3D11Device* dev)
{
    for (auto& m : meshes)
    {
        if (m.dxVB) continue;

        // ── Вершины: читаем из GL VBO ──────────────────────────
        glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
        GLint sz = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &sz);
        if (sz <= 0) { printf("[DX11] VBO size=0, skip\n"); continue; }
        std::vector<uint8_t> vdata(sz);
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, sz, vdata.data());

        // ── Индексы: читаем из GL EBO ──────────────────────────
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
        GLint isz = 0;
        glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &isz);
        std::vector<uint8_t> idata(isz);
        glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, isz, idata.data());

        // stride всегда 16 floats (64 байта) — AnimatedModel.loadMesh всегда пишет 16f
        // loadModel для карты пишет 8f — определяем по размеру буфера
        UINT floatsPerVert = (m.indexCount > 0) ? (UINT)(sz / sizeof(float)) / (m.indexCount > 0 ? 1 : 1) : 16;
        // Безопасный способ: stride по размеру VBO / количеству вертексов
        // Но у нас нет vertexCount отдельно. Используем: если skinned → 16f, иначе 8f
        // Карта: skinned=false, stride=8*4=32. AnimatedModel: skinned=true, stride=16*4=64
        m.dxStride = m.skinned ? (16 * sizeof(float)) : (8 * sizeof(float));

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = (UINT)sz;
        D3D11_SUBRESOURCE_DATA sd = { vdata.data() };
        dev->CreateBuffer(&bd, &sd, (ID3D11Buffer**)&m.dxVB);

        bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.ByteWidth = (UINT)isz;
        D3D11_SUBRESOURCE_DATA sd2 = { idata.data() };
        dev->CreateBuffer(&bd, &sd2, (ID3D11Buffer**)&m.dxIB);

        // ── Текстура: читаем пиксели из GL текстуры → DX11 SRV ─
        if (m.texID && !m.dxSRV) {
            glBindTexture(GL_TEXTURE_2D, m.texID);
            GLint tw = 0, th = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
            if (tw > 0 && th > 0) {
                std::vector<uint8_t> pixels(tw * th * 4);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

                D3D11_TEXTURE2D_DESC td = {};
                td.Width = tw; td.Height = th; td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA tsd = { pixels.data(), (UINT)(tw * 4) };
                ID3D11Texture2D* tex2d = nullptr;
                if (SUCCEEDED(dev->CreateTexture2D(&td, &tsd, &tex2d))) {
                    dev->CreateShaderResourceView(tex2d, nullptr, (ID3D11ShaderResourceView**)&m.dxSRV);
                    tex2d->Release();
                }
            }
        }

        if (!m.dxVB || !m.dxIB)
            printf("[DX11] upload failed for mesh (VB=%p IB=%p)\n", m.dxVB, m.dxIB);
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