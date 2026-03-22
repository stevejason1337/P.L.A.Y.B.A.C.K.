#pragma once

#include "Settings.h"
#include "ThreadPool.h"
#include "Physics.h"
#include "BulletIntegration.h"
#include "BloodFX.h"
#include "ImpactPhysics.h"
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
#include <queue>
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
        if (loaded) return true;  // уже загружено — не грузим повторно
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

        // Mixamo / UE5 ThirdPerson / custom names
        IDLE = _find({ "rifle aiming idle","aiming idle","Idle","idle",
                            "ThirdPersonIdle","MF_Idle","Stand_Idle","mixamo.com" });
        WALK = _find({ "walking","walk forward","Walking",
                            "ThirdPersonWalk","MF_Walk_Fwd","Walk_Fwd","mixamo.com" });
        WALK_BACK = _find({ "walking backwards","Walk Back","walk back",
                            "Walk_Bwd","MF_Walk_Bwd" });
        STRAFE_L = _find({ "strafe left","Strafe Left",
                            "Walk_Lt","MF_Walk_Lt" });
        STRAFE_R = _find({ "strafe right","Strafe Right",
                            "Walk_Rt","MF_Walk_Rt" });
        // Для ближнего боя — ищем удар
        SHOOT = _find({ "firing rifle","Firing Rifle","shoot","fire","Shoot",
                            "Attack","attack","MeleeAttack","Melee_Attack",
                            "Attack_01","Punch","punch","Slam","slam",
                            "ThirdPersonAttack","MF_Attack" });
        RELOAD = _find({ "reloading","Reloading","reload",
                            "Attack_02","Swing","swing" });
        HIT = _find({ "hit reaction","Hit Reaction","hit","getting hit",
                            "HitReact","Hit_React_Front","Hit_React","MF_Hit",
                            "ThirdPersonHit" });
        DEATH = _find({ "dying","Dying","death","Death","die",
                            "falling back death","Dead","dead",
                            "Death_Pose","MF_Death","ThirdPersonDeath",
                            "Death_Forward","Death_Backward" });

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

inline SharedEnemyModel gModelSoldier;
inline SharedEnemyModel gModelZombie;
inline SharedEnemyModel gModelZombie2;
inline SharedEnemyModel gModelPigDemon;

// ─── Тип врага ────────────────────────────────────────────
enum class EnemyType { SOLDIER, ZOMBIE, ZOMBIE2, PIG_DEMON };

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
    int        meleeCombo = 0;    // счётчик комбо (pig demon)
    float      meleeRange = 2.2f;  // дальность удара
    AnimCache     myCache;
    std::string   myCachedAnim;
    DeathAnimator deathAnim;          // ragdoll при смерти
    Hitbox        hitbox;             // точные хитбоксы
    glm::vec3     lastShootDir = glm::vec3(0, 0, 1); // направление последнего выстрела
    int           bulletId = -1;  // ID в BulletWorld

    // Кости — единственное что уникально для каждого врага
    std::vector<glm::mat4> boneFinal;
    struct FlatNode { int parentIdx; std::string name; glm::mat4 localT; };
    std::vector<FlatNode>  flatNodes;
    std::vector<glm::mat4> globalMats;

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
        bulletId = bulletWorld.addEnemy(pos, rotY);
    }

    // Возвращает нужную SharedModel по типу
    SharedEnemyModel& sharedModel() {
        if (type == EnemyType::ZOMBIE2)   return gModelZombie2;
        if (type == EnemyType::ZOMBIE)    return gModelZombie;
        if (type == EnemyType::PIG_DEMON) return gModelPigDemon;
        return gModelSoldier;
    }
    const SharedEnemyModel& sharedModel() const {
        if (type == EnemyType::ZOMBIE2)   return gModelZombie2;
        if (type == EnemyType::ZOMBIE)    return gModelZombie;
        if (type == EnemyType::PIG_DEMON) return gModelPigDemon;
        return gModelSoldier;
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
                if (it->second == -1) {
                    // Анимация из доп. FBX
                    auto eit = m.animExtraScene.find(name);
                    if (eit != m.animExtraScene.end() && eit->second < (int)m.extraScenes.size()) {
                        const aiScene* es = m.extraScenes[eit->second];
                        if (es && es->mNumAnimations > 0) {
                            myCache.build(es->mAnimations[0]);
                            myCachedAnim = name;
                        }
                    }
                }
                else if (it->second >= 0 && (unsigned)it->second < m.scene->mNumAnimations) {
                    myCache.build(m.scene->mAnimations[it->second]);
                    myCachedAnim = name;
                }
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

        // Получаем анимацию — из основной или доп. сцены
        const aiAnimation* anim = nullptr;
        if (it->second == -1) {
            auto eit = m.animExtraScene.find(curAnim);
            if (eit == m.animExtraScene.end() || eit->second >= (int)m.extraScenes.size()) return;
            const aiScene* es = m.extraScenes[eit->second];
            if (!es || es->mNumAnimations == 0) return;
            anim = es->mAnimations[0];
        }
        else {
            if (it->second < 0 || (unsigned)it->second >= m.scene->mNumAnimations) return;
            anim = m.scene->mAnimations[it->second];
        }
        if (!anim) return;

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
            flatNodes.clear();
            struct _BFSEntry { aiNode* node; int parent; };
            std::queue<_BFSEntry> _q;
            _q.push({ m.scene->mRootNode, -1 });
            while (!_q.empty()) {
                auto [_nd, _par] = _q.front(); _q.pop();
                int _idx = (int)flatNodes.size();
                flatNodes.push_back({ _par, _nd->mName.C_Str(), ai2glm(_nd->mTransformation) });
                for (unsigned int _ci = 0; _ci < _nd->mNumChildren; _ci++)
                    _q.push({ _nd->mChildren[_ci], _idx });
            }
            globalMats.resize(flatNodes.size());
        }
        // Proven recursive bone calc
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

    void takeDamage(float dmg, const glm::vec3& shootDir = glm::vec3(0, 0, 1))
    {
        if (isDead()) return;
        lastShootDir = shootDir;
        hp -= dmg;
        hitTimer = 0.25f;

        // Точка попадания
        glm::vec3 hitPos = pos + glm::vec3(0.f, 1.2f, 0.f);

        // Физический импульс от пули + брызги крови + hitmarker
        bulletWorld.applyBulletImpulse(bulletId, shootDir, dmg, hitPos);
        glm::vec3 bloodNorm = glm::normalize(-shootDir + glm::vec3(0, 0.2f, 0));
        bloodFX.spawnHit(hitPos, bloodNorm, shootDir, (dmg >= 150.f) ? 35 : 20);
        gHitMarker.trigger((int)dmg, dmg >= 150.f);

        if (hp <= 0.f) {
            hp = 0.f;
            state = EnemyState::DEAD;
            playAnim(sharedModel().DEATH, false);
            deathAnim.trigger(pos, rotY, shootDir, 6.f);
            bulletWorld.activateRagdoll(bulletId, pos, rotY, shootDir, dmg);
            bloodFX.spawnDeath(pos, shootDir);
            return;
        }
        if (state == EnemyState::PATROL) state = EnemyState::APPROACH;
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        if (removed) return;
        // Не обновляем если модель не загружена
        if (!sharedModel().loaded) return;

        if (isDead()) {
            deadTimer += dt;
            deathAnim.update(dt);
            // Падение без ragdoll — простой наклон
            if (!deathAnim.isRagdoll()) {
                deadAngle += dt * 90.f;
                if (deadAngle > 90.f) deadAngle = 90.f;
                updateAnim(dt);
            }
            if (deadTimer > 6.f && !removed) {
                // Сначала убираем из Bullet, потом помечаем removed
                if (bulletId >= 0) {
                    bulletWorld.removeEnemy(bulletId);
                    bulletId = -1;
                }
                removed = true;
            }
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
        if (type == EnemyType::ZOMBIE || type == EnemyType::ZOMBIE2) {
            // Зомби — только идёт и бьёт
            if (meleeTimer > 0.f) meleeTimer -= dt;
            if (dist > ENEMY_DETECT) {
                _patrol(dt);
                animSpeed = 0.5f;
            }
            else {
                state = EnemyState::APPROACH;
                float spd = (type == EnemyType::ZOMBIE2)
                    ? ENEMY_WALK_SPD * 2.0f
                    : ENEMY_WALK_SPD * 1.2f;
                animSpeed = (type == EnemyType::ZOMBIE2) ? 1.4f : 1.0f;
                _moveTo(pos + dir * spd * dt);
                if (dist <= meleeRange && meleeTimer <= 0.f) {
                    state = EnemyState::MELEE;
                    playerHP -= 12.f;
                    if (playerHP < 0.f) playerHP = 0.f;
                    meleeTimer = 1.5f;
                }
            }
        }
        else if (type == EnemyType::PIG_DEMON) {
            // Pig Demon — быстрый ближний боец
            // Фазы: патруль → бег к игроку → яростная атака (2 удара)
            if (meleeTimer > 0.f) meleeTimer -= dt;
            if (dist > ENEMY_DETECT * 1.2f) {
                _patrol(dt);
                animSpeed = 0.6f;
            }
            else if (dist > meleeRange) {
                state = EnemyState::APPROACH;
                // Чем ближе — тем быстрее бежит (charge)
                float chargeSpd = dist < 8.f
                    ? ENEMY_WALK_SPD * 3.5f   // спринт
                    : ENEMY_WALK_SPD * 2.0f;  // бег
                animSpeed = dist < 8.f ? 1.8f : 1.2f;
                _moveTo(pos + dir * chargeSpd * dt);
            }
            else {
                // В зоне удара — атакуем
                state = EnemyState::MELEE;
                if (meleeTimer <= 0.f) {
                    // Два удара подряд с небольшим интервалом
                    float dmg = (meleeCombo == 0) ? 20.f : 15.f;
                    playerHP -= dmg;
                    if (playerHP < 0.f) playerHP = 0.f;
                    meleeCombo = (meleeCombo + 1) % 3; // 3-хитовое комбо
                    meleeTimer = (meleeCombo == 0) ? 1.8f : 0.6f; // пауза после серии
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
            if (hitTimer > 0.f)                     want = s.HIT;
            else if (state == EnemyState::MELEE)    want = s.SHOOT;
            else if (state == EnemyState::APPROACH ||
                state == EnemyState::PATROL)   want = s.WALK;
            else                                    want = s.IDLE;
        }
        else if (type == EnemyType::PIG_DEMON) {
            if (hitTimer > 0.f)
                want = s.HIT;
            else if (state == EnemyState::MELEE) {
                // Чередуем SHOOT (удар1) и RELOAD (удар2) для комбо
                want = (meleeCombo % 2 == 0) ? s.SHOOT : s.RELOAD;
            }
            else if (state == EnemyState::APPROACH) {
                // Бег если близко, ходьба если далеко
                float dist2 = glm::length(glm::vec3(0.f)); // placeholder
                want = s.WALK; // используем WALK (может быть Run из FBX)
            }
            else
                want = s.IDLE;
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
        glm::vec3 renderPos = pos + bulletWorld.getHitOffset(bulletId);
        glm::mat4 mat(1.f);
        mat = glm::translate(mat, renderPos);
        mat = glm::rotate(mat, glm::radians(rotY), glm::vec3(0, 1, 0));
        if (isDead())
            mat = glm::rotate(mat, glm::radians(deadAngle), glm::vec3(1, 0, 0));
        mat = glm::scale(mat, glm::vec3(scale));
        return mat;
    }
    void _recalcBonesParallel()
    {
        auto& m = sharedModel().proto;
        if (curAnim.empty() || !m.scene || myCachedAnim != curAnim) return;
        auto it = m.animIndex.find(curAnim);
        if (it == m.animIndex.end()) return;
        const aiAnimation* anim = m.scene->mAnimations[it->second];
        _calcBonesWithCache(m, anim, (double)animTime,
            m.scene->mRootNode, glm::mat4(1.f), boneFinal);
    }

};

// ══════════════════════════════════════════════════════════
struct EnemyManager
{
    std::vector<Enemy> enemies;
    int frameNum = 0;
    bool debugNoCull = false; // true = рисуем всех без frustum culling
    // Солдат
    std::string modelPath = "models/characters/soldier/Ch35_nonPBR.fbx";
    std::string texDir = "models/characters/soldier";
    // Зомби 1
    std::string zombiePath = "models/characters/walker/walker.fbx";
    std::string zombieTexDir = "models/characters/walker/textures";
    // Зомби 2
    std::string zombie2Path = "models/characters/walker2/walker2.fbx.fbx";
    std::string zombie2TexDir = "models/characters/walker2/textures";
    // Pig Demon
    std::string pigDemonPath = "models/characters/pig_demon/source/pig_demon.fbx";
    std::string pigDemonTexDir = "models/characters/pig_demon/source/textures";
    // Внешний FBX с анимациями (UE5 ThirdPerson / Mixamo пак)
    std::string pigDemonAnimFbx = "models/animations/source/3rdpersonanim.fbx";

    void load()
    {
        if (!gModelSoldier.load(modelPath, texDir))
            std::cerr << "[ENEMY] Failed to load soldier\n";
        // Зомби загружается только если путь не пустой
        if (!zombiePath.empty()) {
            if (!gModelZombie.load(zombiePath, zombieTexDir))
                std::cerr << "[ENEMY] Failed to load zombie\n";
            else {
                // Walker FBX may have no embedded tex — assign manually
                const std::string texNames[] = {
                    "@Diffuse_0.png","@Diffuse_1.png","@Diffuse_2.png","@Diffuse_3.png","@Diffuse.png"
                };
                auto& meshes = gModelZombie.proto.meshes;
                for (int mi = 0; mi < (int)meshes.size(); mi++) {
                    if (meshes[mi].texID == 0 && meshes[mi].dxSRV == nullptr) {
                        std::string tn = texNames[mi % 5];
                        unsigned int tid = loadTexture(zombieTexDir + "/" + tn);
                        if (tid) meshes[mi].texID = tid;
                        meshes[mi].texPath = zombieTexDir + "/" + tn;
                    }
                }
            }
        }
        if (!zombie2Path.empty()) {
            if (!gModelZombie2.load(zombie2Path, zombie2TexDir))
                std::cerr << "[ENEMY] Failed to load zombie2\n";
        }

        // ── Pig Demon ─────────────────────────────────────────
        if (!pigDemonPath.empty()) {
            if (!gModelPigDemon.load(pigDemonPath, pigDemonTexDir)) {
                std::cerr << "[ENEMY] Failed to load pig_demon\n";
            }
            else {
                // Грузим анимации из отдельного FBX
                // AnimatedModel::loadExtraAnimations() подключает анимации
                // из другого файла к существующей модели
                gModelPigDemon.loaded = true; // форсируем, loadExtraAnim не меняет флаг

                // Назначаем текстуру вручную — в FBX зашит абсолютный путь
                // поэтому автопоиск не работает, пробуем все варианты имён
                {
                    auto& meshes = gModelPigDemon.proto.meshes;
                    const std::string& td = pigDemonTexDir;
                    for (auto& m : meshes) {
                        if (m.texID == 0) {
                            // Пробуем все возможные варианты имени файла
                            const char* names[] = {
                                "PigDemon DIFF.jpg", "PigDemon_DIFF.jpg",
                                "PigDemonDIFF.jpg",  "PigDemon DIFF.png",
                                "PigDemon_DIFF.png", "pigdemon_diff.jpg",
                                "Diffuse.jpg",       "diffuse.jpg",
                                "BaseColor.jpg",     "albedo.jpg",
                                nullptr
                            };
                            for (int ni = 0; names[ni]; ni++) {
                                unsigned int tid = loadTexture(td + "/" + names[ni]);
                                if (tid) { m.texID = tid; printf("[ENEMY] Pig demon tex: %s\n", names[ni]); break; }
                            }
                        }
                    }
                }

                if (!pigDemonAnimFbx.empty()) {
                    printf("[ENEMY] Loading pig_demon animations from: %s\n",
                        pigDemonAnimFbx.c_str());
                    // loadExtraAnim грузит ВСЕ анимации из FBX и добавляет
                    // их в animIndex модели — _autoDetect уже ищет по ним
                    gModelPigDemon.proto.loadExtraAnim(pigDemonAnimFbx);

                    // Выводим все найденные анимации для отладки
                    printf("[ENEMY] Pig Demon animations after loading extra FBX:\n");
                    for (auto& kv : gModelPigDemon.proto.animIndex)
                        printf("[ENEMY]   '%s'\n", kv.first.c_str());

                    // Перезапускаем авто-детект — теперь доступны анимации из FBX
                    // Но _autoDetect приватный, поэтому делаем вручную через _find
                    auto& s = gModelPigDemon;
                    auto _findAnim = [&](std::initializer_list<const char*> keys) -> std::string {
                        for (auto k : keys)
                            if (gModelPigDemon.proto.hasAnim(k)) return k;
                        for (auto k : keys) {
                            std::string lo(k);
                            for (auto& c : lo) c = (char)tolower((unsigned char)c);
                            for (auto& kv : gModelPigDemon.proto.animIndex) {
                                std::string klo = kv.first;
                                for (auto& c : klo) c = (char)tolower((unsigned char)c);
                                if (klo.find(lo) != std::string::npos) return kv.first;
                            }
                        }
                        return "";
                        };

                    // Ходьба / бег
                    if (s.WALK.empty())
                        s.WALK = _findAnim({ "Walk_Fwd","MF_Walk_Fwd","ThirdPersonWalk",
                                            "walking","Walk","Run","run","ThirdPersonRun" });
                    // Бег (будет использован как charge)
                    std::string runAnim = _findAnim({ "Run_Fwd","MF_Run_Fwd","ThirdPersonRun",
                                                     "Sprint","sprint","Run","run" });
                    if (!runAnim.empty()) s.WALK = runAnim; // pig demon бегает, не ходит

                    // Атака — Fist_Fight или High_Kick из UE4 пака
                    if (s.SHOOT.empty())
                        s.SHOOT = _findAnim({ "Fist_Fight","Fist Fight","High_Kick","High Kick",
                                             "Attack","attack","Punch","punch","Slam","slam",
                                             "Melee","melee","Strike","strike","Combat_Idle",
                                             "ThirdPersonAttack","MF_Attack","Attack_01" });
                    // Второй удар для комбо
                    if (s.RELOAD.empty())
                        s.RELOAD = _findAnim({ "High_Kick","High Kick","Fist_Fight","Fist Fight",
                                              "Attack_02","Sprint","sprint" });
                    if (s.RELOAD.empty()) s.RELOAD = s.SHOOT;

                    // Idle — Combat_Idle или обычный Idle из UE4 пака
                    if (s.IDLE.empty())
                        s.IDLE = _findAnim({ "Combat_Idle","Combat Idle","Idle","idle",
                                            "ThirdPersonIdle","MF_Idle","Stand" });

                    // Смерть — Knock_Out
                    if (s.DEATH.empty())
                        s.DEATH = _findAnim({ "Knock_Out","Knock Out","KnockOut",
                                             "Death","death","Dying","dying","Dead",
                                             "ThirdPersonDeath","MF_Death" });
                    // Хит — Damaged
                    if (s.HIT.empty())
                        s.HIT = _findAnim({ "Damaged","damaged","Hit_React","HitReact",
                                           "hit reaction","Hit","hit","Getting Hit",
                                           "ThirdPersonHit","MF_Hit" });

                    // Фолбэки
                    if (s.IDLE.empty() && !gModelPigDemon.proto.animIndex.empty())
                        s.IDLE = gModelPigDemon.proto.animIndex.begin()->first;
                    if (s.WALK.empty())   s.WALK = s.IDLE;
                    if (s.SHOOT.empty())  s.SHOOT = s.IDLE;
                    if (s.RELOAD.empty()) s.RELOAD = s.SHOOT;
                    if (s.DEATH.empty())  s.DEATH = s.IDLE;
                    if (s.HIT.empty())    s.HIT = s.IDLE;

                    printf("[ENEMY] Pig Demon IDLE  = '%s'\n", s.IDLE.c_str());
                    printf("[ENEMY] Pig Demon WALK  = '%s'\n", s.WALK.c_str());
                    printf("[ENEMY] Pig Demon SHOOT = '%s'\n", s.SHOOT.c_str());
                    printf("[ENEMY] Pig Demon DEATH = '%s'\n", s.DEATH.c_str());
                    printf("[ENEMY] Pig Demon HIT   = '%s'\n", s.HIT.c_str());
                }
            }
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

    void spawnZombie2(glm::vec3 p)
    {
        _spawnEnemy(p, EnemyType::ZOMBIE2);
    }

    void spawnPigDemon(glm::vec3 p)
    {
        _spawnEnemy(p, EnemyType::PIG_DEMON);
    }

    void spawnPigDemonGroup(glm::vec3 center, int count, float radius = 5.f)
    {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831f;
            spawnPigDemon(center + glm::vec3(cosf(a) * radius, 0.f, sinf(a) * radius));
        }
    }

    void _spawnEnemy(glm::vec3 p, EnemyType t)
    {
        auto& sm = (t == EnemyType::ZOMBIE2) ? gModelZombie2 :
            (t == EnemyType::ZOMBIE) ? gModelZombie :
            (t == EnemyType::PIG_DEMON) ? gModelPigDemon : gModelSoldier;
        // Проверяем и loaded и meshes — loadExtraAnim может вызываться после load()
        if (!sm.loaded && sm.proto.meshes.empty()) {
            std::cerr << "[ENEMY] Model not loaded: " << (int)t << "\n";
            return;
        }
        // Для pig demon: loaded может быть true но meshes всё ещё грузятся
        if (t == EnemyType::PIG_DEMON && sm.proto.meshes.empty()) {
            std::cerr << "[ENEMY] Pig demon meshes empty!\n";
            return;
        }
        float gy = getGroundY(p, 200.f);
        if (gy != std::numeric_limits<float>::lowest()) p.y = gy;
        // Если пол не найден — оставляем Y как есть (позиция игрока)

        if ((int)enemies.size() >= 64) {
            std::cerr << "[ENEMY] Max enemies (64) reached!\n"; return;
        }
        enemies.reserve(std::max((int)enemies.capacity(), (int)enemies.size() + 8));
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
            e.scale = 0.01f;
        }
        else if (t == EnemyType::ZOMBIE2) {
            e.hp = 150.f;
            e.meleeRange = 2.0f;
            e.scale = 0.01f;
        }
        else if (t == EnemyType::PIG_DEMON) {
            e.hp = 350.f;     // толстый — много HP
            e.meleeRange = 2.5f;      // длинные руки
            e.scale = 0.01f;     // FBX в сантиметрах
            e.animSpeed = 1.0f;
        }
        e.init();
        enemies.push_back(std::move(e));
        // Форсируем первый updateAnim чтобы boneFinal сразу заполнился
        enemies.back().updateAnim(0.016f);
        std::cout << "[ENEMY] Spawned "
            << (t == EnemyType::PIG_DEMON ? "pig_demon" :
                t == EnemyType::ZOMBIE2 ? "zombie2" :
                t == EnemyType::ZOMBIE ? "zombie" : "soldier")
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

    void spawnZombie2Group(glm::vec3 center, int count, float radius = 5.f)
    {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831f;
            spawnZombie2(center + glm::vec3(cosf(a) * radius, 0.f, sinf(a) * radius));
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

        // Serial bone recalc (safe, parallel only needed for 10+ enemies)
        for (auto& e : enemies)
            if (!e.removed && e.sharedModel().loaded)
                e._recalcBonesParallel();

        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(),
                [](const Enemy& e) {return e.removed; }),
            enemies.end());
    }

    // Returns enemy index hit, -1 if miss
    // dmgOut = actual damage (headshot=150, chest=100, belly=75, legs=50)
    // shootDir needed for ragdoll impulse direction
    int rayHit(const glm::vec3& orig, const glm::vec3& dir,
        float maxDist = 200.f, float* dmgOut = nullptr,
        const glm::vec3& shootDir = glm::vec3(0, 0, 1))
    {
        float best = maxDist; int idx = -1; int bestDmg = 0;

        for (int i = 0; i < (int)enemies.size(); i++) {
            auto& e = enemies[i];
            if (e.isDead()) continue;

            // Precise hitbox test
            int zone; glm::vec3 hp;
            int dmg = e.hitbox.rayTest(orig, dir, e.pos, e.scale, zone, hp);
            if (dmg <= 0) continue;

            float t = glm::length(hp - orig);
            if (t < best) {
                best = t;
                idx = i;
                bestDmg = dmg;
            }
        }
        if (dmgOut) *dmgOut = (float)bestDmg;
        if (idx >= 0)
            enemies[idx].takeDamage((float)bestDmg, shootDir);
        return idx;
    }

    void draw(unsigned int shader, const glm::mat4& view, const glm::mat4& proj)
    {
        if (enemies.empty()) return;
        if (!gModelSoldier.loaded && !gModelZombie.loaded) return;
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
            // Sphere cull
            bool visible = true;
            if (!debugNoCull) {
                for (auto& p : planes) {
                    if (glm::dot(glm::vec3(p), e.pos) + p.w < -2.5f) {
                        visible = false; break;
                    }
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