#pragma once
// ============================================================
//  Engine/AudioManager.h  —  ДВИЖОК, не трогаешь
//  Синглтон аудио системы на основе miniaudio.
//  Заменяет старый SoundManager полностью.
//  Игра вызывает: load(), play(), stop(), setVolume()
// ============================================================

// ВАЖНО: MINIAUDIO_IMPLEMENTATION определяется ровно один раз
// в AudioManager.cpp (создай пустой .cpp с этим define)
#include "miniaudio.h"

#include <string>
#include <unordered_map>
#include <iostream>

class AudioManager {
public:
    static AudioManager& get() {
        static AudioManager instance;
        return instance;
    }

    // ── Инициализация (вызывает Engine::init) ─────────────
    bool init() {
        if (ma_engine_init(nullptr, &engine) != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to initialize miniaudio engine\n";
            return false;
        }
        ready = true;
        std::cout << "[Audio] AudioManager ready\n";
        return true;
    }

    // ── Завершение (вызывает Engine::shutdown) ─────────────
    void shutdown() {
        if (!ready) return;
        for (auto& [key, snd] : sounds)
            ma_sound_uninit(&snd);
        sounds.clear();
        ma_engine_uninit(&engine);
        ready = false;
    }

    // ── Загрузить звук по имени ───────────────────────────
    // name  — уникальный ключ (например "pistol_shoot")
    // path  — путь к файлу   (например "sounds/pistol_shoot.wav")
    void load(const std::string& name, const std::string& path) {
        if (!ready) return;
        if (sounds.count(name)) return;     // уже загружен

        ma_sound s;
        ma_result result = ma_sound_init_from_file(
            &engine, path.c_str(),
            MA_SOUND_FLAG_DECODE,
            nullptr, nullptr, &s);

        if (result == MA_SUCCESS) {
            sounds[name] = s;
        } else {
            std::cerr << "[Audio] Failed to load: " << path << "\n";
        }
    }

    // ── Воспроизвести звук ────────────────────────────────
    // loop = true  — зациклить (для музыки/ambient)
    // loop = false — один раз
    void play(const std::string& name, bool loop = false) {
        if (!ready) return;
        auto it = sounds.find(name);
        if (it == sounds.end()) {
            std::cerr << "[Audio] Sound not loaded: " << name << "\n";
            return;
        }
        ma_sound_set_looping(&it->second, loop ? MA_TRUE : MA_FALSE);
        ma_sound_seek_to_pcm_frame(&it->second, 0);
        ma_sound_start(&it->second);
    }

    // ── Остановить звук ───────────────────────────────────
    void stop(const std::string& name) {
        if (!ready) return;
        auto it = sounds.find(name);
        if (it != sounds.end())
            ma_sound_stop(&it->second);
    }

    // ── Громкость (0.0 — 1.0) ─────────────────────────────
    void setVolume(const std::string& name, float vol) {
        if (!ready) return;
        auto it = sounds.find(name);
        if (it != sounds.end())
            ma_sound_set_volume(&it->second, vol);
    }

    // ── Глобальная громкость ──────────────────────────────
    void setMasterVolume(float vol) {
        if (ready) ma_engine_set_volume(&engine, vol);
    }

    // ── Проверка воспроизведения ──────────────────────────
    bool isPlaying(const std::string& name) {
        if (!ready) return false;
        auto it = sounds.find(name);
        if (it == sounds.end()) return false;
        return ma_sound_is_playing(&it->second) == MA_TRUE;
    }

    bool isReady() const { return ready; }

private:
    AudioManager()  = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    ma_engine engine{};
    bool      ready = false;

    std::unordered_map<std::string, ma_sound> sounds;
};
