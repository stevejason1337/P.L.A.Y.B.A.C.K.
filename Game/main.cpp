#include "Window.h"
#include "DX11Core.h"
#include "Mesh.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 1. Создаем движок
    Window window(1280, 720, L"P.L.A.Y.B.A.C.K. Final Arch");
    DX11Core gfx(window.GetHWND());
    Camera playerCamera;

    // 2. Загружаем твои ресурсы (Тут твои модельки и освещение)
    // Shader shader(gfx.GetDevice(), L"Shaders/Default.hlsl");
    // Mesh playerWeapon(gfx.GetDevice(), loadModelData("Models/gun.obj"));

    // 3. Главный цикл
    while (window.ProcessMessages()) {
        float dt = 0.016f; // В идеале использовать класс Timer

        // --- ЛОГИКА ИГРЫ ---
        // Тут твое движение:
        // if(Input::IsKeyDown('W')) playerCamera.MoveForward(dt);

        // --- РЕНДЕР ---
        gfx.BeginFrame(0.1f, 0.1f, 0.15f); // Очистка экрана

        // Устанавливаем матрицы освещения и камеру
        // shader.SetCamera(playerCamera.GetViewMatrix(), playerCamera.GetProjectionMatrix(1.77f));
        // shader.Bind(gfx.GetContext());
        // playerWeapon.Bind(gfx.GetContext());

        // Рисуем!
        // gfx.GetContext()->Draw(playerWeapon.GetVertexCount(), 0);

        gfx.EndFrame();
    }
    return 0;
};