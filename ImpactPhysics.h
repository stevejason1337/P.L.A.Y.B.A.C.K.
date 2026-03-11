#pragma once
// ═══════════════════════════════════════════════════════════════
//  ImpactPhysics.h  —  Физика попадания пули
//
//  Что делает:
//    • Hit Impulse  — кость дёргается в сторону выстрела (живой)
//    • Ragdoll      — полноценный физический труп (мёртвый)
//    • HitMarker    — экранный маркер (крест становится красным)
//
//  Зависит от: Physics.h, AABB.h, BloodFX.h
//
//  Использование:
//    1. #include "ImpactPhysics.h" в Enemy.h
//    2. Враг хранит ImpactBody body;
//    3. При попадании: body.onHit(hitPos, shootDir, damage, isDead);
//    4. В update врага:  body.update(dt);
//    5. При рендере: body.getBoneOffset(boneIdx) добавляй к позиции кости
// ═══════════════════════════════════════════════════════════════

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <imgui.h>
#include "Physics.h"    // Ragdoll, RagdollBone, getGroundY
#include "AABB.h"       // bvh, wallCollide
#include "BloodFX.h"

// ───────────────────────────────────────────────────────────────
//  HIT REACTION  — короткий толчок кости при попадании
//  Применяется пока враг жив: лёгкое дёргание тела
// ───────────────────────────────────────────────────────────────
struct HitReaction {
    struct BoneImpulse {
        int       boneIdx = -1;
        glm::vec3 offset = glm::vec3(0.f);   // смещение кости в model-space
        float     timer = 0.f;               // оставшееся время анимации
        float     duration = 0.12f;
    };

    static constexpr int MAX_IMPULSES = 6;
    BoneImpulse impulses[MAX_IMPULSES];
    float       bodyShakeTimer = 0.f;    // общее дрожание тела
    glm::vec3   bodyShakeDir = {};

    // Вызывается при каждом попадании
    // boneIdx: 0=бёдра 1=торс 2=голова 3=лев.рука 4=прав.рука
    void addImpulse(int boneIdx, const glm::vec3& dir, float force)
    {
        // Ищем свободный слот (или самый старый)
        BoneImpulse* slot = nullptr;
        float minT = 9999.f;
        for (auto& imp : impulses) {
            if (imp.timer <= 0.f) { slot = &imp; break; }
            if (imp.timer < minT) { minT = imp.timer; slot = &imp; }
        }
        if (!slot) return;

        slot->boneIdx = boneIdx;
        slot->offset = dir * force;
        slot->timer = slot->duration;
    }

    // Общий толчок всего тела (для всех костей)
    void addBodyShake(const glm::vec3& dir, float force, float duration = 0.18f)
    {
        bodyShakeDir = dir * force;
        bodyShakeTimer = duration;
    }

    void update(float dt)
    {
        for (auto& imp : impulses) {
            if (imp.timer <= 0.f) continue;
            imp.timer -= dt;
            // Экспоненциальное затухание
            imp.offset *= (1.f - dt * 12.f);
        }
        if (bodyShakeTimer > 0.f) {
            bodyShakeTimer -= dt;
            bodyShakeDir *= (1.f - dt * 10.f);
        }
    }

    // Возвращает смещение для кости в model-space
    glm::vec3 getBoneOffset(int boneIdx) const
    {
        glm::vec3 total = glm::vec3(0.f);
        // Тело целиком
        if (bodyShakeTimer > 0.f) total += bodyShakeDir;
        // Конкретная кость
        for (auto& imp : impulses) {
            if (imp.timer > 0.f && (imp.boneIdx == boneIdx || imp.boneIdx == -1))
                total += imp.offset;
        }
        return total;
    }

    bool isActive() const {
        if (bodyShakeTimer > 0.f) return true;
        for (auto& imp : impulses) if (imp.timer > 0.f) return true;
        return false;
    }
};

// ───────────────────────────────────────────────────────────────
//  IMPACT BODY  — полный контроллер физики врага
//  Объединяет HitReaction (живой) и Ragdoll (мёртвый)
// ───────────────────────────────────────────────────────────────
struct ImpactBody {

    HitReaction reaction;   // hit reaction пока живой
    Ragdoll     ragdoll;    // активируется при смерти
    bool        dead = false;
    bool        settled = false;  // ragdoll успокоился

    // Зоны тела → индекс кости анимации
    // Настрой под индексы костей своей модели
    struct BoneMap {
        int hip = 0;
        int spine = 1;
        int head = 2;
        int lArm = 3;
        int rArm = 4;
    } boneMap;

    // ── Вызывается при попадании пули ────────────────────────
    // hitZone: 0=голова 1=торс 2=живот 3=ноги (из Hitbox)
    // damage:  урон (из Hitbox::rayTest)
    // isDead:  враг умер от этого попадания
    void onHit(const glm::vec3& hitPos,
        const glm::vec3& shootDir,
        int              hitZone,
        float            damage,
        bool             isDead,
        const glm::vec3& enemyPos,
        float            enemyRotY)
    {
        // Сила = пропорционально урону
        float force = damage * 0.0018f;
        // Направление — по вектору выстрела + вверх
        glm::vec3 dir = glm::normalize(shootDir + glm::vec3(0, 0.25f, 0));

        if (isDead) {
            // ── СМЕРТЬ: переходим в ragdoll ──────────────────
            dead = true;
            glm::vec3 deathImpulse = shootDir * (damage * 0.06f);
            deathImpulse.y = fabsf(deathImpulse.y) * 0.5f + 1.5f;
            ragdoll.activate(enemyPos, enemyRotY, deathImpulse);
        }
        else {
            // ── HIT REACTION: дёргаем кость ──────────────────
            int boneIdx = -1;
            switch (hitZone) {
            case 0: boneIdx = boneMap.head;  force *= 1.8f; break; // голова
            case 1: boneIdx = boneMap.spine; force *= 1.0f; break; // торс
            case 2: boneIdx = boneMap.hip;   force *= 0.8f; break; // живот
            case 3: boneIdx = boneMap.hip;   force *= 0.6f; break; // ноги
            }
            // Ограничиваем max смещение чтобы не выглядело дико
            force = std::min(force, 0.18f);
            reaction.addImpulse(boneIdx, dir, force);
            reaction.addBodyShake(dir, force * 0.4f, 0.15f);
        }
    }

    // ── Упрощённый вызов без зоны (для быстрого подключения) ─
    void onHitSimple(const glm::vec3& hitPos,
        const glm::vec3& shootDir,
        float            damage,
        bool             isDead,
        const glm::vec3& enemyPos,
        float            enemyRotY)
    {
        // Определяем зону по высоте относительно позиции врага
        float dy = hitPos.y - enemyPos.y;
        int zone = (dy > 1.6f) ? 0 : (dy > 1.1f) ? 1 : (dy > 0.7f) ? 2 : 3;
        onHit(hitPos, shootDir, zone, damage, isDead, enemyPos, enemyRotY);
    }

    void update(float dt)
    {
        if (dead) {
            if (!settled) {
                ragdoll.update(dt);
                settled = ragdoll.isSettled() && ragdoll.timer > 1.5f;
            }
        }
        else {
            reaction.update(dt);
        }
    }

    // Смещение кости для анимации (добавляй к позиции кости в модели)
    glm::vec3 getBoneOffset(int animBoneIdx) const
    {
        if (dead) return glm::vec3(0.f); // ragdoll сам двигает кости
        return reaction.getBoneOffset(animBoneIdx);
    }

    // Матрица кости для ragdoll рендера
    // boneRole: 0=бёдра 1=позвоноч 2=голова 3=лев.рука 4=прав.рука
    glm::mat4 getRagdollBoneMatrix(int boneRole, float modelScale) const
    {
        static const int roleToRagBone[5] = {
            Ragdoll::HIP, Ragdoll::SPINE, Ragdoll::HEAD,
            Ragdoll::LARM, Ragdoll::RARM
        };
        int rb = (boneRole >= 0 && boneRole < 5) ? roleToRagBone[boneRole] : 0;
        return ragdoll.getBoneMatrix(rb, modelScale);
    }

    bool isRagdoll()  const { return dead && ragdoll.active; }
    bool isSettled()  const { return settled; }
    bool hasReaction()const { return !dead && reaction.isActive(); }

    // Позиция тела (для LOD/AI)
    glm::vec3 getPosition(const glm::vec3& defaultPos) const {
        if (isRagdoll()) return ragdoll.bones[Ragdoll::HIP].pos;
        return defaultPos;
    }
};

// ───────────────────────────────────────────────────────────────
//  HIT MARKER  — экранный индикатор попадания
//  Рисует красный крест через ImGui DrawList
// ───────────────────────────────────────────────────────────────
struct HitMarker {
    float timer = 0.f;
    float duration = 0.25f;
    bool  headshot = false;
    int   damage = 0;

    void trigger(int dmg, bool hs = false) {
        timer = duration;
        damage = dmg;
        headshot = hs;
    }

    // Вызывать после ImGui::NewFrame() перед Render()
    void draw() {
        if (timer <= 0.f) return;
        timer -= ImGui::GetIO().DeltaTime;
        if (timer <= 0.f) return;

        float t = timer / duration;  // 1=только появился, 0=исчезает
        float cx = ImGui::GetIO().DisplaySize.x * 0.5f;
        float cy = ImGui::GetIO().DisplaySize.y * 0.5f;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // Цвет: красный при обычном, золотой при хедшоте
        ImU32 col = headshot
            ? IM_COL32(255, 210, 30, (int)(t * 255))
            : IM_COL32(235, 30, 30, (int)(t * 230));

        float sz = headshot ? 11.f : 8.f;
        float gap = 3.5f;
        float thick = 2.5f;

        // 4 черты разлетаются от центра
        float spread = (1.f - t) * 4.f;
        float g = gap + spread;

        dl->AddLine({ cx - g - sz, cy }, { cx - g, cy }, col, thick);
        dl->AddLine({ cx + g,    cy }, { cx + g + sz, cy }, col, thick);
        dl->AddLine({ cx, cy - g - sz }, { cx, cy - g }, col, thick);
        dl->AddLine({ cx, cy + g }, { cx, cy + g + sz }, col, thick);

        // Хедшот: дополнительный кружок
        if (headshot) {
            dl->AddCircle({ cx, cy }, g + sz * 0.6f, col, 16, thick);
        }

        // Цифры урона — всплывают вверх
        if (damage > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", damage);
            float fadeY = cy - 28.f - (1.f - t) * 18.f;
            ImVec2 tsz = ImGui::CalcTextSize(buf);
            float alpha = t * 0.95f;
            ImU32 tcol = headshot
                ? IM_COL32(255, 220, 50, (int)(alpha * 255))
                : IM_COL32(240, 240, 240, (int)(alpha * 220));
            dl->AddText(ImGui::GetFont(), 16.f * (0.9f + t * 0.2f),
                { cx - tsz.x * 0.5f, fadeY }, tcol, buf);
        }
    }
};

inline HitMarker gHitMarker;

// ───────────────────────────────────────────────────────────────
//  Вспомогательная функция — вызывай вместо doShoot()
//  Автоматически: raycast → hitbox → ImpactBody → BloodFX → HitMarker
// ───────────────────────────────────────────────────────────────
// Прототип — реализован в Enemy.h / EnemyManager
// Возвращает true если враг поражён, заполняет outDamage/outDead/outHitPos
struct EnemyHitResult {
    bool      hit = false;
    int       damage = 0;
    bool      dead = false;
    glm::vec3 hitPos = {};
    glm::vec3 normal = {};  // нормаль поверхности для крови
    bool      headshot = false;
};

// Дополнительный выстрел с эффектами (обёртка над doShoot)
// Вызывай вместо doShoot() если хочешь полные эффекты
inline void doShootFX(const glm::vec3& camPos,
    const glm::vec3& camFront,
    float            fireRate,
    float            recoilKick,
    const EnemyHitResult& hitResult)
{
    // Стандартная логика выстрела
    doShoot(camPos, camFront, fireRate, recoilKick);

    if (hitResult.hit) {
        // Кровь из точки попадания
        glm::vec3 norm = (glm::length(hitResult.normal) > 0.1f)
            ? hitResult.normal
            : -camFront;

        bloodFX.spawnHit(hitResult.hitPos, norm, camFront,
            hitResult.headshot ? 35 : 22);

        // HitMarker
        gHitMarker.trigger(hitResult.damage, hitResult.headshot);

        // Дополнительный брыздг при смерти
        if (hitResult.dead)
            bloodFX.spawnDeath(hitResult.hitPos, camFront);
    }
    else {
        // Попадание в стену — маленький брызг (не кровь)
        // можно заменить на bullet hole эффект
        glm::vec3 wallHit;
        if (shootRay(camPos, camFront, wallHit)) {
            // spark эффект — оставляем bullet hole (уже есть в Player.h)
        }
    }
}