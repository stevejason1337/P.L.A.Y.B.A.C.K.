#pragma once
// ============================================================
//  Engine/Render/RendererDrawScene.h
//  Реализация Renderer::drawScene — отдельно чтобы не было
//  circular include между Renderer.h и Scene.h/MeshRenderer.h
//
//  Включи этот файл ТОЛЬКО в один .cpp (например Core.cpp)
//  или в конец Renderer.cpp после всех других includes.
// ============================================================

#include "Renderer.h"
#include "../Scene.h"
#include "../MeshRenderer.h"
#include "../AnimatedModel.h"

inline void Renderer::drawScene(Scene& scene) {
    if (screenW == 0 || screenH == 0) return;

    float aspect = getAspect();
    glm::mat4 view = camera.getView();
    glm::mat4 proj = camera.getProjection(aspect);
    glm::vec3 lightDir = glm::normalize(sun.direction);
    glm::vec3 lightCol = sun.color * sun.intensity;

    // ── Статические меши ──────────────────────────────────
    glUseProgram(staticShader);
    setMat4(staticShader, "view",       view);
    setMat4(staticShader, "projection", proj);
    setVec3(staticShader, "lightDir",   lightDir);
    setVec3(staticShader, "lightColor", lightCol);
    setVec3(staticShader, "viewPos",    camera.position);

    for (auto& obj : scene.getAll()) {
        if (!obj->active) continue;
        if (auto* mr = obj->getComponent<MeshRenderer>())
            mr->draw(staticShader);
    }

    // ── Анимированные меши ────────────────────────────────
    glUseProgram(animShader);
    setMat4(animShader, "view",       view);
    setMat4(animShader, "projection", proj);
    setVec3(animShader, "lightDir",   lightDir);
    setVec3(animShader, "lightColor", lightCol);
    setVec3(animShader, "viewPos",    camera.position);

    for (auto& obj : scene.getAll()) {
        if (!obj->active) continue;
        if (auto* am = obj->getComponent<AnimatedModel>()) {
            am->animShader = animShader;
            am->draw();
        }
    }
}
