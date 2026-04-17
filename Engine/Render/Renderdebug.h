#pragma once
// ================================================================
//  RenderDebug.h — утиліти для порівняння GL і DX11
//  Підключай тільки в debug білдах
// ================================================================
#include <cstdio>
#include <string>

// ── Простий лічильник draw calls ──────────────────────────────
struct RenderStats {
    int drawCalls = 0;
    int triangles = 0;
    int textureBinds = 0;

    void reset() { drawCalls = triangles = textureBinds = 0; }

    void print(const char* api) const {
        printf("[%s] draws=%d  tris=%d  texBinds=%d\n",
            api, drawCalls, triangles, textureBinds);
    }
};

inline RenderStats gRenderStats;

// ── Macro: увімкни RENDER_DEBUG в налаштуваннях проекту ───────
#ifdef RENDER_DEBUG
#define RD_DRAW(indexCount)  do { gRenderStats.drawCalls++; gRenderStats.triangles += (indexCount)/3; } while(0)
#define RD_TEX()             do { gRenderStats.textureBinds++; } while(0)
#define RD_FRAME_END(api)    do { gRenderStats.print(api); gRenderStats.reset(); } while(0)
#else
#define RD_DRAW(n)  ((void)0)
#define RD_TEX()    ((void)0)
#define RD_FRAME_END(api) ((void)0)
#endif

// ── Порівняння параметрів між API ─────────────────────────────
// Виклич у init() щоб переконатись що константи синхронізовані
inline void renderSanityCheck() {
#ifdef RENDER_DEBUG
    printf("[RenderSanity] FOG_START=%.1f FOG_END=%.1f SHADOW_RES=%d\n",
        FOG_START, FOG_END, SHADOW_RES);
    printf("[RenderSanity] OK\n");
#endif
}