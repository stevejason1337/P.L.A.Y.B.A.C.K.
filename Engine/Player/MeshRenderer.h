#pragma once
// ============================================================
//  Engine/MeshRenderer.h  —  ДВИЖОК, не трогаешь
//  Компонент — рендерит статическую 3D модель.
//  Добавляй так: obj->addComponent<MeshRenderer>("models/cube.obj");
// ============================================================

#include "Component.h"
#include "ModelLoader.h"
#include "Render/Renderer.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <string>

class MeshRenderer : public Component {
public:
    Model*      model     = nullptr;
    bool        castShadow = true;
    bool        visible    = true;

    explicit MeshRenderer(const std::string& path)
        : modelPath(path) {}

    void start() override {
        model = ModelLoader::get().load(modelPath);
        if (!model)
            std::cerr << "[MeshRenderer] Failed to load: " << modelPath << "\n";
    }

    // Вызывается Renderer::drawScene()
    void draw(unsigned int shader) const {
        if (!model || !model->loaded || !visible) return;

        glm::mat4 m = owner->getModelMatrix();
        Renderer::setMat4(shader, "model", m);
        Renderer::setInt (shader, "hasTexture",
            model->meshes.empty() ? 0 :
            model->meshes[0].textures.empty() ? 0 : 1);

        for (auto& mesh : model->meshes)
            mesh.draw(shader);
    }

    const std::string& getPath() const { return modelPath; }

private:
    std::string modelPath;
};
