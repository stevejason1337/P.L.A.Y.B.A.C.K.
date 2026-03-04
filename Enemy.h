#pragma once

#include "Settings.h"
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

        IDLE = _find({ "rifle aiming idle","aiming idle","Idle","idle" });
        WALK = _find({ "walking","walk forward","Walking" });
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

inline SharedEnemyModel sharedEnemy;

// ─── Состояния ────────────────────────────────────────────
enum class EnemyState { PATROL, APPROACH, SHOOT, STRAFE, RETREAT, DEAD };

// ─── Один враг — только своё состояние + свои кости ───────
struct Enemy
{
    glm::vec3  pos = glm::vec3(0.f);
    glm::vec3  spawnPos = glm::vec3(0.f);
    float      rotY = 0.f;
    float      hp = ENEMY_HP;
    EnemyState state = EnemyState::PATROL;
    bool       removed = false;

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
        playAnim(sharedEnemy.IDLE, true);
    }

    void playAnim(const std::string& name, bool loop, const std::string& next = "")
    {
        if (name.empty() || name == curAnim) return;
        curAnim = name;
        animTime = 0.f;
        animLoop = loop;
        animDone = false;
        nextAnim = next;
    }

    // Обновляем кости для этого врага
    void updateAnim(float dt)
    {
        auto& m = sharedEnemy.proto;
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

        // Считаем кости через публичный метод AnimatedModel
        m.calcBonesExt(anim, (double)animTime, m.scene->mRootNode,
            glm::mat4(1.f), boneFinal);
    }

    void takeDamage(float dmg)
    {
        if (isDead()) return;
        hp -= dmg;
        hitTimer = 0.25f;
        if (hp <= 0.f) {
            hp = 0.f;
            state = EnemyState::DEAD;
            playAnim(sharedEnemy.DEATH, false);
            return;
        }
        if (state == EnemyState::PATROL) state = EnemyState::APPROACH;
        std::cout << "[ENEMY] Hit! HP:" << hp << "\n";
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        if (removed) return;

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
        if (state != EnemyState::PATROL) {
            float ty = glm::degrees(atan2f(dir.x, dir.z));
            float d = ty - rotY;
            while (d > 180.f) d -= 360.f;
            while (d < -180.f) d += 360.f;
            float smooth = dt * 7.f; if (smooth > 1.f) smooth = 1.f; rotY += d * smooth;
        }

        // ── Логика ──
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
        auto& s = sharedEnemy;
        if (isDead()) return;

        std::string want;
        if (hitTimer > 0.f)              want = s.HIT;
        else if (reloading)                   want = s.RELOAD;
        else if (state == EnemyState::SHOOT)  want = s.SHOOT;
        else if (state == EnemyState::STRAFE) want = strafeDir > 0 ? s.STRAFE_R : s.STRAFE_L;
        else if (state == EnemyState::RETREAT)want = s.WALK_BACK;
        else if (state == EnemyState::PATROL ||
            state == EnemyState::APPROACH)want = s.WALK;
        else                                   want = s.IDLE;

        if (want.empty()) want = s.IDLE;

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
        mat = glm::scale(mat, glm::vec3(ENEMY_SCALE));
        return mat;
    }
};

// ══════════════════════════════════════════════════════════
struct EnemyManager
{
    std::vector<Enemy> enemies;
    std::string modelPath = "models/characters/soldier/Ch35_nonPBR.fbx";
    std::string texDir = "models/characters/soldier";

    void load()
    {
        if (!sharedEnemy.load(modelPath, texDir))
            std::cerr << "[ENEMY] Failed to load model: " << modelPath << "\n";
    }

    void spawn(glm::vec3 p)
    {
        if (!sharedEnemy.loaded) {
            std::cerr << "[ENEMY] Model not loaded! Call load() first.\n";
            return;
        }
        float gy = getGroundY(p, 200.f);
        if (gy != std::numeric_limits<float>::lowest()) p.y = gy;

        Enemy e;
        e.pos = p;
        e.spawnPos = p;
        e.rotY = (float)(rand() % 360);
        float a = (float)(rand() % 360) * 3.14159f / 180.f;
        e.patrolDir = glm::vec3(cosf(a), 0.f, sinf(a));
        e.init();
        enemies.push_back(std::move(e));
        std::cout << "[ENEMY] Spawned at (" << p.x << "," << p.y << "," << p.z << ")\n";
    }

    void spawnGroup(glm::vec3 center, int count, float radius = 5.f)
    {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831f;
            spawn(center + glm::vec3(cosf(a) * radius, 0.f, sinf(a) * radius));
        }
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        for (auto& e : enemies)
            e.update(dt, playerPos, playerHP);
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
        if (!sharedEnemy.loaded || enemies.empty()) return;
        glUseProgram(shader);

        static unsigned int cachedShader = 0;
        static int lModel = -1, lView = -1, lProj = -1, lSkinned = -1, lBones = -1,
            lHasTex = -1, lTex = -1, lColor = -1;
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
        }

        glUniformMatrix4fv(lView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(lProj, 1, GL_FALSE, glm::value_ptr(proj));

        // Меши общие для всех — биндим один раз снаружи цикла
        auto& meshes = sharedEnemy.proto.meshes;

        for (auto& e : enemies) {
            glm::mat4 model = e.getMatrix();
            glUniformMatrix4fv(lModel, 1, GL_FALSE, glm::value_ptr(model));

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

            unsigned int lastTex = 0;
            for (auto& m : meshes) {
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