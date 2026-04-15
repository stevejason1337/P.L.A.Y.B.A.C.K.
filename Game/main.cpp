// ============================================================
//  Game/main.cpp  —  ИГРА, трогаешь здесь
//
//  Точка входа. 10 строк. Больше никогда не трогаешь.
//  Вся логика — в GameScene.h и компонентах.
//
//  Новая сущность в игре? → создай MyComponent.h в /Game/
//  Баг в рендере/физике? → смотри Engine/
// ============================================================

#include "../Engine/Core.h"
#include "GameScene.h"

int main() {
    EngineConfig cfg;
    cfg.title      = "P.L.A.Y.B.A.C.K.";
    cfg.width      = 1280;
    cfg.height     = 720;
    cfg.vsync      = true;
    cfg.msaa       = 4;
    cfg.fullscreen = false;

    Engine engine;
    if (!engine.init(cfg)) return -1;

    GameScene game;
    engine.run(&game);   // движок вызовет onStart → loop → onShutdown

    return 0;
}
