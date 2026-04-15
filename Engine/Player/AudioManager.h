#pragma once

// ============================================================
//  Engine/AudioManager.h  —  ДВИЖОК, не трогаешь
//  Обёртка над miniaudio. Игра вызывает play/stop по пути.
//  Не знает что именно играет — решает игровой код.
// ============================================================

#define MINIAUDIO_IMPLEMENTATION
#include "../miniaudio.h"

#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>

struct AudioClip {
    ma_sound sound;
    bool     loaded = false;
};

class AudioManager {
public:
    static AudioManager& get() {
        static AudioManager instance;
        return instance;
    }

    bool init() {
        ma_result result = ma_engine_init(nullptr, &engine);
        if (result != MA_SUCCESS) {
            std::cerr << "[AudioManager] Failed to init engine\n";
            return false;
        }
        initialized = true;
        return true;
    }

    void shutdown() {
        clips.clear();
        if (initialized) ma_engine_uninit(&engine);
    }

    // Загрузить звук (однократно)
    bool load(const std::string& id, const std::string& path) {
        if (clips.count(id)) return true;
        auto clip = std::make_unique<AudioClip>();
        ma_result r = ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, &clip->sound);
        if (r != MA_SUCCESS) {
            std::cerr << "[AudioManager] Failed to load: " << path << "\n";
            return false;
        }
        clip->loaded = true;
        clips[id] = std::move(clip);
        return true;
    }

    void play(const std::string& id, bool loop = false) {
        if (!clips.count(id)) return;
        auto& clip = clips[id];
        ma_sound_set_looping(&clip->sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_seek_to_pcm_frame(&clip->sound, 0);
        ma_sound_start(&clip->sound);
    }

    void stop(const std::string& id) {
        if (!clips.count(id)) return;
        ma_sound_stop(&clips[id]->sound);
    }

    void setVolume(const std::string& id, float vol) {
        if (!clips.count(id)) return;
        ma_sound_set_volume(&clips[id]->sound, vol);
    }

    void setMasterVolume(float vol) {
        ma_engine_set_volume(&engine, vol);
    }

    bool isPlaying(const std::string& id) {
        if (!clips.count(id)) return false;
        return ma_sound_is_playing(&clips[id]->sound) == MA_TRUE;
    }

    void unload(const std::string& id) {
        if (!clips.count(id)) return;
        ma_sound_uninit(&clips[id]->sound);
        clips.erase(id);
    }

private:
    ma_engine engine;
    bool initialized = false;
    std::unordered_map<std::string, std::unique_ptr<AudioClip>> clips;

    AudioManager() = default;
    ~AudioManager() { shutdown(); }
};
