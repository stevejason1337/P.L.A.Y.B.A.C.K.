#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
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
        GLenum fmt = (ch==4)?GL_RGBA:(ch==3)?GL_RGB:GL_RED;
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,fmt,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        std::cout << "[TEX] OK: " << path << "\n";
    } else {
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
        aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_OptimizeMeshes);
    if (!sc||!sc->mRootNode) {
        std::cerr<<"[MODEL] Cannot load: "<<path<<"\n"; return {};
    }

    std::vector<GPUMesh> meshes;
    for (unsigned int m=0;m<sc->mNumMeshes;m++) {
        aiMesh* am=sc->mMeshes[m];
        std::vector<float> verts;
        std::vector<unsigned int> idx;

        if (!am->mVertices) continue;  // ← добавь эту строку
        for (unsigned int i=0;i<am->mNumVertices;i++) {
            verts.push_back(am->mVertices[i].x);
            verts.push_back(am->mVertices[i].y);
            verts.push_back(am->mVertices[i].z);
            if (am->HasNormals()) {
                verts.push_back(am->mNormals[i].x);
                verts.push_back(am->mNormals[i].y);
                verts.push_back(am->mNormals[i].z);
            } else { verts.push_back(0);verts.push_back(1);verts.push_back(0); }
            if (am->HasTextureCoords(0)) {
                verts.push_back(am->mTextureCoords[0][i].x);
                verts.push_back(am->mTextureCoords[0][i].y);
            } else { verts.push_back(0);verts.push_back(0); }
        }

        for (unsigned int i=0;i<am->mNumFaces;i++) {
            aiFace& f=am->mFaces[i];
            for (unsigned int j=0;j<f.mNumIndices;j++) idx.push_back(f.mIndices[j]);
        }

        if (buildCol) {
            for (size_t i=0;i+2<idx.size();i+=3) {
                auto gv=[&](unsigned int id)->glm::vec3 {
                    unsigned int b=id*8;
                    return glm::vec3(T*glm::vec4(verts[b],verts[b+1],verts[b+2],1.f));
                };
                colTris.push_back({gv(idx[i]),gv(idx[i+1]),gv(idx[i+2])});
            }
        }

        unsigned int texID=0;
        if (am->mMaterialIndex<sc->mNumMaterials) {
            aiMaterial* mat=sc->mMaterials[am->mMaterialIndex];
            aiString tp;
            if (mat->GetTexture(aiTextureType_DIFFUSE,0,&tp)==AI_SUCCESS) {
                texID=loadTexture(texDir+"/"+tp.C_Str());
                if (!texID) {
                    std::string n=tp.C_Str();
                    size_t sl=n.find_last_of("/\\");
                    if (sl!=std::string::npos) n=n.substr(sl+1);
                    texID=loadTexture(texDir+"/"+n);
                }
            }
        }

        GPUMesh mesh;
        mesh.indexCount=(unsigned int)idx.size();
        mesh.texID=texID; mesh.skinned=false;
        glGenVertexArrays(1,&mesh.VAO);
        glGenBuffers(1,&mesh.VBO);
        glGenBuffers(1,&mesh.EBO);
        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER,mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(float),verts.data(),GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(unsigned int),idx.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
        meshes.push_back(mesh);
    }
    std::cout<<"[MODEL] Loaded: "<<path<<" meshes:"<<meshes.size()<<"\n";
    return meshes;
}

inline void drawMeshes(const std::vector<GPUMesh>& meshes, unsigned int shader)
{
    for (const GPUMesh& m:meshes) {
        if (m.texID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,m.texID);
            glUniform1i(glGetUniformLocation(shader,"hasTexture"),1);
            glUniform1i(glGetUniformLocation(shader,"tex"),0);
        } else {
            glUniform1i(glGetUniformLocation(shader,"hasTexture"),0);
        }
        glBindVertexArray(m.VAO);
        glDrawElements(GL_TRIANGLES,m.indexCount,GL_UNSIGNED_INT,0);
    }
}
