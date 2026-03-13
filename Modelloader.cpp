// ModelLoader.cpp
// STB и Assimp — только здесь, не в хедере

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#endif

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// STB — ровно один раз, только в этом .cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <vector>
#include <string>
#include <map>

#include "ModelLoader.h"

// ──────────────────────────────────────────────
//  Definitions of extern globals
// ──────────────────────────────────────────────
std::vector<Triangle>              colTris;
std::map<std::string, unsigned int> texCache;
bool                               gLoadGLTextures = true;
std::map<std::string, TexPixels>   texPixelCache;

// ──────────────────────────────────────────────
//  loadTexture
// ──────────────────────────────────────────────
unsigned int loadTexture(const std::string& path)
{
    if (texCache.count(path)) return texCache[path];
    unsigned int id = 0;

    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[TEX] Failed: " << path << "\n";
        texCache[path] = 0;
        return 0;
    }

    if (gLoadGLTextures) {
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
        TexPixels px;
        px.data.assign(data, data + w * h * 4);
        px.w = w; px.h = h;
        texPixelCache[path] = std::move(px);
        id = 0;
        std::cout << "[TEX] DX11 pixels cached: " << path << "\n";
    }

    stbi_image_free(data);
    texCache[path] = id;
    return id;
}

// ──────────────────────────────────────────────
//  loadModel
// ──────────────────────────────────────────────
std::vector<GPUMesh> loadModel(const std::string& path,
    const std::string& texDir,
    glm::mat4 T,
    bool buildCol)
{
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_OptimizeMeshes);
    if (!sc || !sc->mRootNode) {
        std::cerr << "[MODEL] Cannot load: " << path << "\n";
        return {};
    }

    std::vector<GPUMesh> meshes;
    for (unsigned int m = 0; m < sc->mNumMeshes; m++) {
        aiMesh* am = sc->mMeshes[m];
        if (!am->mVertices) continue;

        std::vector<float>        verts;
        std::vector<unsigned int> idx;

        for (unsigned int i = 0; i < am->mNumVertices; i++) {
            verts.push_back(am->mVertices[i].x);
            verts.push_back(am->mVertices[i].y);
            verts.push_back(am->mVertices[i].z);
            if (am->HasNormals()) {
                verts.push_back(am->mNormals[i].x);
                verts.push_back(am->mNormals[i].y);
                verts.push_back(am->mNormals[i].z);
            }
            else {
                verts.push_back(0); verts.push_back(1); verts.push_back(0);
            }
            if (am->HasTextureCoords(0)) {
                verts.push_back(am->mTextureCoords[0][i].x);
                verts.push_back(am->mTextureCoords[0][i].y);
            }
            else {
                verts.push_back(0); verts.push_back(0);
            }
        }

        for (unsigned int i = 0; i < am->mNumFaces; i++) {
            aiFace& f = am->mFaces[i];
            for (unsigned int j = 0; j < f.mNumIndices; j++)
                idx.push_back(f.mIndices[j]);
        }

        if (buildCol) {
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                auto gv = [&](unsigned int id) -> glm::vec3 {
                    unsigned int b = id * 8;
                    return glm::vec3(T * glm::vec4(verts[b], verts[b + 1], verts[b + 2], 1.f));
                    };
                colTris.push_back({ gv(idx[i]), gv(idx[i + 1]), gv(idx[i + 2]) });
            }
        }

        // ── Texture ──────────────────────────────────────────
        unsigned int texID = 0;
        std::string  texPath;

        if (am->mMaterialIndex < sc->mNumMaterials) {
            aiMaterial* mat = sc->mMaterials[am->mMaterialIndex];
            aiString tp;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tp) == AI_SUCCESS) {
                // Try full path first
                texID = loadTexture(texDir + "/" + tp.C_Str());

                // Try basename
                if (!texID) {
                    std::string n = tp.C_Str();
                    size_t sl = n.find_last_of("/\\");
                    if (sl != std::string::npos) n = n.substr(sl + 1);
                    texID = loadTexture(texDir + "/" + n);

                    // Try alternate extensions
                    if (!texID) {
                        size_t dot = n.find_last_of('.');
                        std::string base = (dot != std::string::npos) ? n.substr(0, dot) : n;
                        if (!texID) texID = loadTexture(texDir + "/" + base + ".jpeg");
                        if (!texID) texID = loadTexture(texDir + "/" + base + ".jpg");
                        if (!texID) texID = loadTexture(texDir + "/" + base + ".png");
                    }
                }

                // Determine texPath for DX11 pixel cache lookup
                std::string rp = tp.C_Str();
                size_t sl2 = rp.find_last_of("/\\");
                if (sl2 != std::string::npos) rp = rp.substr(sl2 + 1);
                std::string noExt = rp;
                size_t dot2 = noExt.find_last_of('.');
                if (dot2 != std::string::npos) noExt = noExt.substr(0, dot2);
                texPath = texDir + "/" + rp;
                for (auto& ext : std::vector<std::string>{ rp, noExt + ".jpeg", noExt + ".jpg", noExt + ".png" }) {
                    std::string fp = texDir + "/" + ext;
                    if (texPixelCache.count(fp)) { texPath = fp; break; }
                }
            }
        }

        // ── GPUMesh ───────────────────────────────────────────
        GPUMesh mesh;
        mesh.indexCount = (unsigned int)idx.size();
        mesh.texID = texID;
        mesh.skinned = false;
        mesh.texPath = texPath;

        if (gLoadGLTextures) {
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

// ──────────────────────────────────────────────
//  uploadMeshesToDX11
// ──────────────────────────────────────────────
void uploadMeshesToDX11(std::vector<GPUMesh>& meshes, void* dev_)
{
#ifdef _WIN32
    ID3D11Device* dev = static_cast<ID3D11Device*>(dev_);
    if (!dev) return;

    for (auto& m : meshes) {
        if (m.dxVB) continue;
        if (m.cpuVerts.empty() || m.cpuIdx.empty()) {
            printf("[DX11] cpuVerts/cpuIdx empty — mesh skipped\n");
            continue;
        }

        m.dxStride = m.skinned ? (16 * sizeof(float)) : (8 * sizeof(float));

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = (UINT)m.cpuVerts.size();
        D3D11_SUBRESOURCE_DATA sd = { m.cpuVerts.data() };
        dev->CreateBuffer(&bd, &sd, (ID3D11Buffer**)&m.dxVB);

        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = (UINT)m.cpuIdx.size();
        D3D11_SUBRESOURCE_DATA sd2 = { m.cpuIdx.data() };
        dev->CreateBuffer(&bd, &sd2, (ID3D11Buffer**)&m.dxIB);

        if (!m.dxSRV && !m.texPath.empty()) {
            auto it = texPixelCache.find(m.texPath);
            if (it != texPixelCache.end()) {
                auto& px = it->second;
                D3D11_TEXTURE2D_DESC td = {};
                td.Width = px.w; td.Height = px.h;
                td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA tsd = { px.data.data(), (UINT)(px.w * 4) };
                ID3D11Texture2D* tex2d = nullptr;
                if (SUCCEEDED(dev->CreateTexture2D(&td, &tsd, &tex2d))) {
                    dev->CreateShaderResourceView(tex2d, nullptr,
                        (ID3D11ShaderResourceView**)&m.dxSRV);
                    tex2d->Release();
                }
            }
        }

        m.cpuVerts.clear(); m.cpuVerts.shrink_to_fit();
        m.cpuIdx.clear();   m.cpuIdx.shrink_to_fit();

        if (!m.dxVB || !m.dxIB)
            printf("[DX11] upload FAILED (VB=%p IB=%p)\n", m.dxVB, m.dxIB);
        else
            printf("[DX11] mesh OK (stride=%u idx=%u)\n", m.dxStride, m.indexCount);
    }
    printf("[DX11] Uploaded %zu meshes\n", meshes.size());
#else
    (void)meshes; (void)dev_;
#endif
}

// ──────────────────────────────────────────────
//  drawMeshes  (OpenGL only)
// ──────────────────────────────────────────────
void drawMeshes(const std::vector<GPUMesh>& meshes, unsigned int shader)
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