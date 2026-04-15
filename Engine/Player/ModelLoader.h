#pragma once

// ============================================================
//  Engine/ModelLoader.h  —  ДВИЖОК, не трогаешь
//  Загружает статические 3D модели через Assimp + OpenGL.
//  Не знает о Player/Enemy — только меши и текстуры.
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>

#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
};

struct MeshTexture {
    unsigned int id   = 0;
    std::string  type;      // "diffuse" | "normal" | "specular"
    std::string  path;
};

struct Mesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int indexCount = 0;
    std::vector<MeshTexture> textures;

    void draw(unsigned int shaderID) const {
        unsigned int diffuse = 0, specular = 0, normal = 0;
        for (auto& t : textures) {
            if      (t.type == "diffuse")  { glActiveTexture(GL_TEXTURE0 + diffuse++);  }
            else if (t.type == "specular") { glActiveTexture(GL_TEXTURE0 + 4 + specular++); }
            else if (t.type == "normal")   { glActiveTexture(GL_TEXTURE0 + 8 + normal++);  }
            glBindTexture(GL_TEXTURE_2D, t.id);
        }
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

struct Model {
    std::vector<Mesh> meshes;
    std::string       directory;
    bool              loaded = false;
};

class ModelLoader {
public:
    static ModelLoader& get() {
        static ModelLoader instance;
        return instance;
    }

    // Загрузить модель (кешируется)
    Model* load(const std::string& path) {
        if (cache.count(path)) return &cache[path];

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "[ModelLoader] " << importer.GetErrorString() << "\n";
            return nullptr;
        }

        Model& model    = cache[path];
        model.directory = path.substr(0, path.find_last_of('/'));
        model.loaded    = true;

        processNode(scene->mRootNode, scene, model);
        return &model;
    }

private:
    std::unordered_map<std::string, Model>        cache;
    std::unordered_map<std::string, unsigned int> textureCache;

    void processNode(aiNode* node, const aiScene* scene, Model& model) {
        for (unsigned i = 0; i < node->mNumMeshes; i++)
            model.meshes.push_back(processMesh(scene->mMeshes[node->mMeshes[i]], scene, model.directory));
        for (unsigned i = 0; i < node->mNumChildren; i++)
            processNode(node->mChildren[i], scene, model);
    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene, const std::string& dir) {
        std::vector<Vertex>       vertices;
        std::vector<unsigned int> indices;
        std::vector<MeshTexture>  textures;

        for (unsigned i = 0; i < mesh->mNumVertices; i++) {
            Vertex v;
            v.position  = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
            v.normal    = mesh->HasNormals()
                ? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z)
                : glm::vec3(0.f, 1.f, 0.f);
            v.texCoords = mesh->mTextureCoords[0]
                ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
                : glm::vec2(0.f);
            v.tangent   = mesh->HasTangentsAndBitangents()
                ? glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z)
                : glm::vec3(0.f);
            vertices.push_back(v);
        }

        for (unsigned i = 0; i < mesh->mNumFaces; i++)
            for (unsigned j = 0; j < mesh->mFaces[i].mNumIndices; j++)
                indices.push_back(mesh->mFaces[i].mIndices[j]);

        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
            auto diff = loadMaterialTextures(mat, aiTextureType_DIFFUSE,  "diffuse",  dir);
            auto spec = loadMaterialTextures(mat, aiTextureType_SPECULAR, "specular", dir);
            auto norm = loadMaterialTextures(mat, aiTextureType_HEIGHT,   "normal",   dir);
            textures.insert(textures.end(), diff.begin(), diff.end());
            textures.insert(textures.end(), spec.begin(), spec.end());
            textures.insert(textures.end(), norm.begin(), norm.end());
        }

        return setupMesh(vertices, indices, textures);
    }

    Mesh setupMesh(const std::vector<Vertex>& vertices,
                   const std::vector<unsigned int>& indices,
                   const std::vector<MeshTexture>& textures) {
        Mesh m;
        m.textures   = textures;
        m.indexCount = (unsigned int)indices.size();

        glGenVertexArrays(1, &m.VAO);
        glGenBuffers(1, &m.VBO);
        glGenBuffers(1, &m.EBO);

        glBindVertexArray(m.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        // position
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        glEnableVertexAttribArray(0);
        // normal
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(1);
        // texcoords
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));
        glEnableVertexAttribArray(2);
        // tangent
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
        glEnableVertexAttribArray(3);

        glBindVertexArray(0);
        return m;
    }

    std::vector<MeshTexture> loadMaterialTextures(aiMaterial* mat, aiTextureType type,
                                                   const std::string& typeName,
                                                   const std::string& dir) {
        std::vector<MeshTexture> textures;
        for (unsigned i = 0; i < mat->GetTextureCount(type); i++) {
            aiString str;
            mat->GetTexture(type, i, &str);
            std::string fullPath = dir + "/" + str.C_Str();

            if (textureCache.count(fullPath)) {
                textures.push_back({textureCache[fullPath], typeName, fullPath});
                continue;
            }

            unsigned int texID = loadTextureFromFile(fullPath);
            textureCache[fullPath] = texID;
            textures.push_back({texID, typeName, fullPath});
        }
        return textures;
    }

    unsigned int loadTextureFromFile(const std::string& path) {
        unsigned int id;
        glGenTextures(1, &id);

        int w, h, ch;
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
        } else {
            std::cerr << "[ModelLoader] Texture failed: " << path << "\n";
        }
        stbi_image_free(data);
        return id;
    }
};
