#pragma once

// ============================================================
//  Engine/Scene.h  —  ДВИЖОК, не трогаешь
//  Управляет списком всех GameObject.
//  Знает только о GameObject, не знает об игре.
// ============================================================

#include <vector>
#include <memory>
#include <string>
#include <functional>

#include "GameObject.h"

class Scene {
public:
    // Создать пустой объект
    GameObject* createObject(const std::string& name = "GameObject") {
        auto obj    = std::make_unique<GameObject>();
        obj->name   = name;
        GameObject* ptr = obj.get();
        objects.push_back(std::move(obj));
        return ptr;
    }

    // Найти объект по имени
    GameObject* find(const std::string& name) {
        for (auto& o : objects)
            if (o->name == name) return o.get();
        return nullptr;
    }

    // Найти все объекты с компонентом T
    template<typename T>
    std::vector<T*> findComponents() {
        std::vector<T*> result;
        for (auto& o : objects)
            if (auto* c = o->getComponent<T>())
                result.push_back(c);
        return result;
    }

    // Удалить объект
    void destroy(GameObject* obj) {
        if (obj) obj->destroy();
    }

    // Обновить все объекты (вызывает Core)
    void update(float dt) {
        for (auto& o : objects)
            o->update(dt);

        // Удалить объекты помеченные destroy()
        objects.erase(
            std::remove_if(objects.begin(), objects.end(),
                [](const std::unique_ptr<GameObject>& o) {
                    return o->isMarkedForDestroy();
                }),
            objects.end()
        );
    }

    const std::vector<std::unique_ptr<GameObject>>& getAll() const {
        return objects;
    }

    void clear() { objects.clear(); }

private:
    std::vector<std::unique_ptr<GameObject>> objects;
};
