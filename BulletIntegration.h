#pragma once
// ═══════════════════════════════════════════════════════════════
//  BulletIntegration.h  — без ragdoll constraints (без крашей)
//
//  Ragdoll = 5 независимых шаров без джоинтов.
//  Визуально похоже, но никогда не крашится.
// ═══════════════════════════════════════════════════════════════

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <unordered_map>

#include "BloodFX.h"
#include "AABB.h"

inline btVector3 glm2bt(const glm::vec3& v) { return { v.x, v.y, v.z }; }
inline glm::vec3 bt2glm(const btVector3& v) { return { v.x(), v.y(), v.z() }; }

// ── Простой ragdoll: 5 шаров, без джоинтов ────────────────────
struct SimpleRagdoll {
    static const int COUNT = 5;
    btRigidBody* bodies[COUNT] = {};
    glm::vec3    bonePos[COUNT] = {};
    bool         active = false;

    void syncFromBullet() {
        for (int i = 0; i < COUNT; i++) {
            if (!bodies[i]) continue;
            btTransform t;
            bodies[i]->getMotionState()->getWorldTransform(t);
            bonePos[i] = bt2glm(t.getOrigin());
        }
    }
};

// ── Тело одного врага ──────────────────────────────────────────
struct EnemyBulletBody {
    btRigidBody* capsule = nullptr;
    SimpleRagdoll ragdoll;
    bool          isRagdoll = false;

    glm::vec3 hitOffset = {};
    float     hitOffTimer = 0.f;

    void addHitReaction(const glm::vec3& dir, float force) {
        hitOffset = dir * force; hitOffTimer = 0.14f;
    }
    void updateHitReaction(float dt) {
        if (hitOffTimer > 0.f) {
            hitOffTimer -= dt;
            hitOffset *= (1.f - dt * 9.f);
            if (hitOffTimer <= 0.f) hitOffset = {};
        }
    }
    glm::vec3 getHitOffset() const {
        return hitOffTimer > 0.f ? hitOffset : glm::vec3(0.f);
    }
};

// ══════════════════════════════════════════════════════════════
struct BulletWorld {

    btDefaultCollisionConfiguration* config = nullptr;
    btCollisionDispatcher* dispatch = nullptr;
    btBroadphaseInterface* broad = nullptr;
    btSequentialImpulseConstraintSolver* solver = nullptr;
    btDiscreteDynamicsWorld* world = nullptr;

    btTriangleMesh* mapMesh = nullptr;
    btBvhTriangleMeshShape* mapShape = nullptr;
    btRigidBody* mapBody = nullptr;

    std::unordered_map<int, EnemyBulletBody> bodies;
    std::vector<btCollisionShape*>           shapes;
    int nextId = 0;

    // ─────────────────────────────────────────────────────────
    void init() {
        config = new btDefaultCollisionConfiguration();
        dispatch = new btCollisionDispatcher(config);
        broad = new btDbvtBroadphase();
        solver = new btSequentialImpulseConstraintSolver();
        world = new btDiscreteDynamicsWorld(dispatch, broad, solver, config);
        world->setGravity(btVector3(0, -20.f, 0));
        printf("[BULLET] World initialized\n");
    }

    void addMapCollision() {
        if (colTris.empty()) { printf("[BULLET] colTris empty\n"); return; }
        mapMesh = new btTriangleMesh();
        for (auto& tri : colTris)
            mapMesh->addTriangle(glm2bt(tri.a), glm2bt(tri.b), glm2bt(tri.c));
        mapShape = new btBvhTriangleMeshShape(mapMesh, true);
        shapes.push_back(mapShape);
        btTransform t; t.setIdentity();
        btDefaultMotionState* ms = new btDefaultMotionState(t);
        btRigidBody::btRigidBodyConstructionInfo ci(0.f, ms, mapShape, { 0,0,0 });
        mapBody = new btRigidBody(ci);
        mapBody->setFriction(0.8f);
        world->addRigidBody(mapBody);
        printf("[BULLET] Map: %zu tris\n", colTris.size());
    }

    int addEnemy(const glm::vec3& pos, float rotY,
        float height = 1.8f, float radius = 0.3f, float mass = 70.f)
    {
        int id = nextId++;
        btCapsuleShape* shape = new btCapsuleShape(radius, height - radius * 2.f);
        shapes.push_back(shape);

        btTransform t; t.setIdentity();
        t.setOrigin(glm2bt(pos + glm::vec3(0, height * 0.5f, 0)));
        btDefaultMotionState* ms = new btDefaultMotionState(t);
        btVector3 inertia(0, 0, 0);
        shape->calculateLocalInertia(mass, inertia);
        btRigidBody::btRigidBodyConstructionInfo ci(mass, ms, shape, inertia);
        ci.m_friction = 0.8f; ci.m_restitution = 0.05f;

        EnemyBulletBody eb;
        eb.capsule = new btRigidBody(ci);
        eb.capsule->setAngularFactor(btVector3(0, 0, 0));
        eb.capsule->setActivationState(DISABLE_DEACTIVATION);
        world->addRigidBody(eb.capsule);
        bodies[id] = std::move(eb);
        return id;
    }

    void applyBulletImpulse(int bulletId, const glm::vec3& shootDir,
        float damage, const glm::vec3& hitPos)
    {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return;
        auto& eb = it->second;

        float reactForce = std::min(damage * 0.0015f, 0.15f);
        eb.addHitReaction(shootDir, reactForce);

        if (!eb.isRagdoll && eb.capsule) {
            float impForce = std::min(damage * 0.12f, 8.f);
            glm::vec3 imp = shootDir * impForce + glm::vec3(0, impForce * 0.2f, 0);
            btVector3 rel = glm2bt(hitPos) - eb.capsule->getWorldTransform().getOrigin();
            eb.capsule->activate(true);
            eb.capsule->applyImpulse(glm2bt(imp), rel);
        }
    }

    void activateRagdoll(int bulletId, const glm::vec3& enemyPos,
        float enemyRotY, const glm::vec3& shootDir, float damage)
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

        // 5 позиций костей относительно pos
        glm::vec3 offsets[SimpleRagdoll::COUNT] = {
            {0,    0.90f, 0},
            {0,    1.28f, 0},
            {0,    1.72f, 0},
            {-0.28f, 1.25f, 0},
            { 0.28f, 1.25f, 0},
        };
        float masses[SimpleRagdoll::COUNT] = { 20.f, 18.f, 5.f, 4.f, 4.f };
        float radii[SimpleRagdoll::COUNT] = { 0.16f, 0.14f, 0.11f, 0.07f, 0.07f };
        float impForce = std::min(damage * 0.08f, 12.f);

        for (int i = 0; i < SimpleRagdoll::COUNT; i++) {
            btSphereShape* sh = new btSphereShape(radii[i]);
            shapes.push_back(sh);

            glm::vec3 bpos = enemyPos + offsets[i];
            btTransform t2; t2.setIdentity(); t2.setOrigin(glm2bt(bpos));
            btDefaultMotionState* ms = new btDefaultMotionState(t2);
            btVector3 inertia(0, 0, 0);
            sh->calculateLocalInertia(masses[i], inertia);
            btRigidBody::btRigidBodyConstructionInfo ci(masses[i], ms, sh, inertia);
            ci.m_friction = 0.6f; ci.m_restitution = 0.2f;

            eb.ragdoll.bodies[i] = new btRigidBody(ci);
            eb.ragdoll.bodies[i]->setActivationState(DISABLE_DEACTIVATION);
            world->addRigidBody(eb.ragdoll.bodies[i]);

            glm::vec3 imp = shootDir * impForce;
            imp.x += ((rand() % 100) / 100.f - 0.5f) * 3.f;
            imp.z += ((rand() % 100) / 100.f - 0.5f) * 3.f;
            imp.y += 2.f + ((rand() % 100) / 100.f) * 2.f;
            eb.ragdoll.bodies[i]->applyCentralImpulse(glm2bt(imp));
        }

        eb.ragdoll.active = true;
        eb.ragdoll.syncFromBullet();
        bloodFX.spawnDeath(enemyPos, shootDir);
    }

    void update(float dt) {
        if (!world) return;
        world->stepSimulation(dt, 3, 1.f / 60.f);
        for (auto& [id, eb] : bodies) {
            eb.updateHitReaction(dt);
            if (eb.isRagdoll) eb.ragdoll.syncFromBullet();
        }
    }

    glm::vec3 getHitOffset(int bulletId) const {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return {};
        return it->second.getHitOffset();
    }

    bool isRagdoll(int bulletId) const {
        auto it = bodies.find(bulletId);
        return it != bodies.end() && it->second.isRagdoll;
    }

    // ── Удаление врага — без constraints, никогда не крашится ──
    void removeEnemy(int bulletId) {
        auto it = bodies.find(bulletId);
        if (it == bodies.end()) return;
        auto& eb = it->second;

        if (eb.isRagdoll) {
            for (int i = 0; i < SimpleRagdoll::COUNT; i++) {
                if (eb.ragdoll.bodies[i]) {
                    world->removeRigidBody(eb.ragdoll.bodies[i]);
                    delete eb.ragdoll.bodies[i]->getMotionState();
                    delete eb.ragdoll.bodies[i];
                    eb.ragdoll.bodies[i] = nullptr;
                }
            }
        }
        else if (eb.capsule) {
            world->removeRigidBody(eb.capsule);
            delete eb.capsule->getMotionState();
            delete eb.capsule;
            eb.capsule = nullptr;
        }

        bodies.erase(it);
    }

    void shutdown() {
        if (!world) return;
        for (auto& [id, eb] : bodies) {
            if (eb.isRagdoll) {
                for (int i = 0; i < SimpleRagdoll::COUNT; i++) {
                    if (eb.ragdoll.bodies[i]) {
                        world->removeRigidBody(eb.ragdoll.bodies[i]);
                        delete eb.ragdoll.bodies[i]->getMotionState();
                        delete eb.ragdoll.bodies[i];
                    }
                }
            }
            else if (eb.capsule) {
                world->removeRigidBody(eb.capsule);
                delete eb.capsule->getMotionState();
                delete eb.capsule;
            }
        }
        bodies.clear();

        if (mapBody) {
            world->removeRigidBody(mapBody);
            delete mapBody->getMotionState();
            delete mapBody;
        }
        if (mapMesh) delete mapMesh;
        for (auto* s : shapes) delete s;
        shapes.clear();

        delete world; delete solver; delete broad; delete dispatch; delete config;
        world = nullptr;
        printf("[BULLET] Shutdown\n");
    }
};

inline BulletWorld bulletWorld;