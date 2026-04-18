#pragma once
// ============================================================
//  Game/GameScene.h  —  ИГРА
//  Вся логика здесь. Подключай Player.h, Enemy.h и т.д. сюда.
//  Движок вызывает onInit / onUpdate / onRender.
// ============================================================

#include "../Engine/Core/Core.h"
#include "../Engine/Animations/ModelLoader.h"
// #include "Player.h"
// #include "Enemy.h"
// #include "WeaponManager.h"

class GameScene : public IScene {
public:
    bool onInit(Renderer& renderer) override {
        // Даём ModelLoader доступ к DX11 устройству
        ModelLoader::get().setDevice(
            renderer.GetDevice(),
            renderer.GetContext());

        // Загружаем модели:
        // m_playerModel = ModelLoader::get().load("models/player.fbx");

        // Настраиваем камеру и свет
        XMFLOAT3 camPos = { 0.f, 2.f, 5.f };
        XMFLOAT3 lightDir = { -0.4f, -1.f, -0.6f };

        m_view = XMMatrixLookAtLH(
            XMLoadFloat3(&camPos),
            XMVectorSet(0, 0, 0, 1),
            XMVectorSet(0, 1, 0, 0));
        m_proj = XMMatrixPerspectiveFovLH(
            XMConvertToRadians(75.f),
            (float)renderer.GetWidth() / renderer.GetHeight(),
            0.1f, 500.f);

        renderer.SetFrameData(m_view, m_proj, camPos, lightDir, 1.2f);
        return true;
    }

    void onUpdate(float dt) override {
        m_time += dt;
        // Логика: player.update(dt), enemy.update(dt) и т.д.
    }

    void onRender(Renderer& renderer, float dt) override {
        // Рендер моделей:
        // XMMATRIX world = XMMatrixIdentity();
        // ModelLoader::get().draw(m_playerModel, renderer, world, m_view, m_proj);
    }

    void onShutdown() override {}

private:
    float    m_time = 0.f;
    XMMATRIX m_view = XMMatrixIdentity();
    XMMATRIX m_proj = XMMatrixIdentity();
    Model* m_playerModel = nullptr;
};