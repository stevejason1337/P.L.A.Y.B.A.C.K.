#pragma once

// ============================================================
//  Engine/Component.h  —  ДВИЖОК, не трогаешь
//  Базовый класс для всех компонентов (как в Unity)
// ============================================================

class GameObject;   // forward-declare, чтобы не включать GameObject.h

class Component {
public:
    GameObject* owner = nullptr;    // устанавливается автоматически в addComponent

    // Вызывается один раз после addComponent
    virtual void start() {}

    // Вызывается каждый кадр
    virtual void update(float dt) {}

    // Вызывается при уничтожении объекта
    virtual void onDestroy() {}

    virtual ~Component() = default;
};
