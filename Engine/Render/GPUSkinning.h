#pragma once
// ============================================================
//  GPUSkinning.h  —  DX11 Compute Shader skinning pipeline
// ============================================================
//  Ідея:
//    CPU раніше:  calcBonesFlat() -> boneFinal[100] -> _dxUp(cbBones, 6400 bytes) per enemy
//    GPU тепер:   CPU шле тільки { animTime, animIdx } = 8 bytes per enemy
//                 Compute Shader читає запаковані keyframes, інтерполює,
//                 пише готові bone matrices в RWStructuredBuffer
//                 Vertex Shader читає з StructuredBuffer<matrix> (не cbuffer)
//
//  Підключення:
//    1. #include "GPUSkinning.h" в Renderer.h (після ComPtr using)
//    2. buildGpuSkinModel() — викликати після uploadMeshesToDX11() для кожної моделі
//    3. buildGpuSkinInstance() — викликати для кожного Enemy (або пул)
//    4. dispatchSkin() — викликати перед drawEnemiesDX11()
//    5. В VS замість cbuffer Bones — StructuredBuffer<float4x4> bones : register(t1)
// ============================================================

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <unordered_map>

// ──────────────────────────────────────────────────────────────
//  GPU-side keyframe structs  (16-byte aligned)
// ──────────────────────────────────────────────────────────────
struct GK_Pos { float t, x, y, z; };                         // 16 bytes
struct GK_Rot { float t, x, y, z, w, _p0, _p1, _p2; };      // 32 bytes
struct GK_Scl { float t, x, y, z; };                         // 16 bytes

// One entry per node in the flat traversal order
struct GK_Node {
    float  localMat[16]; // bind-pose local transform (used when no channel)
    int    parentIdx;    // -1 = root
    int    boneId;       // -1 = not a skinning bone
    int    _pad0, _pad1;
};

// Per-animation, per-channel descriptor
// chanIdx stored in a separate per-anim map so we rebuild per anim switch
struct GK_Chan {
    int posOff, posN;
    int rotOff, rotN;
    int sclOff, sclN;
    int nodeIdx; // which GK_Node this drives
    int _pad;
};

// Small cbuffer sent per-dispatch (one per enemy per frame)
struct GK_CB {
    int   chanOffset; // offset into chanBuf for this animation
    int   chanCount;
    float animTime;
    int   nodeCount;
    int   boneCount;
    int   _p0, _p1, _p2;
};

// ──────────────────────────────────────────────────────────────
//  HLSL Compute Shader source
// ──────────────────────────────────────────────────────────────
static const char* HLSL_SKIN_CS = R"(
struct GK_Node { float4x4 localMat; int parentIdx; int boneId; int _p0; int _p1; };
struct GK_Chan { int posOff,posN; int rotOff,rotN; int sclOff,sclN; int nodeIdx; int _p; };
struct GK_Pos  { float t,x,y,z; };
struct GK_Rot  { float t,x,y,z,w; float3 _p; };
struct GK_Scl  { float t,x,y,z; };

cbuffer CB : register(b3) {
    int   chanOffset;
    int   chanCount;
    float animTime;
    int   nodeCount;
    int   boneCount;
    int _p0,_p1,_p2;
};

StructuredBuffer<GK_Node>   gNodes   : register(t4);
StructuredBuffer<GK_Chan>   gChans   : register(t5);
StructuredBuffer<GK_Pos>    gPosKeys : register(t6);
StructuredBuffer<GK_Rot>    gRotKeys : register(t7);
StructuredBuffer<GK_Scl>    gSclKeys : register(t8);
StructuredBuffer<float4x4>  gOffset  : register(t9);  // bone offset matrices

RWStructuredBuffer<float4x4> gGlobal : register(u0);  // temp global transforms
RWStructuredBuffer<float4x4> gBones  : register(u1);  // output bone matrices (read by VS)

// Build channel lookup: nodeIdx -> chanIdx in [chanOffset, chanOffset+chanCount)
// Using groupshared for nodes processed by this group
groupshared float4x4 gs_global[200]; // shared mem — max 200 nodes

float4 qmul(float4 a, float4 b) {
    return float4(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                  a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                  a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                  a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);
}
float4x4 q2m(float4 q) {
    q = normalize(q);
    float x=q.x,y=q.y,z=q.z,w=q.w;
    return float4x4(1-2*(y*y+z*z), 2*(x*y+z*w),   2*(x*z-y*w),   0,
                    2*(x*y-z*w),   1-2*(x*x+z*z), 2*(y*z+x*w),   0,
                    2*(x*z+y*w),   2*(y*z-x*w),   1-2*(x*x+y*y), 0,
                    0,             0,             0,             1);
}
float4x4 trs(float3 t, float4 r, float3 s) {
    float4x4 R=q2m(r);
    return float4x4(R[0]*s.x, R[1]*s.y, R[2]*s.z, float4(t,1));
}

int findPos(int off, int n, float t) {
    if(n<=1) return off;
    int lo=off, hi=off+n-2;
    while(lo<hi){int m=(lo+hi)/2; if(gPosKeys[m].t<t) lo=m+1; else hi=m;}
    return lo;
}
int findRot(int off, int n, float t) {
    if(n<=1) return off;
    int lo=off, hi=off+n-2;
    while(lo<hi){int m=(lo+hi)/2; if(gRotKeys[m].t<t) lo=m+1; else hi=m;}
    return lo;
}
int findScl(int off, int n, float t) {
    if(n<=1) return off;
    int lo=off, hi=off+n-2;
    while(lo<hi){int m=(lo+hi)/2; if(gSclKeys[m].t<t) lo=m+1; else hi=m;}
    return lo;
}

// Process ONE node — called sequentially in topological order
float4x4 evalNode(int ni, float t) {
    GK_Node nd = gNodes[ni];

    // Find channel for this node
    float4x4 nodeT = nd.localMat;
    for(int ci = chanOffset; ci < chanOffset + chanCount; ci++) {
        if(gChans[ci].nodeIdx != ni) continue;
        GK_Chan ch = gChans[ci];

        // Position
        float3 pos = float3(0,0,0);
        if(ch.posN >= 1) {
            int i = findPos(ch.posOff, ch.posN, t);
            int i1 = min(i+1, ch.posOff+ch.posN-1);
            float dt = gPosKeys[i1].t - gPosKeys[i].t;
            float f = dt < 1e-5 ? 0 : saturate((t - gPosKeys[i].t)/dt);
            float3 p0 = float3(gPosKeys[i].x,  gPosKeys[i].y,  gPosKeys[i].z);
            float3 p1 = float3(gPosKeys[i1].x, gPosKeys[i1].y, gPosKeys[i1].z);
            pos = lerp(p0, p1, f);
        }
        // Rotation
        float4 rot = float4(0,0,0,1);
        if(ch.rotN >= 1) {
            int i = findRot(ch.rotOff, ch.rotN, t);
            int i1 = min(i+1, ch.rotOff+ch.rotN-1);
            float dt = gRotKeys[i1].t - gRotKeys[i].t;
            float f = dt < 1e-5 ? 0 : saturate((t - gRotKeys[i].t)/dt);
            float4 q0 = float4(gRotKeys[i].x,  gRotKeys[i].y,  gRotKeys[i].z,  gRotKeys[i].w);
            float4 q1 = float4(gRotKeys[i1].x, gRotKeys[i1].y, gRotKeys[i1].z, gRotKeys[i1].w);
            if(dot(q0,q1) < 0) q1 = -q1;
            rot = normalize(lerp(q0, q1, f));
        }
        // Scale
        float3 scl = float3(1,1,1);
        if(ch.sclN >= 1) {
            int i = findScl(ch.sclOff, ch.sclN, t);
            int i1 = min(i+1, ch.sclOff+ch.sclN-1);
            float dt = gSclKeys[i1].t - gSclKeys[i].t;
            float f = dt < 1e-5 ? 0 : saturate((t - gSclKeys[i].t)/dt);
            float3 s0 = float3(gSclKeys[i].x,  gSclKeys[i].y,  gSclKeys[i].z);
            float3 s1 = float3(gSclKeys[i1].x, gSclKeys[i1].y, gSclKeys[i1].z);
            scl = lerp(s0, s1, f);
        }
        nodeT = trs(pos, rot, scl);
        break;
    }

    // Multiply by parent
    float4x4 parent = (float4x4)1;
    if(nd.parentIdx >= 0) parent = gs_global[nd.parentIdx];
    return mul(parent, nodeT);
}

// Single-thread dispatch: one thread processes ALL nodes sequentially
// (nodes are in topological order so parent is always ready)
[numthreads(1,1,1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    for(int ni = 0; ni < nodeCount; ni++) {
        float4x4 g = evalNode(ni, animTime);
        gs_global[ni] = g;
        gGlobal[ni]   = g;

        int boneId = gNodes[ni].boneId;
        if(boneId >= 0 && boneId < boneCount) {
            gBones[boneId] = mul(g, gOffset[boneId]);
        }
    }
}
)";

// ──────────────────────────────────────────────────────────────
//  Per-model GPU data  (uploaded once at load time)
// ──────────────────────────────────────────────────────────────
struct GpuSkinModel {
    ComPtr<ID3D11Buffer>             nodeBuf;
    ComPtr<ID3D11ShaderResourceView> nodeSRV;
    ComPtr<ID3D11Buffer>             chanBuf;   // all anims concatenated
    ComPtr<ID3D11ShaderResourceView> chanSRV;
    ComPtr<ID3D11Buffer>             posBuf;
    ComPtr<ID3D11ShaderResourceView> posSRV;
    ComPtr<ID3D11Buffer>             rotBuf;
    ComPtr<ID3D11ShaderResourceView> rotSRV;
    ComPtr<ID3D11Buffer>             sclBuf;
    ComPtr<ID3D11ShaderResourceView> sclSRV;
    ComPtr<ID3D11Buffer>             offBuf;    // bone offset matrices
    ComPtr<ID3D11ShaderResourceView> offSRV;

    // Per-animation descriptors (CPU-side only)
    std::vector<int> animChanOff;  // chanBuf offset per anim
    std::vector<int> animChanCnt;  // channel count per anim
    int nodeCount = 0;
    int boneCount = 0;
    bool ready = false;
};

// Per-enemy instance buffers
struct GpuSkinInstance {
    ComPtr<ID3D11Buffer>              globalBuf; // temp global matrices
    ComPtr<ID3D11UnorderedAccessView> globalUAV;
    ComPtr<ID3D11Buffer>              boneBuf;
    ComPtr<ID3D11UnorderedAccessView> boneUAV;
    ComPtr<ID3D11ShaderResourceView>  boneSRV;   // bound to VS t1
    bool ready = false;
};

// ──────────────────────────────────────────────────────────────
//  Helper: create immutable StructuredBuffer + SRV
// ──────────────────────────────────────────────────────────────
static bool _gsMkSB(ID3D11Device* dev, const void* data,
    UINT elemSz, UINT count,
    ID3D11Buffer** buf, ID3D11ShaderResourceView** srv)
{
    if (!count) return true; // empty is ok
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = elemSz * count;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = elemSz;
    D3D11_SUBRESOURCE_DATA sd = { data };
    if (FAILED(dev->CreateBuffer(&bd, &sd, buf))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
    svd.Format = DXGI_FORMAT_UNKNOWN;
    svd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    svd.Buffer.NumElements = count;
    return SUCCEEDED(dev->CreateShaderResourceView(*buf, &svd, srv));
}

// Helper: create RW StructuredBuffer + UAV + optional SRV
static bool _gsMkRW(ID3D11Device* dev, UINT elemSz, UINT count,
    ID3D11Buffer** buf, ID3D11UnorderedAccessView** uav,
    ID3D11ShaderResourceView** srv = nullptr)
{
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = elemSz * count;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = elemSz;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, buf))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = count;
    if (FAILED(dev->CreateUnorderedAccessView(*buf, &ud, uav))) return false;
    if (srv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
        svd.Format = DXGI_FORMAT_UNKNOWN;
        svd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        svd.Buffer.NumElements = count;
        dev->CreateShaderResourceView(*buf, &svd, srv);
    }
    return true;
}

// ──────────────────────────────────────────────────────────────
//  buildGpuSkinModel()
//  Call after model is loaded (aiScene still alive)
//  flatNodes = Enemy::flatNodes built from this model's aiScene
// ──────────────────────────────────────────────────────────────
// AnimatedModel.h is included by the caller (Enemy.h/main.cpp)
// Forward-declare only what buildGpuSkinModel needs
struct GsFlatNode {
    struct aiNode* node;
    int parentIdx;
    std::string name;
    int boneId;
};

inline bool buildGpuSkinModel(ID3D11Device* dev,
    const std::vector<GsFlatNode>& flatNodes,
    const AnimatedModel& proto,
    GpuSkinModel& out)
{
    out = {};
    if (!proto.scene || flatNodes.empty()) return false;

    out.nodeCount = (int)flatNodes.size();
    out.boneCount = proto.boneCount;

    // name -> node index map
    std::unordered_map<std::string, int> nodeByName;
    for (int i = 0; i < (int)flatNodes.size(); i++)
        nodeByName[flatNodes[i].name] = i;

    // ── 1. GK_Node array ────────────────────────────────────────
    std::vector<GK_Node> gpuNodes(flatNodes.size());
    for (int i = 0; i < (int)flatNodes.size(); i++) {
        const auto& fn = flatNodes[i];
        // Convert aiMatrix4x4 to column-major float[16]
        auto& am = fn.node->mTransformation;
        float m[16] = {
            am.a1, am.b1, am.c1, am.d1,
            am.a2, am.b2, am.c2, am.d2,
            am.a3, am.b3, am.c3, am.d3,
            am.a4, am.b4, am.c4, am.d4,
        };
        memcpy(gpuNodes[i].localMat, m, 64);
        gpuNodes[i].parentIdx = fn.parentIdx;
        gpuNodes[i].boneId = fn.boneId;
        gpuNodes[i]._pad0 = gpuNodes[i]._pad1 = 0;
    }

    // ── 2. Pack keyframes for all animations ─────────────────────
    std::vector<GK_Pos> allPos;
    std::vector<GK_Rot> allRot;
    std::vector<GK_Scl> allScl;
    std::vector<GK_Chan> allChans;

    int numAnims = (int)proto.scene->mNumAnimations;
    out.animChanOff.resize(numAnims, 0);
    out.animChanCnt.resize(numAnims, 0);

    for (int ai = 0; ai < numAnims; ai++) {
        const aiAnimation* anim = proto.scene->mAnimations[ai];
        out.animChanOff[ai] = (int)allChans.size();
        for (unsigned ci = 0; ci < anim->mNumChannels; ci++) {
            const aiNodeAnim* ch = anim->mChannels[ci];
            auto nit = nodeByName.find(ch->mNodeName.C_Str());
            if (nit == nodeByName.end()) continue;

            GK_Chan gc;
            gc.nodeIdx = nit->second;
            gc.posOff = (int)allPos.size(); gc.posN = (int)ch->mNumPositionKeys;
            gc.rotOff = (int)allRot.size(); gc.rotN = (int)ch->mNumRotationKeys;
            gc.sclOff = (int)allScl.size(); gc.sclN = (int)ch->mNumScalingKeys;
            gc._pad = 0;

            for (unsigned k = 0; k < ch->mNumPositionKeys; k++) {
                auto& v = ch->mPositionKeys[k];
                allPos.push_back({ (float)v.mTime, v.mValue.x, v.mValue.y, v.mValue.z });
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; k++) {
                auto& v = ch->mRotationKeys[k];
                GK_Rot r; r.t = float(v.mTime); r.x = v.mValue.x; r.y = v.mValue.y;
                r.z = v.mValue.z; r.w = v.mValue.w; r._p0 = r._p1 = r._p2 = 0;
                allRot.push_back(r);
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; k++) {
                auto& v = ch->mScalingKeys[k];
                allScl.push_back({ (float)v.mTime, v.mValue.x, v.mValue.y, v.mValue.z });
            }
            allChans.push_back(gc);
        }
        out.animChanCnt[ai] = (int)allChans.size() - out.animChanOff[ai];
    }

    // ── 3. Bone offset matrices ───────────────────────────────────
    std::vector<glm::mat4> offsets(proto.boneCount, glm::mat4(1.f));
    for (auto& [name, bi] : proto.boneMap)
        if (bi.id < proto.boneCount) offsets[bi.id] = bi.offset;

    // ── 4. Upload to GPU ──────────────────────────────────────────
    bool ok = true;
    ok &= _gsMkSB(dev, gpuNodes.data(), sizeof(GK_Node), (UINT)gpuNodes.size(),
        out.nodeBuf.GetAddressOf(), out.nodeSRV.GetAddressOf());
    if (!allChans.empty())
        ok &= _gsMkSB(dev, allChans.data(), sizeof(GK_Chan), (UINT)allChans.size(),
            out.chanBuf.GetAddressOf(), out.chanSRV.GetAddressOf());
    if (!allPos.empty())
        ok &= _gsMkSB(dev, allPos.data(), sizeof(GK_Pos), (UINT)allPos.size(),
            out.posBuf.GetAddressOf(), out.posSRV.GetAddressOf());
    if (!allRot.empty())
        ok &= _gsMkSB(dev, allRot.data(), sizeof(GK_Rot), (UINT)allRot.size(),
            out.rotBuf.GetAddressOf(), out.rotSRV.GetAddressOf());
    if (!allScl.empty())
        ok &= _gsMkSB(dev, allScl.data(), sizeof(GK_Scl), (UINT)allScl.size(),
            out.sclBuf.GetAddressOf(), out.sclSRV.GetAddressOf());
    if (!offsets.empty())
        ok &= _gsMkSB(dev, offsets.data(), sizeof(glm::mat4), (UINT)offsets.size(),
            out.offBuf.GetAddressOf(), out.offSRV.GetAddressOf());

    if (ok) {
        out.ready = true;
        printf("[GPUSKIN] Model ready: %d nodes, %d bones, %d anims, "
            "%zu pos / %zu rot / %zu scl keys\n",
            out.nodeCount, out.boneCount, numAnims,
            allPos.size(), allRot.size(), allScl.size());
    }
    else {
        printf("[GPUSKIN] Upload FAILED\n");
    }
    return ok;
}

// ──────────────────────────────────────────────────────────────
//  buildGpuSkinInstance()
//  Call once per enemy (or reuse from pool)
// ──────────────────────────────────────────────────────────────
inline bool buildGpuSkinInstance(ID3D11Device* dev,
    int nodeCount, int boneCount, GpuSkinInstance& out)
{
    out = {};
    bool ok = true;
    ok &= _gsMkRW(dev, sizeof(glm::mat4), nodeCount,
        out.globalBuf.GetAddressOf(), out.globalUAV.GetAddressOf());
    ok &= _gsMkRW(dev, sizeof(glm::mat4), boneCount,
        out.boneBuf.GetAddressOf(), out.boneUAV.GetAddressOf(),
        out.boneSRV.GetAddressOf());
    out.ready = ok;
    return ok;
}

#endif // _WIN32