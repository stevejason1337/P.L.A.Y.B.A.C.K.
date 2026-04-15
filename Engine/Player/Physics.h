#pragma once

// ============================================================
//  Engine/Physics.h  —  ДВИЖОК, не трогаешь
//  Обёртка над Bullet Physics.
//  Игра не трогает btDynamicsWorld напрямую.
// ============================================================

#include <btBulletDynamicsCommon.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <functional>
#include <iostream>

// Конвертация GLM ↔ Bullet
inline btVector3    glmToBt(const glm::vec3& v) { return btVector3(v.x, v.y, v.z); }
inline glm::vec3    btToGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }

// ── RigidBody компонент ────────────────────────────────────
#include "Component.h"

struct RigidBodyDesc {
    float     mass        = 1.f;
    glm::vec3 boxHalfSize = glm::vec3(0.5f);   // для Box collider
    bool      isKinematic = false;
    bool      isTrigger   = false;
    float     friction    = 0.5f;
    float     restitution = 0.1f;
    bool      noRotation  = false;              // lock rotation (для capsule игрока)
};

class PhysicsWorld;  // forward

class RigidBody : public Component {
public:
    btRigidBody*    body       = nullptr;
    RigidBodyDesc   desc;

    explicit RigidBody(RigidBodyDesc d = {}) : desc(d) {}

    void setLinearVelocity(const glm::vec3& v) {
        if (body) body->setLinearVelocity(glmToBt(v));
    }
    glm::vec3 getLinearVelocity() const {
        return body ? btToGlm(body->getLinearVelocity()) : glm::vec3(0.f);
    }

    void applyImpulse(const glm::vec3& impulse) {
        if (body) { body->activate(); body->applyCentralImpulse(glmToBt(impulse)); }
    }

    void setPosition(const glm::vec3& pos) {
        if (!body) return;
        btTransform t;
        body->getMotionState()->getWorldTransform(t);
        t.setOrigin(glmToBt(pos));
        body->getMotionState()->setWorldTransform(t);
        body->setWorldTransform(t);
        body->activate();
    }

    glm::vec3 getPosition() const {
        if (!body) return glm::vec3(0.f);
        btTransform t;
        body->getMotionState()->getWorldTransform(t);
        return btToGlm(t.getOrigin());
    }

    bool isGrounded = false;   // PhysicsWorld обновляет это каждый кадр

    void onDestroy() override;   // реализация внизу, после PhysicsWorld
};

// ── Physics World ──────────────────────────────────────────
class PhysicsWorld {
public:
    static PhysicsWorld& get() {
        static PhysicsWorld instance;
        return instance;
    }

    bool init() {
        collisionConfig   = std::make_unique<btDefaultCollisionConfiguration>();
        dispatcher        = std::make_unique<btCollisionDispatcher>(collisionConfig.get());
        broadphase        = std::make_unique<btDbvtBroadphase>();
        solver            = std::make_unique<btSequentialImpulseConstraintSolver>();
        world             = std::make_unique<btDiscreteDynamicsWorld>(
                                dispatcher.get(), broadphase.get(),
                                solver.get(), collisionConfig.get());
        world->setGravity(btVector3(0, -9.81f, 0));
        std::cout << "[PhysicsWorld] Initialized\n";
        return true;
    }

    void shutdown() {
        // Bullet очищает объекты вместе с world
        world.reset();
    }

    // Вызывать в игровом цикле
    void step(float dt) {
        world->stepSimulation(dt, 10, 1.f/120.f);
    }

    // Создать RigidBody для компонента
    void registerBody(RigidBody* rb, const glm::vec3& startPos) {
        btCollisionShape* shape = new btBoxShape(glmToBt(rb->desc.boxHalfSize));

        btVector3 inertia(0,0,0);
        if (rb->desc.mass > 0.f)
            shape->calculateLocalInertia(rb->desc.mass, inertia);

        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(glmToBt(startPos));

        auto* motionState = new btDefaultMotionState(transform);
        btRigidBody::btRigidBodyConstructionInfo ci(rb->desc.mass, motionState, shape, inertia);
        ci.m_friction    = rb->desc.friction;
        ci.m_restitution = rb->desc.restitution;

        rb->body = new btRigidBody(ci);
        if (rb->desc.isKinematic) {
            rb->body->setCollisionFlags(rb->body->getCollisionFlags() |
                btCollisionObject::CF_KINEMATIC_OBJECT);
            rb->body->setActivationState(DISABLE_DEACTIVATION);
        }
        if (rb->desc.noRotation) {
            rb->body->setAngularFactor(btVector3(0,0,0));
        }

        world->addRigidBody(rb->body);
        bodies.push_back(rb);
    }

    void removeBody(RigidBody* rb) {
        if (!rb->body) return;
        world->removeRigidBody(rb->body);
        bodies.erase(std::remove(bodies.begin(), bodies.end(), rb), bodies.end());
        delete rb->body->getMotionState();
        delete rb->body->getCollisionShape();
        delete rb->body;
        rb->body = nullptr;
    }

    // Raycast — возвращает точку попадания и нормаль
    struct RayHit {
        bool      hit     = false;
        glm::vec3 point   = {};
        glm::vec3 normal  = {};
        RigidBody* body   = nullptr;
    };

    RayHit raycast(const glm::vec3& from, const glm::vec3& to) const {
        btVector3 bFrom = glmToBt(from);
        btVector3 bTo   = glmToBt(to);
        btCollisionWorld::ClosestRayResultCallback cb(bFrom, bTo);
        world->rayTest(bFrom, bTo, cb);
        if (!cb.hasHit()) return {};
        RayHit hit;
        hit.hit    = true;
        hit.point  = btToGlm(cb.m_hitPointWorld);
        hit.normal = btToGlm(cb.m_hitNormalWorld);
        // найти кому принадлежит тело
        for (auto* rb : bodies)
            if (rb->body == cb.m_collisionObject)
                hit.body = rb;
        return hit;
    }

    void setGravity(const glm::vec3& g) {
        world->setGravity(glmToBt(g));
    }

    btDiscreteDynamicsWorld* raw() { return world.get(); }

private:
    std::unique_ptr<btDefaultCollisionConfiguration>      collisionConfig;
    std::unique_ptr<btCollisionDispatcher>                dispatcher;
    std::unique_ptr<btDbvtBroadphase>                     broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver>  solver;
    std::unique_ptr<btDiscreteDynamicsWorld>              world;
    std::vector<RigidBody*>                               bodies;
};

// Реализация onDestroy (нужен полный тип PhysicsWorld)
inline void RigidBody::onDestroy() {
    PhysicsWorld::get().removeBody(this);
}
