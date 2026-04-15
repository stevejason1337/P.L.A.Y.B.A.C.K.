#pragma once

// ============================================================
//  Engine/GameObject.h  —  ДВИЖОК, не трогаешь
//  Базовый объект сцены. Знает только о компонентах.
//  НЕ знает о Player, Enemy, Weapon — никогда.
// ============================================================

#include <vector>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "Component.h"

class GameObject {
public:
    // ── Трансформ ──────────────────────────────────────────
    std::string  name     = "GameObject";
    glm::vec3    position = glm::vec3(0.f);
    glm::quat    rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3    scale    = glm::vec3(1.f);
    bool         active   = true;

    // ── Компоненты ─────────────────────────────────────────

    // Добавить компонент. Автоматически устанавливает owner и вызывает start()
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        auto c    = std::make_unique<T>(std::forward<Args>(args)...);
        T*   ptr  = c.get();
        c->owner  = this;
        components.push_back(std::move(c));
        ptr->start();
        return ptr;
    }

    // Получить первый компонент данного типа
    template<typename T>
    T* getComponent() {
        for (auto& c : components)
            if (auto* p = dynamic_cast<T*>(c.get()))
                return p;
        return nullptr;
    }

    // Получить все компоненты данного типа
    template<typename T>
    std::vector<T*> getComponents() {
        std::vector<T*> result;
        for (auto& c : components)
            if (auto* p = dynamic_cast<T*>(c.get()))
                result.push_back(p);
        return result;
    }

    // Есть ли компонент данного типа
    template<typename T>
    bool hasComponent() { return getComponent<T>() != nullptr; }

    // Удалить компонент данного типа
    template<typename T>
    void removeComponent() {
        for (auto it = components.begin(); it != components.end(); ++it) {
            if (dynamic_cast<T*>(it->get())) {
                (*it)->onDestroy();
                components.erase(it);
                return;
            }
        }
    }

    // ── Апдейт (вызывает Scene) ────────────────────────────
    void update(float dt) {
        if (!active) return;
        for (auto& c : components)
            c->update(dt);
    }

    void destroy() {
        for (auto& c : components)
            c->onDestroy();
        components.clear();
        markedForDestroy = true;
    }

    bool isMarkedForDestroy() const { return markedForDestroy; }

    // ── Утилиты трансформа ─────────────────────────────────
    glm::mat4 getModelMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.f), position);
        glm::mat4 r = glm::mat4_cast(rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.f), scale);
        return t * r * s;
    }

    glm::vec3 forward() const {
        return glm::normalize(rotation * glm::vec3(0.f, 0.f, -1.f));
    }
    glm::vec3 right() const {
        return glm::normalize(rotation * glm::vec3(1.f, 0.f, 0.f));
    }
    glm::vec3 up() const {
        return glm::normalize(rotation * glm::vec3(0.f, 1.f, 0.f));
    }

    void setEulerAngles(float pitchDeg, float yawDeg, float rollDeg) {
        rotation = glm::quat(glm::vec3(
            glm::radians(pitchDeg),
            glm::radians(yawDeg),
            glm::radians(rollDeg)
        ));
    }

private:
    std::vector<std::unique_ptr<Component>> components;
    bool markedForDestroy = false;
};
