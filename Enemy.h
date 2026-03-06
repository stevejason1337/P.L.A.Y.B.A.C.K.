#pragma once

#include "Settings.h"
#include "ThreadPool.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <algorithm>
#include <iostream>
#include <string>
#include <memory>
#include <cmath>
#include <map>
#include "ModelLoader.h"
#include "AnimatedModel.h"
#include "AABB.h"

// ─── Настройки ────────────────────────────────────────────
const float ENEMY_SCALE = 0.01f;
const float ENEMY_HP = 100.f;
const float ENEMY_DETECT = 30.f;
const float ENEMY_SHOOT_D = 16.f;
const float ENEMY_CLOSE_D = 5.f;
const float ENEMY_WALK_SPD = 1.6f;
const float ENEMY_STRAFE_SPD = 1.2f;
const float ENEMY_SHOOT_DMG = 4.f;

// ─── Общие данные модели (загружается ОДИН РАЗ) ───────────
struct SharedEnemyModel
{
    AnimatedModel proto;   // единственный экземпляр с мешами и сценой
    bool loaded = false;

    // Имена анимаций — заполняются автоматически
    std::string IDLE, WALK, WALK_BACK, STRAFE_L, STRAFE_R,
        SHOOT, RELOAD, HIT, DEATH;

    bool load(const std::string& path, const std::string& texDir)
    {
        proto.texLoader = loadTexture;
        if (!proto.load(path, texDir)) return false;
        loaded = true;
        _autoDetect();
        return true;
    }

    bool hasAnim(const std::string& s) const {
        return !s.empty() && proto.hasAnim(s);
    }

private:
    void _autoDetect()
    {
        std::cout << "[ENEMY] === Animations ===\n";
        for (auto& kv : proto.animIndex)
            std::cout << "[ENEMY]   '" << kv.first << "'\n";

        IDLE = _find({ "rifle aiming idle","aiming idle","Idle","idle","mixamo.com" });
        WALK = _find({ "walking","walk forward","Walking","mixamo.com" });
        WALK_BACK = _find({ "walking backwards","Walk Back","walk back" });
        STRAFE_L = _find({ "strafe left","Strafe Left" });
        STRAFE_R = _find({ "strafe right","Strafe Right" });
        SHOOT = _find({ "firing rifle","Firing Rifle","shoot","fire","Shoot" });
        RELOAD = _find({ "reloading","Reloading","reload" });
        HIT = _find({ "hit reaction","Hit Reaction","hit","getting hit" });
        DEATH = _find({ "dying","Dying","death","Death","die","falling back death" });

        if (IDLE.empty() && !proto.animIndex.empty())
            IDLE = proto.animIndex.begin()->first;
        if (WALK.empty())      WALK = IDLE;
        if (WALK_BACK.empty()) WALK_BACK = WALK;
        if (STRAFE_L.empty())  STRAFE_L = WALK;
        if (STRAFE_R.empty())  STRAFE_R = WALK;
        if (SHOOT.empty())     SHOOT = IDLE;
        if (DEATH.empty())     DEATH = IDLE;
        if (RELOAD.empty())    RELOAD = IDLE;
        if (HIT.empty())       HIT = IDLE;

        std::cout << "[ENEMY] IDLE='" << IDLE << "'\n";
        std::cout << "[ENEMY] WALK='" << WALK << "'\n";
        std::cout << "[ENEMY] SHOOT='" << SHOOT << "'\n";
        std::cout << "[ENEMY] DEATH='" << DEATH << "'\n";
    }

    std::string _find(std::initializer_list<const char*> keys) const
    {
        for (auto k : keys)
            if (proto.hasAnim(k)) return k;
        // частичное совпадение
        for (auto k : keys) {
            std::string lo(k);
            for (auto& c : lo) c = (char)tolower((unsigned char)c);
            for (auto& kv : proto.animIndex) {
                std::string klo = kv.first;
                for (auto& c : klo) c = (char)tolower((unsigned char)c);
                if (klo.find(lo) != std::string::npos) return kv.first;
            }
        }
        return "";
    }
};

inline SharedEnemyModel sharedEnemy;  // солдат
inline SharedEnemyModel sharedZombie; // зомби

// ─── Тип врага ────────────────────────────────────────────
enum class EnemyType { SOLDIER, ZOMBIE };

// ─── Состояния ────────────────────────────────────────────
// ZOMBIE использует только PATROL / APPROACH / MELEE / DEAD
enum class EnemyState { PATROL, APPROACH, SHOOT, STRAFE, RETREAT, MELEE, DEAD };

// ─── Один враг — только своё состояние + свои кости ───────
struct Enemy
{
    EnemyType  type = EnemyType::SOLDIER;
    float      scale = ENEMY_SCALE;
    glm::vec3  pos = glm::vec3(0.f);
    glm::vec3  spawnPos = glm::vec3(0.f);
    float      rotY = 0.f;
    float      hp = ENEMY_HP;
    EnemyState state = EnemyState::PATROL;
    bool       removed = false;
    float      meleeTimer = 0.f;   // кулдаун удара зомби
    float      meleeRange = 2.2f;  // дальность удара
    AnimCache  myCache;               // персональный кеш каналов (не общий!)
    std::string myCachedAnim;

    // Кости — единственное что уникально для каждого врага
    std::vector<glm::mat4> boneFinal;

    // Анимационное состояние
    std::string curAnim;
    float animTime = 0.f;
    float animSpeed = 1.f;
    bool  animLoop = true;
    bool  animDone = false;
    std::string nextAnim;

    // Таймеры
    float deadTimer = 0.f;
    float deadAngle = 0.f;
    float stateTimer = 0.f;
    float shootTimer = 0.f;
    float reloadTimer = 0.f;
    float hitTimer = 0.f;
    float patrolTimer = 0.f;
    float strafeDir = 1.f;
    int   ammo = 30;
    bool  reloading = false;
    glm::vec3 patrolDir = glm::vec3(1, 0, 0);

    bool isDead() const { return state == EnemyState::DEAD; }

    void init()
    {
        boneFinal.assign(AnimatedModel::MAX_BONES, glm::mat4(1.f));
        auto& sm = sharedModel();
        playAnim(sm.WALK.empty() ? sm.IDLE : sm.WALK, true);
    }

    // Возвращает нужную SharedModel по типу
    SharedEnemyModel& sharedModel() {
        return type == EnemyType::ZOMBIE ? sharedZombie : sharedEnemy;
    }
    const SharedEnemyModel& sharedModel() const {
        return type == EnemyType::ZOMBIE ? sharedZombie : sharedEnemy;
    }

    void playAnim(const std::string& name, bool loop, const std::string& next = "")
    {
        if (name.empty() || name == curAnim) return;
        curAnim = name;
        animTime = 0.f;
        animLoop = loop;
        animDone = false;
        nextAnim = next;
        // Перестраиваем персональный кеш каналов
        auto& m = sharedModel().proto;
        if (m.scene) {
            auto it = m.animIndex.find(name);
            if (it != m.animIndex.end()) {
                myCache.build(m.scene->mAnimations[it->second]);
                myCachedAnim = name;
            }
        }
    }

    // Обновляем кости для этого врага
    void updateAnim(float dt)
    {
        auto& m = sharedModel().proto;
        if (curAnim.empty() || !m.scene) return;

        auto it = m.animIndex.find(curAnim);
        if (it == m.animIndex.end()) return;

        const aiAnimation* anim = m.scene->mAnimations[it->second];
        double tps = anim->mTicksPerSecond > 0 ? anim->mTicksPerSecond : 25.0;
        double dur = anim->mDuration;

        animTime += dt * animSpeed * (float)tps;
        if (animTime >= (float)dur) {
            if (animLoop) {
                animTime = fmodf(animTime, (float)dur);
            }
            else {
                animTime = (float)dur - 0.001f;
                animDone = true;
                if (!nextAnim.empty()) {
                    playAnim(nextAnim, true);
                    return;
                }
            }
        }

        // Если кеш не построен — строим
        if (myCachedAnim != curAnim) {
            myCache.build(anim);
            myCachedAnim = curAnim;
        }
        // Считаем кости используя персональный кеш врага
        _calcBonesWithCache(m, anim, (double)animTime,
            m.scene->mRootNode, glm::mat4(1.f), boneFinal);
    }

    // Рекурсивный расчёт костей с персональным кешем врага
    void _calcBonesWithCache(const AnimatedModel& m, const aiAnimation* anim,
        double t, aiNode* node, const glm::mat4& parent,
        std::vector<glm::mat4>& out)
    {
        const std::string name = node->mName.C_Str();
        glm::mat4 nodeT = ai2glm(node->mTransformation);

        // O(1) поиск через персональный кеш
        const aiNodeAnim* ch = myCache.getCh(anim, name);
        if (ch) {
            // Позиция
            glm::vec3 pos;
            if (ch->mNumPositionKeys == 1) {
                pos = ai2vec(ch->mPositionKeys[0].mValue);
            }
            else {
                unsigned int i = AnimatedModel::_findPosKeyStatic(ch, t);
                unsigned int i1 = std::min(i + 1, ch->mNumPositionKeys - 1);
                float f = (float)((t - ch->mPositionKeys[i].mTime) /
                    (ch->mPositionKeys[i1].mTime - ch->mPositionKeys[i].mTime + 1e-9));
                f = glm::clamp(f, 0.f, 1.f);
                pos = glm::mix(ai2vec(ch->mPositionKeys[i].mValue),
                    ai2vec(ch->mPositionKeys[i1].mValue), f);
            }
            // Вращение
            glm::quat rot;
            if (ch->mNumRotationKeys == 1) {
                rot = ai2quat(ch->mRotationKeys[0].mValue);
            }
            else {
                unsigned int i = AnimatedModel::_findRotKeyStatic(ch, t);
                unsigned int i1 = std::min(i + 1, ch->mNumRotationKeys - 1);
                float f = (float)((t - ch->mRotationKeys[i].mTime) /
                    (ch->mRotationKeys[i1].mTime - ch->mRotationKeys[i].mTime + 1e-9));
                f = glm::clamp(f, 0.f, 1.f);
                rot = glm::normalize(glm::slerp(ai2quat(ch->mRotationKeys[i].mValue),
                    ai2quat(ch->mRotationKeys[i1].mValue), f));
            }
            // Масштаб
            glm::vec3 scl(1.f);
            if (ch->mNumScalingKeys == 1) {
                scl = ai2vec(ch->mScalingKeys[0].mValue);
            }
            else {
                unsigned int i = AnimatedModel::_findSclKeyStatic(ch, t);
                unsigned int i1 = std::min(i + 1, ch->mNumScalingKeys - 1);
                float f = (float)((t - ch->mScalingKeys[i].mTime) /
                    (ch->mScalingKeys[i1].mTime - ch->mScalingKeys[i].mTime + 1e-9));
                f = glm::clamp(f, 0.f, 1.f);
                scl = glm::mix(ai2vec(ch->mScalingKeys[i].mValue),
                    ai2vec(ch->mScalingKeys[i1].mValue), f);
            }
            nodeT = glm::translate(glm::mat4(1.f), pos)
                * glm::mat4_cast(rot)
                * glm::scale(glm::mat4(1.f), scl);
        }

        glm::mat4 global = parent * nodeT;
        auto it = m.boneMap.find(name);
        if (it != m.boneMap.end() && it->second.id < AnimatedModel::MAX_BONES)
            out[it->second.id] = m.globalInvT * global * it->second.offset;

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            _calcBonesWithCache(m, anim, t, node->mChildren[i], global, out);
    }

    void takeDamage(float dmg)
    {
        if (isDead()) return;
        hp -= dmg;
        hitTimer = 0.25f;
        if (hp <= 0.f) {
            hp = 0.f;
            state = EnemyState::DEAD;
            playAnim(sharedModel().DEATH, false);
            return;
        }
        if (state == EnemyState::PATROL) state = EnemyState::APPROACH;
        std::cout << "[ENEMY] Hit! HP:" << hp << "\n";
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        if (removed) return;
        // Не обновляем если модель не загружена
        if (!sharedModel().loaded) return;

        if (isDead()) {
            deadTimer += dt;
            deadAngle += dt * 80.f; if (deadAngle > 90.f) deadAngle = 90.f;
            if (deadTimer > 4.f) removed = true;
            updateAnim(dt);
            return;
        }

        if (hitTimer > 0.f) hitTimer -= dt;
        stateTimer += dt;
        if (shootTimer > 0.f) shootTimer -= dt;

        glm::vec3 diff = playerPos - pos;
        diff.y = 0.f;
        float dist = glm::length(diff);
        glm::vec3 dir = dist > 0.01f ? diff / dist : glm::vec3(0, 0, 1);

        // Плавный поворот
        if (state != EnemyState::PATROL && dist < ENEMY_DETECT) {
            float ty = glm::degrees(atan2f(dir.x, dir.z));
            float d = ty - rotY;
            while (d > 180.f) d -= 360.f;
            while (d < -180.f) d += 360.f;
            // Зомби поворачивается медленнее солдата — реалистичнее
            float rotSpd = (type == EnemyType::ZOMBIE) ? 3.5f : 7.f;
            float smooth = dt * rotSpd; if (smooth > 1.f) smooth = 1.f;
            rotY += d * smooth;
        }

        // ── Логика ──
        if (type == EnemyType::ZOMBIE) {
            // Зомби — только идёт и бьёт
            if (meleeTimer > 0.f) meleeTimer -= dt;
            if (dist > ENEMY_DETECT) {
                _patrol(dt);
                animSpeed = 0.5f;
            }
            else {
                state = EnemyState::APPROACH;
                float spd = ENEMY_WALK_SPD * 1.2f; // нормальная скорость зомби
                animSpeed = 1.0f;                   // анимация на нормальной скорости
                _moveTo(pos + dir * spd * dt);
                if (dist <= meleeRange && meleeTimer <= 0.f) {
                    state = EnemyState::MELEE;
                    playerHP -= 12.f;
                    if (playerHP < 0.f) playerHP = 0.f;
                    meleeTimer = 1.5f;
                }
            }
        }
        else {
            // Солдат — стреляет
            if (dist > ENEMY_DETECT) {
                _patrol(dt);
            }
            else if (dist > ENEMY_SHOOT_D) {
                state = EnemyState::APPROACH;
                _moveTo(pos + dir * ENEMY_WALK_SPD * dt);
                if (stateTimer > 3.f) { _switchCombat(); stateTimer = 0.f; }
            }
            else if (dist < ENEMY_CLOSE_D) {
                state = EnemyState::RETREAT;
                _moveTo(pos - dir * ENEMY_WALK_SPD * dt);
                _tryShoot(playerHP);
            }
            else {
                stateTimer -= dt;
                if (stateTimer <= 0.f) _switchCombat();
                if (state == EnemyState::STRAFE) {
                    glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
                    _moveTo(pos + right * strafeDir * ENEMY_STRAFE_SPD * dt);
                }
                _tryShoot(playerHP);
            }
        }

        if (reloading) {
            reloadTimer -= dt;
            if (reloadTimer <= 0.f) { reloading = false; ammo = 30; }
        }

        // ── Анимация по состоянию ──
        _updateAnimState();
        updateAnim(dt);
    }

    void _updateAnimState()
    {
        auto& s = sharedModel();
        if (isDead()) return;

        std::string want;
        if (type == EnemyType::ZOMBIE) {
            // Зомби — только ходьба и удар
            if (hitTimer > 0.f)                want = s.HIT;
            else if (state == EnemyState::MELEE)    want = s.SHOOT; // удар рукой
            else if (state == EnemyState::APPROACH ||
                state == EnemyState::PATROL)   want = s.WALK;
            else                                     want = s.IDLE;
        }
        else {
            if (hitTimer > 0.f)              want = s.HIT;
            else if (reloading)                   want = s.RELOAD;
            else if (state == EnemyState::SHOOT)  want = s.SHOOT;
            else if (state == EnemyState::STRAFE) want = strafeDir > 0 ? s.STRAFE_R : s.STRAFE_L;
            else if (state == EnemyState::RETREAT)want = s.WALK_BACK;
            else if (state == EnemyState::PATROL ||
                state == EnemyState::APPROACH)want = s.WALK;
            else                                   want = s.IDLE;
        }

        if (want.empty()) want = s.IDLE;
        if (want.empty()) return;

        bool once = (want == s.HIT || want == s.RELOAD);
        if (want != curAnim)
            playAnim(want, !once, once ? s.IDLE : "");
    }

    void _patrol(float dt)
    {
        state = EnemyState::PATROL;
        patrolTimer -= dt;
        if (patrolTimer <= 0.f) {
            float a = (float)(rand() % 360) * 3.14159f / 180.f;
            patrolDir = glm::vec3(cosf(a), 0.f, sinf(a));
            patrolTimer = 2.f + (float)(rand() % 20) / 10.f;
            rotY = glm::degrees(atan2f(patrolDir.x, patrolDir.z));
        }
        glm::vec3 np = pos + patrolDir * (ENEMY_WALK_SPD * 0.35f) * dt;
        if (glm::length(np - spawnPos) < 10.f) _moveTo(np);
        else patrolTimer = 0.f;
    }

    void _switchCombat()
    {
        if (state == EnemyState::SHOOT || state == EnemyState::APPROACH ||
            state == EnemyState::RETREAT) {
            state = EnemyState::STRAFE;
            stateTimer = 1.f + (float)(rand() % 12) / 10.f;
            strafeDir = (rand() % 2 == 0) ? 1.f : -1.f;
        }
        else {
            state = EnemyState::SHOOT;
            stateTimer = 1.5f + (float)(rand() % 15) / 10.f;
        }
    }

    void _tryShoot(float& playerHP)
    {
        if (reloading) return;
        if (shootTimer <= 0.f) {
            if (rand() % 100 < 55) {
                playerHP -= ENEMY_SHOOT_DMG;
                if (playerHP < 0.f) playerHP = 0.f;
            }
            ammo--;
            shootTimer = 0.10f + (float)(rand() % 5) / 100.f;
            if (ammo <= 0) { reloading = true; reloadTimer = 2.2f; }
        }
    }

    void _moveTo(glm::vec3 np)
    {
        float gy = getGroundY(np, 100.f);
        np.y = (gy != std::numeric_limits<float>::lowest()) ? gy : pos.y;
        wallCollide(np, 0.4f);
        pos = np;
    }



    glm::mat4 getMatrix() const
    {
        glm::mat4 mat(1.f);
        mat = glm::translate(mat, pos);
        mat = glm::rotate(mat, glm::radians(rotY), glm::vec3(0, 1, 0));
        if (isDead())
            mat = glm::rotate(mat, glm::radians(deadAngle), glm::vec3(1, 0, 0));
        mat = glm::scale(mat, glm::vec3(scale));
        return mat;
    }
};

// ══════════════════════════════════════════════════════════
struct EnemyManager
{
    std::vector<Enemy> enemies;
    int frameNum = 0;
    // Солдат
    std::string modelPath = "models/characters/soldier/Ch35_nonPBR.fbx";
    std::string texDir = "models/characters/soldier";
    // Зомби
    std::string zombiePath = "models/characters/walker/walker.fbx";
    std::string zombieTexDir = "models/characters/walker";

    void load()
    {
        if (!sharedEnemy.load(modelPath, texDir))
            std::cerr << "[ENEMY] Failed to load soldier\n";
        // Зомби загружается только если путь не пустой
        if (!zombiePath.empty()) {
            if (!sharedZombie.load(zombiePath, zombieTexDir))
                std::cerr << "[ENEMY] Failed to load zombie (path: " << zombiePath << ")\n";
        }
    }

    // Спавн солдата
    void spawn(glm::vec3 p)
    {
        _spawnEnemy(p, EnemyType::SOLDIER);
    }

    // Спавн зомби
    void spawnZombie(glm::vec3 p)
    {
        _spawnEnemy(p, EnemyType::ZOMBIE);
    }

    void _spawnEnemy(glm::vec3 p, EnemyType t)
    {
        auto& sm = (t == EnemyType::ZOMBIE) ? sharedZombie : sharedEnemy;
        if (!sm.loaded) {
            std::cerr << "[ENEMY] Model not loaded!\n";
            return;
        }
        float gy = getGroundY(p, 200.f);
        if (gy != std::numeric_limits<float>::lowest()) p.y = gy;

        Enemy e;
        e.type = t;
        e.pos = p;
        e.spawnPos = p;
        e.rotY = (float)(rand() % 360);
        float a = (float)(rand() % 360) * 3.14159f / 180.f;
        e.patrolDir = glm::vec3(cosf(a), 0.f, sinf(a));
        // Зомби медленнее и более живучий
        if (t == EnemyType::ZOMBIE) {
            e.hp = 180.f;
            e.meleeRange = 2.0f;
            // Автоподбор масштаба — пробуем по размеру bounding box
            auto& proto = sharedZombie.proto;
            if (!proto.meshes.empty()) {
                // Если модель в сантиметрах (Mixamo) — 0.01, если в метрах — 1.0
                // parasiteZombie обычно ~170 единиц высотой в сантиметрах
                e.scale = 0.01f;
            }
            else {
                e.scale = 0.01f;
            }
        }
        e.init();
        enemies.push_back(std::move(e));
        std::cout << "[ENEMY] Spawned "
            << (t == EnemyType::ZOMBIE ? "zombie" : "soldier")
            << " at (" << p.x << "," << p.y << "," << p.z << ")\n";
    }

    void spawnGroup(glm::vec3 center, int count, float radius = 5.f)
    {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831f;
            spawn(center + glm::vec3(cosf(a) * radius, 0.f, sinf(a) * radius));
        }
    }

    void spawnZombieGroup(glm::vec3 center, int count, float radius = 5.f)
    {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831f;
            spawnZombie(center + glm::vec3(cosf(a) * radius, 0.f, sinf(a) * radius));
        }
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        frameNum++;

        // Обновляем spatial grid
        gSpatialGrid.clear();
        for (int i = 0; i < (int)enemies.size(); i++)
            gSpatialGrid.insert(i, enemies[i].pos.x, enemies[i].pos.z);

        // Логика AI — в главном потоке (меняет playerHP — нет race condition)
        for (int i = 0; i < (int)enemies.size(); i++) {
            auto& e = enemies[i];
            if (e.removed || !e.sharedModel().loaded) continue;
            float dist = glm::length(e.pos - playerPos);
            // LOD — дальние обновляются реже
            if (!LODSystem::shouldUpdate(i, dist, frameNum)) continue;
            e.update(dt, playerPos, playerHP);
        }

        // (анимации обновляются внутри каждого e.update() выше)

        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(),
                [](const Enemy& e) {return e.removed; }),
            enemies.end());
    }

    int rayHit(const glm::vec3& orig, const glm::vec3& dir, float maxDist = 200.f)
    {
        float best = maxDist; int idx = -1;
        for (int i = 0; i < (int)enemies.size(); i++) {
            if (enemies[i].isDead()) continue;
            glm::vec3 oc = orig - enemies[i].pos;
            float b = glm::dot(oc, dir), c = glm::dot(oc, oc) - 0.5f * 0.5f;
            float d = b * b - c; if (d < 0.f) continue;
            float t = -b - sqrtf(d);
            if (t > 0.f && t < best) { best = t; idx = i; }
        }
        return idx;
    }

    void draw(unsigned int shader, const glm::mat4& view, const glm::mat4& proj)
    {
        if (enemies.empty()) return;
        if (!sharedEnemy.loaded && !sharedZombie.loaded) return;
        glUseProgram(shader);

        static unsigned int cachedShader = 0;
        static int lModel = -1, lView = -1, lProj = -1, lSkinned = -1, lBones = -1,
            lHasTex = -1, lTex = -1, lColor = -1, lNormMat = -1;
        if (cachedShader != shader) {
            cachedShader = shader;
            lModel = glGetUniformLocation(shader, "model");
            lView = glGetUniformLocation(shader, "view");
            lProj = glGetUniformLocation(shader, "projection");
            lSkinned = glGetUniformLocation(shader, "skinned");
            lBones = glGetUniformLocation(shader, "bones");
            lHasTex = glGetUniformLocation(shader, "hasTexture");
            lTex = glGetUniformLocation(shader, "tex");
            lColor = glGetUniformLocation(shader, "baseColor");
            lNormMat = glGetUniformLocation(shader, "normalMatrix");
        }

        glUniformMatrix4fv(lView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(lProj, 1, GL_FALSE, glm::value_ptr(proj));

        // Frustum culling — вытаскиваем 6 плоскостей из viewproj
        glm::mat4 vp = proj * view;
        // Плоскости frustum (left, right, bottom, top, near, far)
        glm::vec4 planes[6];
        planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // left
        planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // right
        planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // bottom
        planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // top
        planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // near
        planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // far
        // Нормализуем
        for (auto& p : planes) p /= glm::length(glm::vec3(p));

        int drawn = 0;
        for (auto& e : enemies) {
            // Sphere cull — радиус врага ~1м
            bool visible = true;
            for (auto& p : planes) {
                if (glm::dot(glm::vec3(p), e.pos) + p.w < -1.2f) {
                    visible = false; break;
                }
            }
            if (!visible) continue;
            drawn++;
            glm::mat4 model = e.getMatrix();
            glUniformMatrix4fv(lModel, 1, GL_FALSE, glm::value_ptr(model));
            if (lNormMat >= 0) {
                glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(model)));
                glUniformMatrix3fv(lNormMat, 1, GL_FALSE, glm::value_ptr(nm));
            }

            // Кости — уникальны для каждого врага
            bool hasBones = !e.boneFinal.empty();
            glUniform1i(lSkinned, (int)hasBones);
            if (hasBones && lBones >= 0) {
                int bc = (int)e.boneFinal.size(); if (bc > 100) bc = 100;
                glUniformMatrix4fv(lBones, bc, GL_FALSE, glm::value_ptr(e.boneFinal[0]));
            }

            // Цвет
            glm::vec3 col = e.isDead()
                ? glm::vec3(0.2f, 0.15f, 0.1f)
                : e.reloading ? glm::vec3(0.6f, 0.6f, 0.2f)
                : e.state == EnemyState::SHOOT ? glm::vec3(0.75f, 0.3f, 0.1f)
                : glm::vec3(0.35f, 0.42f, 0.28f);
            glUniform3fv(lColor, 1, glm::value_ptr(col));

            // Меши берём для конкретного типа врага
            const auto& meshes = e.sharedModel().proto.meshes;
            unsigned int lastTex = 0;
            for (const auto& mesh : meshes) {
                const auto& m = mesh;
                if (m.texID) {
                    if (m.texID != lastTex) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, m.texID);
                        lastTex = m.texID;
                    }
                    glUniform1i(lHasTex, 1); glUniform1i(lTex, 0);
                }
                else {
                    glUniform1i(lHasTex, 0); lastTex = 0;
                }
                glBindVertexArray(m.VAO);
                glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
    }
};

inline EnemyManager enemyManager;