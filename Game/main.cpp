// ============================================================
//  Game/main.cpp  —  точка входа
// ============================================================

#include "../Engine/Core/Core.h"
#include "GameScene.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    Engine engine;

    EngineConfig cfg;
    cfg.title = "P.L.A.Y.B.A.C.K.";
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = true;
    cfg.fullscreen = false;

    if (!engine.init(cfg)) {
        MessageBoxA(nullptr, "Engine init failed!", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    GameScene game;
    engine.run(&game);
    return 0;
}