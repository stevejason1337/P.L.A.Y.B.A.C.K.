#pragma once
// Soundmanager.h — только объявление структуры
// MINIAUDIO_IMPLEMENTATION определяется ТОЛЬКО в Soundmanager.cpp

#include "miniaudio.h"
#include <string>

#define SND_SHOT_VOICES  4
#define SND_STEP_VOICES  2

struct SoundManager {
    ma_engine engine;
    bool  ready = false;
    float stepTimer = 0.f;

    ma_sound shotSounds[3][SND_SHOT_VOICES];
    ma_sound reloadSounds[3];
    ma_sound emptySound;
    ma_sound stepSound[SND_STEP_VOICES];
    int      shotVoice[3] = {};
    int      stepVoice = 0;

    bool shotLoaded[3] = {};
    bool reloadLoaded[3] = {};
    bool emptyLoaded = false;
    bool stepLoaded = false;

    void init();
    void shutdown();
    void playShot(int weapon);
    void playReload(int weapon);
    void playEmpty();
    void playFootstep(float dt, bool moving, bool onGround);

private:
    void _play(ma_sound& s);
};

extern SoundManager soundManager;