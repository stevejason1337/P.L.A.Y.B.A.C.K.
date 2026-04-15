#pragma once

// ============================================================
//  Engine/MeshRenderer.h  —  ДВИЖОК, не трогаешь
//  Компонент: рисует статическую 3D модель на GameObject.
//  Добавляется к любому объекту: player->addComponent<MeshRenderer>("models/ak47.obj");
// ============================================================

#include "Component.h"
#include "ModelLoader.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class MeshRenderer : public Component {
public:
    bool  visible      = true;
    bool  castShadow   = true;

    explicit MeshRenderer(const std::string& modelPath)
        : modelPath(modelPath) {}

    void start() override {
        model = ModelLoader::get().load(modelPath);
        if (!model) {
            std::cerr << "[MeshRenderer] Failed to load: " << modelPath << "\n";
        }
    }

    // Вызывается Renderer'ом (не через update!)
    void draw(unsigned int shaderID) const {
        if (!visible || !model || !model->loaded) return;
        glm::mat4 mat = owner->getModelMatrix();
        glUniformMatrix4fv(glGetUniformLocation(shaderID, "model"), 1, GL_FALSE, glm::value_ptr(mat));
        for (auto& mesh : model->meshes)
            mesh.draw(shaderID);
    }

    const std::string& getPath() const { return modelPath; }

private:
    std::string modelPath;
    Model*      model = nullptr;
};
