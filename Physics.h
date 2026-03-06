#pragma once
// Physics.h - capsule collider, gravity, impulses, ragdoll
// Include after AABB.h

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

// -------------------------------------------------------
// CAPSULE vs WORLD collision (replaces wallCollide)
// A capsule is defined by two endpoints + radius.
// Much better than AABB for characters - no corner snagging.
// -------------------------------------------------------
struct Capsule {
    glm::vec3 base;   // bottom center
    glm::vec3 tip;    // top center
    float     radius;
};

// Closest point on segment AB to point P
inline glm::vec3 closestPointOnSegment(
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& p)
{
    glm::vec3 ab = b - a;
    float len2 = glm::dot(ab, ab);
    if (len2 < 1e-8f) return a;
    float t = glm::clamp(glm::dot(p - a, ab) / len2, 0.f, 1.f);
    return a + ab * t;
}

// Push capsule out of world geometry using BVH sphere sweeps
// Returns true if any collision resolved
inline bool capsuleVsWorld(glm::vec3& pos, float height, float radius,
    int iterations = 3)
{
    if (bvh.nodes.empty()) return false;
    bool hit = false;

    for (int iter = 0; iter < iterations; iter++) {
        glm::vec3 base = pos + glm::vec3(0, radius, 0);
        glm::vec3 tip = pos + glm::vec3(0, height - radius, 0);

        // Sample 8 points around capsule at 3 heights
        bool resolved = false;
        float heights[] = { 0.15f, height * 0.5f, height - 0.1f };
        glm::vec3 dirs[] = {
            {1,0,0},{-1,0,0},{0,0,1},{0,0,-1},
            {0.707f,0,0.707f},{-0.707f,0,0.707f},
            {0.707f,0,-0.707f},{-0.707f,0,-0.707f}
        };

        for (float hy : heights) {
            glm::vec3 o = pos + glm::vec3(0, hy, 0);
            for (auto& d : dirs) {
                float h;
                if (bvh.raycast(o, d, radius + 0.02f, h)) {
                    float pen = radius - h;
                    if (pen > 0.f) {
                        pos -= d * pen;
                        resolved = true;
                        hit = true;
                    }
                }
            }
        }
        if (!resolved) break;
    }
    return hit;
}

// -------------------------------------------------------
// RIGIDBODY - simple physics body for characters
// -------------------------------------------------------
struct Rigidbody {
    glm::vec3 velocity = glm::vec3(0.f);
    glm::vec3 position = glm::vec3(0.f);
    float     mass = 70.f;   // kg
    float     height = 1.8f;   // capsule height
    float     radius = 0.35f;  // capsule radius
    float     friction = 8.f;    // horizontal damping
    float     bounciness = 0.05f;
    bool      onGround = false;
    bool      useGravity = true;
    bool      enabled = true;

    static constexpr float GRAVITY = -18.f; // m/s^2 (stronger than real - feels better)

    void addImpulse(const glm::vec3& imp) {
        velocity += imp / mass;
    }

    void addForce(const glm::vec3& force, float dt) {
        velocity += (force / mass) * dt;
    }

    // Call every frame
    void update(float dt)
    {
        if (!enabled) return;

        // Gravity
        if (useGravity && !onGround)
            velocity.y += GRAVITY * dt;

        // Clamp fall speed
        if (velocity.y < -40.f) velocity.y = -40.f;

        // Move
        position += velocity * dt;

        // Ground collision via BVH raycast
        float groundY = getGroundY(position, 4.f);
        if (groundY != std::numeric_limits<float>::lowest()) {
            float feetY = groundY;
            if (position.y <= feetY + 0.02f) {
                position.y = feetY;
                if (velocity.y < 0.f) {
                    // Bounce slightly or stop
                    velocity.y = -velocity.y * bounciness;
                    if (fabsf(velocity.y) < 0.1f) velocity.y = 0.f;
                }
                onGround = true;
            }
            else {
                onGround = false;
            }
        }
        else {
            onGround = false;
        }

        // Wall collision (capsule)
        capsuleVsWorld(position, height, radius);

        // Horizontal friction
        if (onGround) {
            float speed = glm::length(glm::vec2(velocity.x, velocity.z));
            if (speed > 0.f) {
                float drag = std::min(friction * dt, speed);
                glm::vec2 hv = glm::vec2(velocity.x, velocity.z);
                hv -= glm::normalize(hv) * drag;
                velocity.x = hv.x;
                velocity.z = hv.y;
            }
        }
    }
};

// -------------------------------------------------------
// RAGDOLL BONE - one physics bone for death simulation
// -------------------------------------------------------
struct RagdollBone {
    glm::vec3 pos = glm::vec3(0.f);
    glm::vec3 vel = glm::vec3(0.f);
    float     mass = 5.f;
    float     radius = 0.12f;
    bool      pinned = false; // root bone can be pinned initially

    void update(float dt)
    {
        if (pinned) return;
        vel.y += -18.f * dt; // gravity
        if (vel.y < -20.f) vel.y = -20.f;
        pos += vel * dt;

        // Ground
        float gy = getGroundY(pos, 3.f);
        if (gy != std::numeric_limits<float>::lowest() && pos.y < gy + radius) {
            pos.y = gy + radius;
            vel.y = -vel.y * 0.2f;  // low bounce
            // friction on ground
            vel.x *= 0.7f;
            vel.z *= 0.7f;
        }
    }
};

// -------------------------------------------------------
// RAGDOLL - full body death physics
// Simplified: 5 key bones (hips, spine, head, l/r arm)
// -------------------------------------------------------
struct Ragdoll {
    enum Bone { HIP = 0, SPINE, HEAD, LARM, RARM, COUNT };

    RagdollBone bones[COUNT];
    bool active = false;
    float timer = 0.f;

    // Activate from death position + death impulse
    void activate(const glm::vec3& rootPos, float rotY,
        const glm::vec3& deathImpulse)
    {
        active = true;
        timer = 0.f;

        float cy = cosf(glm::radians(rotY));
        float sy = sinf(glm::radians(rotY));

        // Place bones relative to root
        bones[HIP].pos = rootPos + glm::vec3(0, 0.9f, 0);
        bones[SPINE].pos = rootPos + glm::vec3(0, 1.35f, 0);
        bones[HEAD].pos = rootPos + glm::vec3(0, 1.75f, 0);
        bones[LARM].pos = rootPos + glm::vec3(-cy * 0.4f, 1.3f, -sy * 0.4f);
        bones[RARM].pos = rootPos + glm::vec3(cy * 0.4f, 1.3f, sy * 0.4f);

        // Apply impulse - different strength per bone
        float strengths[COUNT] = { 1.0f, 0.9f, 0.7f, 1.1f, 1.1f };
        for (int i = 0; i < COUNT; i++) {
            bones[i].vel = deathImpulse * strengths[i];
            // Add random spin
            bones[i].vel.x += ((float)(rand() % 100) / 100.f - 0.5f) * 3.f;
            bones[i].vel.z += ((float)(rand() % 100) / 100.f - 0.5f) * 3.f;
        }
    }

    void update(float dt)
    {
        if (!active) return;
        timer += dt;
        for (auto& b : bones) b.update(dt);
    }

    // Get transform matrix for a bone (for rendering override)
    glm::mat4 getBoneMatrix(int boneIdx, float scale) const
    {
        const auto& b = bones[boneIdx];
        glm::mat4 m = glm::translate(glm::mat4(1.f), b.pos / scale);
        return m;
    }

    // Is ragdoll still actively moving?
    bool isSettled() const
    {
        if (!active) return true;
        for (auto& b : bones) {
            if (glm::length(b.vel) > 0.05f) return false;
        }
        return true;
    }
};

// -------------------------------------------------------
// DEATH ANIMATOR - blends between death anim and ragdoll
// -------------------------------------------------------
struct DeathAnimator {
    enum Phase { NONE, PLAYING_ANIM, BLENDING, RAGDOLL };

    Phase   phase = NONE;
    float   blendTimer = 0.f;
    float   blendTime = 0.4f;  // seconds to blend into ragdoll
    Ragdoll ragdoll;

    void trigger(const glm::vec3& pos, float rotY,
        const glm::vec3& shootDir, float impactForce = 8.f)
    {
        phase = PLAYING_ANIM;
        blendTimer = 0.f;

        // Death impulse - opposite to shoot direction + upward kick
        glm::vec3 impulse = shootDir * impactForce;
        impulse.y = fabsf(impulse.y) + 2.f; // always goes up a bit
        ragdoll.activate(pos, rotY, impulse);
    }

    void update(float dt)
    {
        if (phase == NONE) return;

        if (phase == PLAYING_ANIM) {
            blendTimer += dt;
            if (blendTimer > 0.3f) {  // after 0.3s start blending
                phase = BLENDING;
                blendTimer = 0.f;
            }
        }

        if (phase == BLENDING || phase == RAGDOLL) {
            ragdoll.update(dt);
            blendTimer += dt;
            if (blendTimer > blendTime)
                phase = RAGDOLL;
        }
    }

    float blendWeight() const
    {
        if (phase == RAGDOLL)    return 1.f;
        if (phase == BLENDING)   return blendTimer / blendTime;
        return 0.f;
    }

    bool isActive() const { return phase != NONE; }
    bool isRagdoll() const { return phase == RAGDOLL; }
};

// -------------------------------------------------------
// HITBOX - per-enemy sphere hitbox for bullet detection
// Much more accurate than BVH raycast vs enemy
// -------------------------------------------------------
struct Hitbox {
    struct Sphere { glm::vec3 offset; float radius; int damage; };

    // Head, chest, belly, legs
    Sphere zones[4] = {
        { {0, 1.75f, 0}, 0.18f, 150 },  // head  - instant kill
        { {0, 1.35f, 0}, 0.28f, 100 },  // chest - normal
        { {0, 1.00f, 0}, 0.25f, 75  },  // belly
        { {0, 0.55f, 0}, 0.22f, 50  },  // legs  - weak
    };

    // Returns damage if ray hits, 0 if miss
    // hitZone: 0=head, 1=chest, 2=belly, 3=legs
    int rayTest(const glm::vec3& origin, const glm::vec3& dir,
        const glm::vec3& enemyPos, float enemyScale,
        int& hitZone, glm::vec3& hitPoint) const
    {
        float bestT = 1e30f;
        int   bestZ = -1;

        for (int i = 0; i < 4; i++) {
            glm::vec3 center = enemyPos + zones[i].offset;
            float r = zones[i].radius;

            // Ray vs sphere
            glm::vec3 oc = origin - center;
            float b = glm::dot(oc, dir);
            float c = glm::dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc < 0.f) continue;

            float t = -b - sqrtf(disc);
            if (t < 0.f) t = -b + sqrtf(disc);
            if (t < 0.f || t > bestT) continue;

            bestT = t;
            bestZ = i;
        }

        if (bestZ < 0) return 0;
        hitZone = bestZ;
        hitPoint = origin + dir * bestT;
        return zones[bestZ].damage;
    }

    // Headshot?
    static bool isHeadshot(int zone) { return zone == 0; }
};

// -------------------------------------------------------
// PHYSICS WORLD - manages all rigidbodies
// -------------------------------------------------------
struct PhysicsWorld {
    std::vector<Rigidbody*> bodies;

    void add(Rigidbody* rb) { bodies.push_back(rb); }
    void remove(Rigidbody* rb) {
        bodies.erase(std::remove(bodies.begin(), bodies.end(), rb), bodies.end());
    }

    void update(float dt)
    {
        for (auto* rb : bodies)
            if (rb && rb->enabled) rb->update(dt);
    }
};

inline PhysicsWorld gPhysicsWorld;