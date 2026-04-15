#pragma once

// ============================================================
//  Engine/AnimatedModel.h  —  ДВИЖОК, не трогаешь
//  Компонент для анимированных скелетных моделей.
//  Загружает FBX/GLTF с анимациями через Assimp.
//  Игра вызывает только: play("run"), play("shoot"), stop()
// ============================================================

#include "Component.h"
#include "ModelLoader.h"   // для Vertex, Mesh

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/anim.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

// Максимум костей в шейдере
static constexpr int MAX_BONES         = 100;
static constexpr int MAX_BONE_INFLUENCE= 4;

struct BoneVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    int       boneIDs[MAX_BONE_INFLUENCE]   = {-1,-1,-1,-1};
    float     weights[MAX_BONE_INFLUENCE]   = { 0, 0, 0, 0};
};

struct BoneInfo {
    int       id;
    glm::mat4 offset;   // bind pose inverse
};

struct AnimMesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int indexCount = 0;
    std::vector<MeshTexture> textures;
    void draw() const {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

class AnimatedModel : public Component {
public:
    unsigned int animShader = 0;    // шейдер со skinning (выставь снаружи)

    explicit AnimatedModel(const std::string& path) : modelPath(path) {}

    void start() override { load(modelPath); }

    void update(float dt) override {
        if (!loaded || currentAnim < 0) return;
        animTime += dt * playbackSpeed;
        const aiAnimation* anim = scene->mAnimations[currentAnim];
        float tps  = (float)(anim->mTicksPerSecond > 0 ? anim->mTicksPerSecond : 25.0);
        float ticks = animTime * tps;
        if (loop) ticks = fmod(ticks, (float)anim->mDuration);
        else      ticks = glm::clamp(ticks, 0.f, (float)anim->mDuration);
        computeBoneTransforms(ticks, anim, scene->mRootNode, glm::mat4(1.f));
    }

    // ── API для игрового кода ──────────────────────────────
    void play(const std::string& animName, bool doLoop = true) {
        for (unsigned i = 0; i < scene->mNumAnimations; i++) {
            if (std::string(scene->mAnimations[i]->mName.C_Str()) == animName) {
                currentAnim   = (int)i;
                animTime      = 0.f;
                loop          = doLoop;
                return;
            }
        }
        std::cerr << "[AnimatedModel] Anim not found: " << animName << "\n";
    }

    void stop() { currentAnim = -1; }

    float playbackSpeed = 1.f;

    // Вызывается Renderer'ом
    void draw() const {
        if (!loaded || animShader == 0) return;
        glUseProgram(animShader);
        for (int i = 0; i < (int)boneMatrices.size(); i++) {
            std::string uni = "finalBonesMatrices[" + std::to_string(i) + "]";
            glUniformMatrix4fv(glGetUniformLocation(animShader, uni.c_str()),
                               1, GL_FALSE, glm::value_ptr(boneMatrices[i]));
        }
        glm::mat4 model = owner->getModelMatrix();
        glUniformMatrix4fv(glGetUniformLocation(animShader, "model"),
                           1, GL_FALSE, glm::value_ptr(model));
        for (auto& m : meshes) m.draw();
    }

    bool isLoaded() const { return loaded; }

private:
    std::string modelPath;
    bool loaded = false;
    int  currentAnim = -1;
    float animTime   = 0.f;
    bool  loop       = true;

    const aiScene* scene = nullptr;
    Assimp::Importer importer;   // держим живым — aiScene принадлежит ему

    std::vector<AnimMesh>   meshes;
    std::unordered_map<std::string, BoneInfo> boneMap;
    int boneCount = 0;
    std::vector<glm::mat4>  boneMatrices;
    glm::mat4               globalInverse;

    void load(const std::string& path) {
        scene = importer.ReadFile(path,
            aiProcess_Triangulate | aiProcess_FlipUVs |
            aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights);

        if (!scene) {
            std::cerr << "[AnimatedModel] " << importer.GetErrorString() << "\n";
            return;
        }

        globalInverse = glm::inverse(aiToGlm(scene->mRootNode->mTransformation));
        boneMatrices.resize(MAX_BONES, glm::mat4(1.f));

        std::string dir = path.substr(0, path.find_last_of('/'));
        for (unsigned i = 0; i < scene->mNumMeshes; i++)
            meshes.push_back(processMesh(scene->mMeshes[i], scene, dir));
        loaded = true;
    }

    AnimMesh processMesh(aiMesh* mesh, const aiScene* sc, const std::string& dir);

    void computeBoneTransforms(float tick, const aiAnimation* anim,
                                aiNode* node, const glm::mat4& parent) {
        std::string name = node->mName.C_Str();
        glm::mat4   nodeT = aiToGlm(node->mTransformation);

        for (unsigned i = 0; i < anim->mNumChannels; i++) {
            if (anim->mChannels[i]->mNodeName.C_Str() == name) {
                nodeT = interpolateBone(anim->mChannels[i], tick);
                break;
            }
        }

        glm::mat4 global = parent * nodeT;

        if (boneMap.count(name)) {
            int idx = boneMap[name].id;
            boneMatrices[idx] = globalInverse * global * boneMap[name].offset;
        }

        for (unsigned i = 0; i < node->mNumChildren; i++)
            computeBoneTransforms(tick, anim, node->mChildren[i], global);
    }

    glm::mat4 interpolateBone(const aiNodeAnim* ch, float tick);

    static glm::mat4 aiToGlm(const aiMatrix4x4& m) {
        return glm::mat4(
            m.a1,m.b1,m.c1,m.d1,  m.a2,m.b2,m.c2,m.d2,
            m.a3,m.b3,m.c3,m.d3,  m.a4,m.b4,m.c4,m.d4);
    }
};

// ── Реализации (в .h чтобы не нужен .cpp) ─────────────────

inline glm::mat4 AnimatedModel::interpolateBone(const aiNodeAnim* ch, float tick) {
    // Position
    glm::vec3 pos(0.f);
    if (ch->mNumPositionKeys == 1) {
        auto& k = ch->mPositionKeys[0].mValue;
        pos = {k.x, k.y, k.z};
    } else {
        for (unsigned i = 0; i < ch->mNumPositionKeys - 1; i++) {
            if (tick < (float)ch->mPositionKeys[i+1].mTime) {
                float t = (float)(tick - ch->mPositionKeys[i].mTime) /
                          (float)(ch->mPositionKeys[i+1].mTime - ch->mPositionKeys[i].mTime);
                auto& a = ch->mPositionKeys[i].mValue;
                auto& b = ch->mPositionKeys[i+1].mValue;
                pos = glm::mix(glm::vec3(a.x,a.y,a.z), glm::vec3(b.x,b.y,b.z), t);
                break;
            }
        }
    }
    // Rotation
    aiQuaternion rot;
    if (ch->mNumRotationKeys == 1) {
        rot = ch->mRotationKeys[0].mValue;
    } else {
        for (unsigned i = 0; i < ch->mNumRotationKeys - 1; i++) {
            if (tick < (float)ch->mRotationKeys[i+1].mTime) {
                float t = (float)(tick - ch->mRotationKeys[i].mTime) /
                          (float)(ch->mRotationKeys[i+1].mTime - ch->mRotationKeys[i].mTime);
                aiQuaternion::Interpolate(rot, ch->mRotationKeys[i].mValue,
                                          ch->mRotationKeys[i+1].mValue, t);
                rot.Normalize();
                break;
            }
        }
    }
    // Scale
    glm::vec3 scl(1.f);
    if (ch->mNumScalingKeys == 1) {
        auto& k = ch->mScalingKeys[0].mValue;
        scl = {k.x, k.y, k.z};
    } else {
        for (unsigned i = 0; i < ch->mNumScalingKeys - 1; i++) {
            if (tick < (float)ch->mScalingKeys[i+1].mTime) {
                float t = (float)(tick - ch->mScalingKeys[i].mTime) /
                          (float)(ch->mScalingKeys[i+1].mTime - ch->mScalingKeys[i].mTime);
                auto& a = ch->mScalingKeys[i].mValue;
                auto& b = ch->mScalingKeys[i+1].mValue;
                scl = glm::mix(glm::vec3(a.x,a.y,a.z), glm::vec3(b.x,b.y,b.z), t);
                break;
            }
        }
    }

    glm::mat4 T = glm::translate(glm::mat4(1.f), pos);
    glm::mat4 R = glm::mat4_cast(glm::quat(rot.w, rot.x, rot.y, rot.z));
    glm::mat4 S = glm::scale(glm::mat4(1.f), scl);
    return T * R * S;
}
