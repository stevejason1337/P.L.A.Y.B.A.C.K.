#pragma once
// ============================================================
//  Engine/Animations/ModelLoader.h  —  DirectX 11
// ============================================================

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "stb_image.h"

#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>

#include "../Render/Renderer.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────
//  DX11Mesh  —  реализация абстрактного Mesh из Renderer.h
// ─────────────────────────────────────────────────────────────
struct MeshTexture {
    ComPtr<ID3D11ShaderResourceView> srv;
    std::string type;
    std::string path;
};

class DX11Mesh : public Mesh {
public:
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> indexBuffer;
    UINT                 indexCount = 0;
    std::vector<MeshTexture> textures;

    ID3D11Buffer* const* GetVertexBufferAddress() const override {
        return vertexBuffer.GetAddressOf();
    }
    ID3D11Buffer* GetIndexBuffer() const override { return indexBuffer.Get(); }
    UINT          GetIndexCount()  const override { return indexCount; }

    void BindTextures(ID3D11DeviceContext* ctx) const {
        for (auto& t : textures) {
            if (t.type == "diffuse" && t.srv)
                ctx->PSSetShaderResources(0, 1, t.srv.GetAddressOf());
        }
    }
};

// ─────────────────────────────────────────────────────────────
//  Model
// ─────────────────────────────────────────────────────────────
struct Model {
    std::vector<DX11Mesh> meshes;
    bool loaded = false;
};

// ─────────────────────────────────────────────────────────────
//  ModelLoader  —  синглтон
// ─────────────────────────────────────────────────────────────
class ModelLoader {
public:
    static ModelLoader& get() {
        static ModelLoader instance;
        return instance;
    }

    void setDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        m_device = dev;
        m_context = ctx;
    }

    Model* load(const std::string& path) {
        if (m_cache.count(path)) return &m_cache[path];
        if (!m_device) {
            std::cerr << "[ModelLoader] setDevice() не вызван!\n";
            return nullptr;
        }

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace |
            aiProcess_ConvertToLeftHanded);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            std::cerr << "[ModelLoader] " << importer.GetErrorString() << "\n";
            return nullptr;
        }

        Model& model = m_cache[path];
        model.loaded = true;
        std::string dir = path.substr(0, path.find_last_of("/\\"));
        processNode(scene->mRootNode, scene, model, dir);
        return &model;
    }

    // Рисует все меши модели — вызывай из Player/Enemy/etc.
    void draw(const Model* model, Renderer& renderer,
        const XMMATRIX& world, const XMMATRIX& view, const XMMATRIX& proj) {
        if (!model || !model->loaded) return;
        for (auto& mesh : model->meshes) {
            mesh.BindTextures(m_context);
            RenderObject ro;
            ro.mesh = const_cast<DX11Mesh*>(&mesh);
            ro.worldMatrix = world;
            renderer.RenderObject(ro, view, proj);
        }
    }

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    std::unordered_map<std::string, Model>                           m_cache;
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> m_texCache;

    void processNode(aiNode* node, const aiScene* scene, Model& model, const std::string& dir) {
        for (unsigned i = 0; i < node->mNumMeshes; i++)
            model.meshes.push_back(processMesh(scene->mMeshes[node->mMeshes[i]], scene, dir));
        for (unsigned i = 0; i < node->mNumChildren; i++)
            processNode(node->mChildren[i], scene, model, dir);
    }

    DX11Mesh processMesh(aiMesh* mesh, const aiScene* scene, const std::string& dir) {
        std::vector<Vertex>       vertices;
        std::vector<unsigned int> indices;

        for (unsigned i = 0; i < mesh->mNumVertices; i++) {
            Vertex v{};
            v.position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
            v.normal = mesh->HasNormals()
                ? XMFLOAT3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z)
                : XMFLOAT3(0.f, 1.f, 0.f);
            v.uv = mesh->mTextureCoords[0]
                ? XMFLOAT2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
                : XMFLOAT2(0.f, 0.f);
            v.tangent = mesh->HasTangentsAndBitangents()
                ? XMFLOAT3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z)
                : XMFLOAT3(0.f, 0.f, 0.f);
            for (int j = 0; j < 4; j++) { v.boneIDs[j] = -1; v.weights[j] = 0.f; }
            vertices.push_back(v);
        }

        for (unsigned i = 0; i < mesh->mNumFaces; i++)
            for (unsigned j = 0; j < mesh->mFaces[i].mNumIndices; j++)
                indices.push_back(mesh->mFaces[i].mIndices[j]);

        DX11Mesh result;
        result.indexCount = (UINT)indices.size();

        D3D11_BUFFER_DESC vbd{};
        vbd.Usage = D3D11_USAGE_IMMUTABLE;
        vbd.ByteWidth = (UINT)(sizeof(Vertex) * vertices.size());
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vsd{ vertices.data() };
        m_device->CreateBuffer(&vbd, &vsd, &result.vertexBuffer);

        D3D11_BUFFER_DESC ibd{};
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        ibd.ByteWidth = (UINT)(sizeof(unsigned int) * indices.size());
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA isd{ indices.data() };
        m_device->CreateBuffer(&ibd, &isd, &result.indexBuffer);

        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
            loadTextures(mat, aiTextureType_DIFFUSE, "diffuse", dir, result.textures);
            loadTextures(mat, aiTextureType_SPECULAR, "specular", dir, result.textures);
            loadTextures(mat, aiTextureType_HEIGHT, "normal", dir, result.textures);
        }
        return result;
    }

    void loadTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName,
        const std::string& dir, std::vector<MeshTexture>& out) {
        for (unsigned i = 0; i < mat->GetTextureCount(type); i++) {
            aiString str;
            mat->GetTexture(type, i, &str);
            std::string fullPath = dir + "/" + str.C_Str();
            MeshTexture mt;
            mt.type = typeName;
            mt.path = fullPath;
            if (m_texCache.count(fullPath)) {
                mt.srv = m_texCache[fullPath];
            }
            else {
                mt.srv = loadTextureSRV(fullPath);
                m_texCache[fullPath] = mt.srv;
            }
            out.push_back(std::move(mt));
        }
    }

    ComPtr<ID3D11ShaderResourceView> loadTextureSRV(const std::string& path) {
        int w, h, ch;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data) {
            std::cerr << "[ModelLoader] Texture failed: " << path << "\n";
            return nullptr;
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{ data, (UINT)(w * 4), 0 };
        ComPtr<ID3D11Texture2D> tex;
        m_device->CreateTexture2D(&td, &sd, &tex);
        stbi_image_free(data);
        if (!tex) return nullptr;
        ComPtr<ID3D11ShaderResourceView> srv;
        m_device->CreateShaderResourceView(tex.Get(), nullptr, &srv);
        return srv;
    }
};