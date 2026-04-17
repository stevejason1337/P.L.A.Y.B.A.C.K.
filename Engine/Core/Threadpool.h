#pragma once
// ═══════════════════════════════════════════════════════════════
//  ThreadPool.h  —  многопоточность + оптимизация движка
//  Использование:
//    1. #include "ThreadPool.h"  в Enemy.h и main.cpp
//    2. enemyManager.update() автоматически распараллеливается
// ═══════════════════════════════════════════════════════════════

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <queue>
#include <atomic>
#include <future>
#include <algorithm>
#include <chrono>

// ─────────────────────────────────────────────────────────────
// 1. THREAD POOL — пул потоков для параллельных задач
// ─────────────────────────────────────────────────────────────
struct ThreadPool
{
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
    std::mutex                        mtx;
    std::condition_variable           cv;
    std::atomic<bool>                 stop{ false };
    std::atomic<int>                  activeTasks{ 0 };

    explicit ThreadPool(int numThreads = -1)
    {
        if (numThreads < 1)
            // Оставляем 1 ядро для главного потока (рендер)
            numThreads = std::max(1, (int)std::thread::hardware_concurrency() - 1);

        for (int i = 0; i < numThreads; i++)
            workers.emplace_back([this] { _worker(); });

        printf("[THREAD] Pool started: %d worker threads\n", numThreads);
    }

    ~ThreadPool()
    {
        stop = true;
        cv.notify_all();
        for (auto& w : workers) if (w.joinable()) w.join();
    }

    // Добавить задачу в очередь — возвращает future
    template<typename F>
    auto submit(F&& f) -> std::future<decltype(f())>
    {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.push([task] { (*task)(); });
        }
        activeTasks++;
        cv.notify_one();
        return fut;
    }

    // Дождаться завершения всех задач
    void wait()
    {
        while (activeTasks > 0)
            std::this_thread::yield();
    }

    // Параллельный for — делит работу между потоками
    template<typename F>
    void parallel_for(int begin, int end, F&& func)
    {
        if (end <= begin) return;
        int count = end - begin;
        int nw = (int)workers.size();
        int chunk = std::max(1, count / nw);

        std::vector<std::future<void>> futs;
        futs.reserve(nw);

        for (int i = begin; i < end; i += chunk) {
            int i0 = i, i1 = std::min(i + chunk, end);
            futs.push_back(submit([&func, i0, i1] {
                for (int j = i0; j < i1; j++) func(j);
                }));
        }
        for (auto& f : futs) f.get();
    }

private:
    void _worker()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [this] { return stop || !tasks.empty(); });
                if (stop && tasks.empty()) return;
                task = std::move(tasks.front());
                tasks.pop();
            }
            task();
            activeTasks--;
        }
    }
};

// Глобальный пул — создаётся один раз
inline ThreadPool gThreadPool;

// ─────────────────────────────────────────────────────────────
// 2. OBJECT POOL — переиспользование объектов без new/delete
//    Убирает аллокации при спавне/деспавне врагов
// ─────────────────────────────────────────────────────────────
template<typename T, int CAPACITY = 256>
struct ObjectPool
{
    struct Slot { T obj; bool used = false; };
    std::array<Slot, CAPACITY> slots;
    int nextFree = 0;

    T* alloc()
    {
        // Ищем свободный слот начиная с nextFree
        for (int i = 0; i < CAPACITY; i++) {
            int idx = (nextFree + i) % CAPACITY;
            if (!slots[idx].used) {
                slots[idx].used = true;
                nextFree = (idx + 1) % CAPACITY;
                return &slots[idx].obj;
            }
        }
        return nullptr; // пул заполнен
    }

    void free(T* ptr)
    {
        for (auto& s : slots)
            if (&s.obj == ptr) { s.used = false; return; }
    }

    int usedCount() const
    {
        int n = 0;
        for (auto& s : slots) if (s.used) n++;
        return n;
    }
};

// ─────────────────────────────────────────────────────────────
// 3. FRAME LIMITER — стабильный dt без скачков
// ─────────────────────────────────────────────────────────────
struct FrameLimiter
{
    using Clock = std::chrono::high_resolution_clock;
    using Time = std::chrono::time_point<Clock>;

    Time     last = Clock::now();
    double   accum = 0.0;
    double   smoothDt = 0.016;
    int      targetFPS = 0; // 0 = без лимита

    // Вызывай каждый кадр — возвращает сглаженный dt
    float tick()
    {
        auto now = Clock::now();
        double raw = std::chrono::duration<double>(now - last).count();
        last = now;

        // Clamp — если лаг больше 100мс игнорируем (пауза отладчика)
        if (raw > 0.1) raw = 0.1;

        // Экспоненциальное сглаживание dt
        smoothDt = smoothDt * 0.85 + raw * 0.15;

        // Лимит FPS если нужен
        if (targetFPS > 0) {
            double frameTime = 1.0 / targetFPS;
            if (raw < frameTime) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(frameTime - raw));
            }
        }

        return (float)smoothDt;
    }
};

inline FrameLimiter gFrameLimiter;

// ─────────────────────────────────────────────────────────────
// 4. SPATIAL GRID — быстрый поиск врагов рядом с игроком
//    O(1) вместо O(N) для поиска ближайших
// ─────────────────────────────────────────────────────────────
struct SpatialGrid
{
    float cellSize = 10.f;
    std::unordered_map<uint64_t, std::vector<int>> cells;

    void clear() { cells.clear(); }

    void insert(int idx, float x, float z)
    {
        cells[_key(x, z)].push_back(idx);
    }

    // Возвращает индексы врагов в радиусе radius от (x,z)
    std::vector<int> query(float x, float z, float radius) const
    {
        std::vector<int> result;
        int r = (int)ceilf(radius / cellSize);
        int cx = (int)floorf(x / cellSize);
        int cz = (int)floorf(z / cellSize);
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++) {
                auto it = cells.find(_key2(cx + dx, cz + dz));
                if (it != cells.end())
                    for (int i : it->second) result.push_back(i);
            }
        return result;
    }

private:
    uint64_t _key(float x, float z) const
    {
        return _key2((int)floorf(x / cellSize), (int)floorf(z / cellSize));
    }
    uint64_t _key2(int x, int z) const
    {
        return ((uint64_t)(uint32_t)x << 32) | (uint32_t)z;
    }
};

inline SpatialGrid gSpatialGrid;

// ─────────────────────────────────────────────────────────────
// 5. LOD SYSTEM — упрощаем обновление дальних врагов
//    Дальние враги обновляются реже — экономим CPU
// ─────────────────────────────────────────────────────────────
struct LODSystem
{
    // Возвращает множитель частоты обновления для врага на dist
    // 1.0 = каждый кадр, 0.5 = каждый 2й, 0.25 = каждый 4й
    static float updateRate(float dist)
    {
        if (dist < 15.f)  return 1.00f; // близко — каждый кадр
        if (dist < 30.f)  return 0.50f; // средняя — каждые 2 кадра
        if (dist < 60.f)  return 0.25f; // далеко — каждые 4 кадра
        return 0.10f;                   // очень далеко — каждые 10 кадров
    }

    // Нужно ли обновлять врага в этом кадре?
    static bool shouldUpdate(int enemyIdx, float dist, int frameNum)
    {
        float rate = updateRate(dist);
        int period = (int)(1.f / rate);
        // Размазываем обновления — не все сразу в один кадр
        return (frameNum + enemyIdx * 7) % period == 0;
    }

    // Масштаб анимации по дальности — дальние упрощены
    static int boneCount(float dist, int maxBones)
    {
        if (dist < 20.f) return maxBones;       // полный скелет
        if (dist < 40.f) return maxBones * 3 / 4; // 75% костей
        if (dist < 70.f) return maxBones / 2;   // 50% костей
        return maxBones / 4;                    // 25% костей
    }
};

// ─────────────────────────────────────────────────────────────
// 6. PERF COUNTER — счётчик производительности
// ─────────────────────────────────────────────────────────────
struct PerfCounter
{
    struct Scope {
        const char* name;
        std::chrono::high_resolution_clock::time_point start;
        double* target;
        Scope(const char* n, double* t)
            : name(n), target(t),
            start(std::chrono::high_resolution_clock::now()) {
        }
        ~Scope() {
            auto end = std::chrono::high_resolution_clock::now();
            *target = std::chrono::duration<double, std::milli>(end - start).count();
        }
    };

    double timeUpdate = 0.0; // мс на update врагов
    double timeDraw = 0.0; // мс на draw врагов
    double timeAnim = 0.0; // мс на анимации
    double timeRender = 0.0; // мс на рендер сцены
    int    enemiesDrawn = 0;

    Scope measureUpdate() { return { "update", &timeUpdate }; }
    Scope measureDraw() { return { "draw",   &timeDraw }; }
    Scope measureAnim() { return { "anim",   &timeAnim }; }

    void print() const {
        printf("[PERF] update=%.2fms draw=%.2fms anim=%.2fms render=%.2fms enemies=%d\n",
            timeUpdate, timeDraw, timeAnim, timeRender, enemiesDrawn);
    }
};

inline PerfCounter gPerf;