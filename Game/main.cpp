// ============================================================
//  Game/main.cpp  —  ИГРА — точка входа
//  Создаёт движок, передаёт ему GameScene и запускает.
//  Больше ничего здесь не делай — логика в GameScene.h
// ============================================================

#include "../Engine/Core.h"
#include "GameScene.h"

int main() {
    Engine engine;

    EngineConfig cfg;
    cfg.title      = "P.L.A.Y.B.A.C.K.";
    cfg.width      = 1280;
    cfg.height     = 720;
    cfg.vsync      = true;
    cfg.fullscreen = false;
    cfg.msaa       = 4;

    if (!engine.init(cfg)) {
        std::cerr << "[Main] Engine init failed!\n";
        return -1;
    }

    GameScene game;
    engine.run(&game);

    return 0;
}
