#pragma once
// ═══════════════════════════════════════════════════════════════
//  BulletIntegration.h
//
//  Подключает Bullet Physics к твоему Enemy/Player/World.
//
//  Что даёт:
//    • Каждый враг — btCapsuleShape rigidbody
//    • При выстреле: btRigidBody::applyImpulse() — тело реально
//      отлетает в сторону выстрела (сила зависит от урона)
//    • При смерти: ragdoll из 5 btRigidBody + 4 btHingeConstraint
//    • Брызги крови (BloodFX) вызываются автоматически
//    • Карта (BVH триангулированная) добавляется как btBvhTriangleMeshShape
//
//  Подключение:
//    1. #include "BulletIntegration.h"  в main.cpp  (после glad)
//    2. bulletWorld.init();             после OpenGL/BVH
//    3. bulletWorld.addMapCollision();  после bvh.build()
//    4. В spawn(): bulletWorld.addEnemy(e);
//    5. В update(): bulletWorld.update(dt);
//    6. В takeDamage(): bulletWorld.applyBulletImpulse(e, shootDir, dmg);
//    7. В смерти:   bulletWorld.activateRagdoll(e, shootDir, dmg);
//    8. Cleanup:    bulletWorld.shutdown();
// ═══════════════════════════════════════════════════════════════

// ── Bullet headers ──────────────────────────────────────────────
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <unordered_map>

#include "BloodFX.h"    // брызги крови
#include "AABB.h"       // colTris для карты

// ── GLM ↔ Bullet конвертеры ────────────────────────────────────
inline btVector3    glm2bt(const glm::vec3& v)  { return { v.x, v.y, v.z }; }
inline glm::vec3    bt2glm(const btVector3& v)  { return { v.x(), v.y(), v.z() }; }
inline btTransform  glm2btT(const glm::vec3& p, float rotY = 0.f) {
    btTransform t; t.setIdentity();
    t.setOrigin(glm2bt(p));
    btQuaternion q(btVector3(0,1,0), btScalar(glm::radians(rotY)));
    t.setRotation(q);
    return t;
}

// ═══════════════════════════════════════════════════════════════
//  RAGDOLL — 5 костей, 4 джоинта
// ═══════════════════════════════════════════════════════════════
struct BulletRagdoll {
    enum Bone { HIP=0, SPINE=1, HEAD=2, LARM=3, RARM=4, COUNT=5 };

    btRigidBody*        bodies[COUNT]  = {};
    btTypedConstraint*  joints[4]      = {};  // hip-spine, spine-head, spine-larm, spine-rarm
    bool                active = false;

    // Позиции костей для рендера — обновляются из Bullet каждый кадр
    glm::vec3  bonePos[COUNT]  = {};
    glm::quat  boneRot[COUNT]  = {};

    void syncFromBullet() {
        for (int i = 0; i < COUNT; i++) {
            if (!bodies[i]) continue;
            btTransform t; bodies[i]->getMotionState()->getWorldTransform(t);
            bonePos[i] = bt2glm(t.getOrigin());
            btQuaternion q = t.getRotation();
            boneRot[i] = glm::quat(q.w(), q.x(), q.y(), q.z());
        }
    }

    // Матрица модели для кости (для рендера)
    glm::mat4 getBoneMatrix(int b, float scale) const {
        glm::mat4 m = glm::translate(glm::mat4(1.f), bonePos[b] / scale);
        m *= glm::mat4_cast(boneRot[b]);
        return m;
    }
};

// ═══════════════════════════════════════════════════════════════
//  ENEMY BODY — Bullet тело одного врага
// ═══════════════════════════════════════════════════════════════
struct EnemyBulletBody {
    btRigidBody*   capsule  = nullptr;   // живой враг — капсула
    BulletRagdoll  ragdoll;              // мёртвый — ragdoll
    bool           isRagdoll = false;

    // Hit reaction — временное смещение (не физика Bullet)
    glm::vec3 hitOffset    = {};
    float     hitOffTimer  = 0.f;

    // Позиция из Bullet (обновляй в update)
    glm::vec3 bulletPos = {};

    void addHitReaction(const glm::vec3& dir, float force) {
        hitOffset   = dir * force;
        hitOffTimer = 0.14f;
    }

    void updateHitReaction(float dt) {
        if (hitOffTimer > 0.f) {
            hitOffTimer -= dt;
            hitOffset   *= (1.f - dt * 9.f);
            if (hitOffTimer <= 0.f) hitOffset = {};
        }
    }

    glm::vec3 getHitOffset() const {
        return hitOffTimer > 0.f ? hitOffset : glm::vec3(0.f);
    }
};

// ═══════════════════════════════════════════════════════════════
//  BULLET WORLD — главный менеджер
// ═══════════════════════════════════════════════════════════════
struct BulletWorld {

    // ── Bullet объекты ────────────────────────────────────────
    btDefaultCollisionConfiguration*     config   = nullptr;
    btCollisionDispatcher*               dispatch = nullptr;
    btBroadphaseInterface*               broad    = nullptr;
    btSequentialImpulseConstraintSolver* solver   = nullptr;
    btDiscreteDynamicsWorld*             world    = nullptr;

    // Карта
    btTriangleMesh*         mapMesh  = nullptr;
    btBvhTriangleMeshShape* mapShape = nullptr;
    btRigidBody*            mapBody  = nullptr;

    // Тела врагов: enemyId → EnemyBulletBody
    std::unordered_map<int, EnemyBulletBody> bodies;

    // Все шейпы (для cleanup)
    std::vector<btCollisionShape*> shapes;
    // Все констрейнты (ragdoll joints)
    std::vector<btTypedConstraint*> constraints;

    int nextId = 0;

    // ─────────────────────────────────────────────────────────
    void init() {
        config   = new btDefaultCollisionConfiguration();
        dispatch = new btCollisionDispatcher(config);
        broad    = new btDbvtBroadphase();
        solver   = new btSequentialImpulseConstraintSolver();
        world    = new btDiscreteDynamicsWorld(dispatch, broad, solver, config);
        world->setGravity(btVector3(0, -20.f, 0));
        printf("[BULLET] World initialized\n");
    }

    // ─────────────────────────────────────────────────────────
    //  Добавляем карту как статический collider из colTris (BVH)
    // ─────────────────────────────────────────────────────────
    void addMapCollision() {
        if (colTris.empty()) {
            printf("[BULLET] colTris empty — map collision skipped\n");
            return;
        }
        mapMesh = new btTriangleMesh();
        for (auto& tri : colTris) {
            mapMesh->addTriangle(
                glm2bt(tri.a), glm2bt(tri.b), glm2bt(tri.c));
        }
        mapShape = new btBvhTriangleMeshShape(mapMesh, true);
        shapes.push_back(mapShape);

        btTransform t; t.setIdentity();
        btDefaultMotionState* ms = new btDefaultMotionState(t);
        btRigidBody::btRigidBodyConstructionInfo ci(0.f, ms, mapShape, {0,0,0});
        mapBody = new btRigidBody(ci);
        mapBody->setFriction(0.8f);
        world->addRigidBody(mapBody);
        printf("[BULLET] Map collision: %zu triangles\n", colTris.size());
    }

    // ─────────────────────────────────────────────────────────
    //  Добавить врага — вызывай в EnemyManager::spawn()
    //  Возвращает bulletId — сохраняй в Enemy
    // ─────────────────────────────────────────────────────────
    int addEnemy(const glm::vec3& pos, float rotY,
                 float height = 1.8f, float radius = 0.3f, float mass = 70.f)
    {
        int id = nextId++;

        // Капсула
        btCapsuleShape* shape = new btCapsuleShape(radius, height - radius*2.f);
        shapes.push_back(shape);

        btTransform t = glm2btT(pos + glm::vec3(0, height*0.5f, 0), rotY);
        btDefaultMotionState* ms = new btDefaultMotionState(t);

        btVector3 inertia(0,0,0);
        shape->calculateLocalInertia(mass, inertia);

        btRigidBody::btRigidBodyConstructionInfo ci(mass, ms, shape, inertia);
        ci.m_friction    = 0.8f;
        ci.m_restitution = 0.05f;   // почти не отскакивает

        EnemyBulletBody eb;
        eb.capsule = new btRigidBody(ci);
        // Не даём крутиться — капсула всегда вертикальная
        eb.capsule->setAngularFactor(btVector3(0,0,0));
        eb.capsule->setActivationState(DISABLE_DEACTIVATION);
        world->addRigidBody(eb.capsule);

        eb.bulletPos = pos;
        bodies[id] = std::move(eb);

        return id;
    }

    // ─────────────────────────────────────────────────────────
    //  Синхронизация позиции из Enemy.pos → Bullet
    //  (AI двигает врага, Bullet следует за ним)
    // ─────────────────────────────────────────────────────────
    void syncEnemyPos(int bulletId, const glm::vec3& pos, float rotY,
                      float height = 1.8f)
    {
        auto it = bodies.find(bulletId);
        if (it == bodies.end() || it->second.isRagdoll) return;
        auto* rb = it->second.capsule;
        if (!rb) return;

        btTransform t = glm2btT(pos + glm::vec3(0, height*0.5f, 0), rotY);
        rb->setWorldTransform(t);
        rb->getMotionState()->setWorldTransform(t);
        rb->setLinearVelocity({0,0,0});
        rb->setAngularVelocity({0,0,0});
    }

    // ─────────────────────────────────────────────────────────
    //  Применить импульс от пули — вызывай в takeDamage()
    // ─────────────────────────────────────────────────────────
    void applyBulletImpulse(int bulletId,
                            const glm::vec3& shootDir,
                            float damage,
                            const glm::vec3& hitPos)
    {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return;
        auto& eb = it->second;

        // Hit reaction (визуальное смещение)
        float reactForce = damage * 0.0015f;
        reactForce = std::min(reactForce, 0.15f);
        eb.addHitReaction(shootDir, reactForce);

        // Брызги крови в точке попадания
        glm::vec3 norm = glm::normalize(-shootDir + glm::vec3(0, 0.15f, 0));
        bloodFX.spawnHit(hitPos, norm, shootDir, (damage >= 150.f) ? 35 : 20);

        // Bullet impulse на капсулу (если не ragdoll)
        if (!eb.isRagdoll && eb.capsule) {
            float impForce = damage * 0.12f;   // сила пропорциональна урону
            impForce = std::min(impForce, 8.f);  // max чтобы не улетел в космос

            // Импульс в направлении выстрела + небольшой вверх
            glm::vec3 imp = shootDir * impForce + glm::vec3(0, impForce * 0.2f, 0);
            btVector3 btImp = glm2bt(imp);
            // Точка приложения — в точке попадания
            btVector3 btRel = glm2bt(hitPos) - eb.capsule->getWorldTransform().getOrigin();
            eb.capsule->activate(true);
            eb.capsule->applyImpulse(btImp, btRel);
        }
        else if (eb.isRagdoll) {
            // Ragdoll — импульс на ближайшую кость
            _applyRagdollImpulse(eb.ragdoll, hitPos, shootDir, damage);
        }
    }

    // ─────────────────────────────────────────────────────────
    //  Активировать ragdoll — вызывай при смерти врага
    // ─────────────────────────────────────────────────────────
    void activateRagdoll(int bulletId,
                         const glm::vec3& enemyPos,
                         float            enemyRotY,
                         const glm::vec3& shootDir,
                         float            damage)
    {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return;
        auto& eb = it->second;

        // Удаляем капсулу
        if (eb.capsule) {
            world->removeRigidBody(eb.capsule);
            delete eb.capsule->getMotionState();
            delete eb.capsule;
            eb.capsule = nullptr;
        }

        eb.isRagdoll = true;
        _buildRagdoll(eb.ragdoll, enemyPos, enemyRotY, shootDir, damage);

        // Много крови при смерти
        bloodFX.spawnDeath(enemyPos, shootDir);
    }

    // ─────────────────────────────────────────────────────────
    //  Update — вызывай каждый кадр
    // ─────────────────────────────────────────────────────────
    void update(float dt) {
        if (!world) return;
        // Bullet step (max 3 substeps по 1/60)
        world->stepSimulation(dt, 3, 1.f/60.f);

        // Синхронизируем позиции из Bullet
        for (auto& [id, eb] : bodies) {
            eb.updateHitReaction(dt);
            if (eb.isRagdoll) {
                eb.ragdoll.syncFromBullet();
            }
            else if (eb.capsule) {
                btTransform t;
                eb.capsule->getMotionState()->getWorldTransform(t);
                eb.bulletPos = bt2glm(t.getOrigin());
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    //  Получить hit reaction offset для рендера кости
    // ─────────────────────────────────────────────────────────
    glm::vec3 getHitOffset(int bulletId) const {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return {};
        return it->second.getHitOffset();
    }

    // Получить матрицу кости ragdoll для рендера
    // boneRole: 0=hip 1=spine 2=head 3=larm 4=rarm
    bool getRagdollBone(int bulletId, int boneRole,
                        float scale, glm::mat4& outMat) const
    {
        auto it = bodies.find(bulletId);
        if (it == bodies.end() || !it->second.isRagdoll) return false;
        outMat = it->second.ragdoll.getBoneMatrix(boneRole, scale);
        return true;
    }

    bool isRagdoll(int bulletId) const {
        auto it = bodies.find(bulletId);
        return it != bodies.end() && it->second.isRagdoll;
    }

    // ─────────────────────────────────────────────────────────
    //  Удалить врага (при removed=true)
    // ─────────────────────────────────────────────────────────
    void removeEnemy(int bulletId) {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return;
        auto& eb = it->second;

        if (eb.capsule) {
            world->removeRigidBody(eb.capsule);
            delete eb.capsule->getMotionState();
            delete eb.capsule;
        }
        if (eb.isRagdoll) _destroyRagdoll(eb.ragdoll);

        bodies.erase(it);
    }

    // ─────────────────────────────────────────────────────────
    void shutdown() {
        for (auto& [id, eb] : bodies) {
            if (eb.capsule) {
                world->removeRigidBody(eb.capsule);
                delete eb.capsule->getMotionState();
                delete eb.capsule;
            }
            if (eb.isRagdoll) _destroyRagdoll(eb.ragdoll);
        }
        bodies.clear();

        for (auto* c : constraints) { world->removeConstraint(c); delete c; }
        constraints.clear();

        if (mapBody)  { world->removeRigidBody(mapBody); delete mapBody->getMotionState(); delete mapBody; }
        if (mapShape) delete mapShape;
        if (mapMesh)  delete mapMesh;

        for (auto* s : shapes) delete s;
        shapes.clear();

        delete world; delete solver; delete broad; delete dispatch; delete config;
        world = nullptr;
        printf("[BULLET] Shutdown\n");
    }

private:
    // ── Строим ragdoll из 5 capsule rigidbody + 4 hinge joints ──
    void _buildRagdoll(BulletRagdoll& rd,
                       const glm::vec3& root,
                       float rotY,
                       const glm::vec3& shootDir,
                       float damage)
    {
        // Параметры костей: radius, height, смещение от root
        struct BoneDef { float r; float h; glm::vec3 offset; float mass; };
        BoneDef defs[BulletRagdoll::COUNT] = {
            { 0.16f, 0.28f, {0, 0.90f, 0}, 20.f },   // HIP
            { 0.14f, 0.32f, {0, 1.28f, 0}, 18.f },   // SPINE
            { 0.11f, 0.24f, {0, 1.72f, 0}, 5.f  },   // HEAD
            { 0.07f, 0.30f, {-0.28f,1.25f,0}, 4.f }, // LARM
            { 0.07f, 0.30f, { 0.28f,1.25f,0}, 4.f }, // RARM
        };

        float impForce = damage * 0.08f;
        impForce = std::min(impForce, 12.f);
        float strengths[BulletRagdoll::COUNT] = { 1.0f, 0.9f, 0.6f, 1.1f, 1.1f };

        for (int i = 0; i < BulletRagdoll::COUNT; i++) {
            auto& d = defs[i];
            btCapsuleShape* sh = new btCapsuleShape(d.r, d.h - d.r*2.f);
            shapes.push_back(sh);

            glm::vec3 bpos = root + d.offset;
            btTransform t; t.setIdentity(); t.setOrigin(glm2bt(bpos));
            btDefaultMotionState* ms = new btDefaultMotionState(t);

            btVector3 inertia(0,0,0);
            sh->calculateLocalInertia(d.mass, inertia);
            btRigidBody::btRigidBodyConstructionInfo ci(d.mass, ms, sh, inertia);
            ci.m_friction    = 0.6f;
            ci.m_restitution = 0.1f;
            rd.bodies[i] = new btRigidBody(ci);
            rd.bodies[i]->setActivationState(DISABLE_DEACTIVATION);
            world->addRigidBody(rd.bodies[i]);

            // Начальный импульс
            glm::vec3 imp = shootDir * impForce * strengths[i];
            imp.x += ((rand()%100)/100.f - 0.5f) * 3.f;
            imp.z += ((rand()%100)/100.f - 0.5f) * 3.f;
            imp.y += 1.5f + ((rand()%100)/100.f) * 2.f;
            rd.bodies[i]->applyCentralImpulse(glm2bt(imp));
        }

        // ── Джоинты (ограничивают вращение) ──────────────────
        // HIP ↔ SPINE
        _addHinge(rd, rd.bodies[BulletRagdoll::HIP], rd.bodies[BulletRagdoll::SPINE],
                  btVector3(0, 0.18f, 0), btVector3(0, -0.16f, 0),
                  -0.3f, 0.3f);
        // SPINE ↔ HEAD
        _addHinge(rd, rd.bodies[BulletRagdoll::SPINE], rd.bodies[BulletRagdoll::HEAD],
                  btVector3(0, 0.20f, 0), btVector3(0, -0.12f, 0),
                  -0.4f, 0.4f);
        // SPINE ↔ LARM
        _addHinge(rd, rd.bodies[BulletRagdoll::SPINE], rd.bodies[BulletRagdoll::LARM],
                  btVector3(-0.18f, 0.12f, 0), btVector3(0, 0.15f, 0),
                  -1.5f, 0.5f);
        // SPINE ↔ RARM
        _addHinge(rd, rd.bodies[BulletRagdoll::SPINE], rd.bodies[BulletRagdoll::RARM],
                  btVector3(0.18f, 0.12f, 0), btVector3(0, 0.15f, 0),
                  -1.5f, 0.5f);

        rd.active = true;
        rd.syncFromBullet();
    }

    void _addHinge(BulletRagdoll& rd,
                   btRigidBody* a, btRigidBody* b,
                   btVector3 pivA, btVector3 pivB,
                   float loLimit, float hiLimit)
    {
        btHingeConstraint* h = new btHingeConstraint(
            *a, *b, pivA, pivB,
            btVector3(0,0,1), btVector3(0,0,1));
        h->setLimit(loLimit, hiLimit, 0.9f, 0.3f);
        world->addConstraint(h, true);
        constraints.push_back(h);
    }

    void _applyRagdollImpulse(BulletRagdoll& rd,
                               const glm::vec3& hitPos,
                               const glm::vec3& dir,
                               float damage)
    {
        float bestDist = 9999.f;
        int   bestBone = BulletRagdoll::SPINE;
        for (int i = 0; i < BulletRagdoll::COUNT; i++) {
            if (!rd.bodies[i]) continue;
            float d = glm::distance(rd.bonePos[i], hitPos);
            if (d < bestDist) { bestDist = d; bestBone = i; }
        }
        float force = damage * 0.06f;
        glm::vec3 imp = dir * force + glm::vec3(0, force*0.3f, 0);
        rd.bodies[bestBone]->activate(true);
        rd.bodies[bestBone]->applyCentralImpulse(glm2bt(imp));
    }

    void _destroyRagdoll(BulletRagdoll& rd) {
        for (int i = 0; i < BulletRagdoll::COUNT; i++) {
            if (rd.bodies[i]) {
                world->removeRigidBody(rd.bodies[i]);
                delete rd.bodies[i]->getMotionState();
                delete rd.bodies[i];
                rd.bodies[i] = nullptr;
            }
        }
    }
};

inline BulletWorld bulletWorld;
