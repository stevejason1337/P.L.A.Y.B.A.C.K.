#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include "ModelLoader.h"
#include "AABB.h"

const float ENEMY_SPEED = 2.5f;
const float ENEMY_DETECT_DIST = 25.f;
const float ENEMY_ATTACK_DIST = 1.8f;
const float ENEMY_ATTACK_DMG = 10.f;
const float ENEMY_HP = 3.f;
const float ENEMY_SCALE = 1.0f;  // меняй если модель слишком большая/маленькая

enum class EnemyState { IDLE, CHASE, ATTACK, DEAD };

struct Enemy
{
    glm::vec3  pos = glm::vec3(0.f);
    float      rotY = 0.f;
    float      hp = ENEMY_HP;
    EnemyState state = EnemyState::IDLE;
    float      deadTimer = 0.f;
    float      deadAngle = 0.f;
    bool       removed = false;

    bool isDead() const { return state == EnemyState::DEAD; }

    void takeDamage(float dmg)
    {
        if (isDead()) return;
        hp -= dmg;
        std::cout << "[ENEMY] Hit! HP:" << hp << "\n";
        if (hp <= 0.f) {
            hp = 0.f;
            state = EnemyState::DEAD;
            std::cout << "[ENEMY] Dead!\n";
        }
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        if (removed) return;

        if (isDead()) {
            deadTimer += dt;
            deadAngle = std::min(deadAngle + dt * 120.f, 90.f);
            if (deadTimer > 3.f) removed = true;
            return;
        }

        glm::vec3 toPlayer = playerPos - pos;
        toPlayer.y = 0.f;
        float dist = glm::length(toPlayer);

        if (dist > 0.01f)
            rotY = glm::degrees(atan2f(toPlayer.x, toPlayer.z));

        if (dist > ENEMY_DETECT_DIST) {
            state = EnemyState::IDLE;
        }
        else if (dist > ENEMY_ATTACK_DIST) {
            state = EnemyState::CHASE;
            glm::vec3 dir = glm::normalize(toPlayer);
            glm::vec3 newPos = pos + dir * ENEMY_SPEED * dt;
            float gy = getGroundY(newPos);
            newPos.y = (gy != std::numeric_limits<float>::lowest()) ? gy : pos.y;
            pos = newPos;
        }
        else {
            state = EnemyState::ATTACK;
            playerHP -= ENEMY_ATTACK_DMG * dt;
            if (playerHP < 0.f) playerHP = 0.f;
        }
    }

    glm::mat4 getMatrix() const
    {
        glm::mat4 m = glm::mat4(1.f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(rotY), glm::vec3(0, 1, 0));
        if (isDead())
            m = glm::rotate(m, glm::radians(deadAngle), glm::vec3(1, 0, 0));
        m = glm::scale(m, glm::vec3(ENEMY_SCALE));
        return m;
    }
};

struct EnemyManager
{
    std::vector<GPUMesh> meshes;
    std::vector<Enemy>   enemies;

    void load()
    {
        meshes = loadModel(
            "models/characters/soldier/soldier.glb",
            "",
            glm::mat4(1.f),
            false
        );
        std::cout << "[ENEMY] Loaded. Meshes:" << meshes.size() << "\n";
    }

    void spawn(glm::vec3 p)
    {
        float gy = getGroundY(p);
        if (gy != std::numeric_limits<float>::lowest()) p.y = gy;
        Enemy e; e.pos = p;
        enemies.push_back(e);
        std::cout << "[ENEMY] Spawned at (" << p.x << "," << p.y << "," << p.z << ")\n";
    }

    void spawnGroup(glm::vec3 center, int count, float radius = 4.f)
    {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831f;
            glm::vec3 p = center + glm::vec3(cosf(a) * radius, 0, sinf(a) * radius);
            spawn(p);
        }
    }

    void update(float dt, const glm::vec3& playerPos, float& playerHP)
    {
        for (auto& e : enemies)
            e.update(dt, playerPos, playerHP);

        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(),
                [](const Enemy& e) { return e.removed; }),
            enemies.end()
        );
    }

    int rayHit(const glm::vec3& orig, const glm::vec3& dir, float maxDist = 200.f)
    {
        float best = maxDist;
        int   idx = -1;
        for (int i = 0; i < (int)enemies.size(); i++) {
            if (enemies[i].isDead()) continue;
            float r = 0.6f;
            glm::vec3 oc = orig - enemies[i].pos;
            float b = glm::dot(oc, dir);
            float c = glm::dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc < 0.f) continue;
            float t = -b - sqrtf(disc);
            if (t > 0.f && t < best) { best = t; idx = i; }
        }
        return idx;
    }

    void draw(unsigned int shader, const glm::mat4& view, const glm::mat4& proj)
    {
        if (meshes.empty()) return;
        glUseProgram(shader);
        for (auto& e : enemies) {
            glm::mat4 model = e.getMatrix();
            glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

            glm::vec3 col = e.isDead() ? glm::vec3(0.25f, 0.2f, 0.15f)
                : e.state == EnemyState::ATTACK ? glm::vec3(1.f, 0.2f, 0.2f)
                : e.state == EnemyState::CHASE ? glm::vec3(0.9f, 0.6f, 0.1f)
                : glm::vec3(0.5f, 0.45f, 0.4f);

            glUniform3fv(glGetUniformLocation(shader, "baseColor"), 1, glm::value_ptr(col));
            glUniform1i(glGetUniformLocation(shader, "hasTexture"), 0);

            for (auto& m : meshes) {
                glBindVertexArray(m.VAO);
                glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
    }
};

inline EnemyManager enemyManager;