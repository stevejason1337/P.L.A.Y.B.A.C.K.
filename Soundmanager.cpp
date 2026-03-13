// Soundmanager.cpp
// MINIAUDIO_IMPLEMENTATION — ровно один раз, только здесь
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Soundmanager.h"
#include "Player.h"    // для player.sprinting в playFootstep
#include <iostream>

// ── Definition of the global instance ────────────────────────
SoundManager soundManager;

// ── private helper ────────────────────────────────────────────
void SoundManager::_play(ma_sound& s)
{
    ma_sound_seek_to_pcm_frame(&s, 0);
    ma_sound_start(&s);
}

// ── init ──────────────────────────────────────────────────────
void SoundManager::init()
{
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
        std::cerr << "[SOUND] Failed to init miniaudio!\n";
        return;
    }
    ready = true;
    ma_engine_set_volume(&engine, 1.0f);

    const char* shotFiles[] = {
        "sounds/glock/glock.mp3",
        "sounds/glock/glock.mp3",
        "sounds/ak74/ak74.mp3"
    };
    const char* reloadFiles[] = {
        "sounds/glock/reload_glock.mp3",
        "sounds/glock/reload_glock.mp3",
        "sounds/ak74/reload_ak74.mp3"
    };

    for (int w = 0; w < 3; w++) {
        for (int v = 0; v < SND_SHOT_VOICES; v++) {
            if (ma_sound_init_from_file(&engine, shotFiles[w],
                MA_SOUND_FLAG_DECODE, NULL, NULL, &shotSounds[w][v]) == MA_SUCCESS)
                shotLoaded[w] = true;
        }
        if (ma_sound_init_from_file(&engine, reloadFiles[w],
            MA_SOUND_FLAG_DECODE, NULL, NULL, &reloadSounds[w]) == MA_SUCCESS)
            reloadLoaded[w] = true;
    }

    if (ma_sound_init_from_file(&engine, "sounds/empty_click.mp3",
        MA_SOUND_FLAG_DECODE, NULL, NULL, &emptySound) == MA_SUCCESS)
        emptyLoaded = true;

    for (int v = 0; v < SND_STEP_VOICES; v++) {
        if (ma_sound_init_from_file(&engine, "sounds/footstep.mp3",
            MA_SOUND_FLAG_DECODE, NULL, NULL, &stepSound[v]) == MA_SUCCESS)
            stepLoaded = true;
    }

    std::cout << "[SOUND] Sound System Ready.\n";
}

// ── shutdown ──────────────────────────────────────────────────
void SoundManager::shutdown()
{
    if (!ready) return;
    for (int w = 0; w < 3; w++) {
        for (int v = 0; v < SND_SHOT_VOICES; v++)
            if (shotLoaded[w]) ma_sound_uninit(&shotSounds[w][v]);
        if (reloadLoaded[w]) ma_sound_uninit(&reloadSounds[w]);
    }
    if (emptyLoaded) ma_sound_uninit(&emptySound);
    for (int v = 0; v < SND_STEP_VOICES; v++)
        if (stepLoaded) ma_sound_uninit(&stepSound[v]);
    ma_engine_uninit(&engine);
}

// ── playShot ─────────────────────────────────────────────────
void SoundManager::playShot(int w)
{
    if (!ready || w < 0 || w > 2 || !shotLoaded[w]) return;
    int v = shotVoice[w];
    shotVoice[w] = (v + 1) % SND_SHOT_VOICES;
    _play(shotSounds[w][v]);
}

// ── playReload ────────────────────────────────────────────────
void SoundManager::playReload(int w)
{
    if (!ready || w < 0 || w > 2 || !reloadLoaded[w]) return;
    _play(reloadSounds[w]);
}

// ── playEmpty ─────────────────────────────────────────────────
void SoundManager::playEmpty()
{
    if (!ready || !emptyLoaded) return;
    _play(emptySound);
}

// ── playFootstep ──────────────────────────────────────────────
void SoundManager::playFootstep(float dt, bool moving, bool onGround)
{
    if (!moving || !onGround) { stepTimer = 0.05f; return; }
    stepTimer -= dt;
    if (stepTimer <= 0.f) {
        if (ready && stepLoaded) {
            int v = stepVoice;
            stepVoice = (stepVoice + 1) % SND_STEP_VOICES;
            _play(stepSound[v]);
        }
        stepTimer = player.sprinting ? 0.3f : 0.5f;
    }
}