#pragma once
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <string>
#include <iostream>

struct SoundManager {
    ma_engine engine;
    bool  ready = false;
    float stepTimer = 0.f;

    void init() {
        if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
            std::cerr << "[SOUND] Failed to init miniaudio!\n"; return;
        }
        ready = true;
        ma_engine_set_volume(&engine, 1.0f);
        std::cout << "[SOUND] Sound System Ready.\n";
    }

    void shutdown() { if (ready) ma_engine_uninit(&engine); }

    void play(const std::string& path) {
        if (!ready) return;
        ma_engine_play_sound(&engine, path.c_str(), NULL);
    }

    // ── Звук выстрела — вызывать ТОЛЬКО если выстрел реально произошёл ──
    // (проверку ammo/reloading делает doShoot, сюда попадаем только при успехе)
    void playShot(int weaponIdx) {
        if (!ready) return;
        switch (weaponIdx) {
        case 0: play("sounds/glock/glock.mp3");   break;  // Glock
        case 1: play("sounds/glock/glock.mp3");   break;  // Sawnoff (замени на свой файл)
        case 2: play("sounds/ak74/ak74.mp3");     break;  // AK-74
        default: play("sounds/glock/glock.mp3");  break;
        }
    }

    // ── Звук перезарядки ──
    void playReload(int weaponIdx) {
        if (!ready) return;
        switch (weaponIdx) {
        case 0: play("sounds/glock/reload_glock.mp3");  break;
        case 1: play("sounds/glock/reload_glock.mp3");  break;
        case 2: play("sounds/ak74/reload_ak74.mp3");    break;
        default: play("sounds/glock/reload_glock.mp3"); break;
        }
    }

    // ── Звук пустого магазина (клик) ──
    void playEmpty() {
        if (!ready) return;
        play("sounds/empty_click.mp3");
    }

    // ── Шаги ──
    void playFootstep(float dt, bool moving, bool onGround) {
        if (!moving || !onGround) {
            stepTimer = 0.05f;
            return;
        }
        stepTimer -= dt;
        if (stepTimer <= 0.f) {
            play("sounds/footstep.mp3");
            extern struct Character player;
            stepTimer = player.sprinting ? 0.3f : 0.5f;
        }
    }
};

extern SoundManager soundManager;