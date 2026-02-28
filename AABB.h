#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <limits>
#include <iostream>

#include "Triangle.h"

struct AABB {
    glm::vec3 min = glm::vec3(1e30f), max = glm::vec3(-1e30f);

    bool intersectsRay(const glm::vec3& o, const glm::vec3& d, float& tMin) const {
        glm::vec3 inv=1.f/d, t0=(min-o)*inv, t1=(max-o)*inv;
        glm::vec3 lo=glm::min(t0,t1), hi=glm::max(t0,t1);
        float tn=glm::max(glm::max(lo.x,lo.y),lo.z);
        float tf=glm::min(glm::min(hi.x,hi.y),hi.z);
        tMin=tn; return tf>=tn&&tf>0.f;
    }
    void expand(const glm::vec3& p){ min=glm::min(min,p); max=glm::max(max,p); }
    glm::vec3 center()const{ return (min+max)*.5f; }
};

struct BVHNode {
    AABB box;
    int left=-1,right=-1,triStart=0,triCount=0;
    bool isLeaf()const{return left==-1;}
};

struct BVHTree {
    std::vector<BVHNode> nodes;
    std::vector<int>     triIndices;
    const std::vector<Triangle>* tris=nullptr;
    glm::vec3 worldMin{1e30f}, worldMax{-1e30f};

    void build(const std::vector<Triangle>& triangles) {
        tris=&triangles;
        if (triangles.empty()) return;
        for (auto& t:triangles) {
            worldMin=glm::min(worldMin,glm::min(t.a,glm::min(t.b,t.c)));
            worldMax=glm::max(worldMax,glm::max(t.a,glm::max(t.b,t.c)));
        }
        std::cout<<"[BVH] min:("<<worldMin.x<<","<<worldMin.y<<","<<worldMin.z<<")"
                 <<" max:("<<worldMax.x<<","<<worldMax.y<<","<<worldMax.z<<")\n";
        triIndices.resize(triangles.size());
        for (int i=0;i<(int)triangles.size();i++) triIndices[i]=i;
        nodes.reserve(triangles.size()*2);
        buildNode(0,(int)triIndices.size());
        std::cout<<"[BVH] "<<triangles.size()<<" tris -> "<<nodes.size()<<" nodes\n";
    }

    bool raycast(const glm::vec3& o,const glm::vec3& d,float maxD,float& hit)const {
        if (nodes.empty()) return false;
        hit=maxD; bool h=false;
        raycastNode(0,o,d,hit,h); return h;
    }

private:
    AABB calcBox(int s,int c)const {
        AABB b; b.min=glm::vec3(1e30f); b.max=glm::vec3(-1e30f);
        for (int i=s;i<s+c;i++){auto& t=(*tris)[triIndices[i]];b.expand(t.a);b.expand(t.b);b.expand(t.c);}
        return b;
    }
    int buildNode(int s,int c) {
        int idx=(int)nodes.size(); nodes.push_back({});
        nodes[idx].box=calcBox(s,c); nodes[idx].triStart=s; nodes[idx].triCount=c;
        if (c<=8) return idx;
        glm::vec3 sz=nodes[idx].box.max-nodes[idx].box.min;
        int axis=0; if(sz.y>sz.x)axis=1; if(sz.z>sz[axis])axis=2;
        float mid=nodes[idx].box.center()[axis];
        int sp=s;
        for (int i=s;i<s+c;i++){
            auto& t=(*tris)[triIndices[i]];
            if((t.a[axis]+t.b[axis]+t.c[axis])/3.f<mid) std::swap(triIndices[i],triIndices[sp++]);
        }
        if(sp==s||sp==s+c) sp=s+c/2;
        nodes[idx].triCount=0;
        int L=buildNode(s,sp-s); int R=buildNode(sp,c-(sp-s));
        nodes[idx].left=L; nodes[idx].right=R;
        return idx;
    }
    bool rayTri(const Triangle& t,const glm::vec3& o,const glm::vec3& d,float& hit)const {
        const float E=1e-7f;
        glm::vec3 e1=t.b-t.a,e2=t.c-t.a,h=glm::cross(d,e2);
        float a=glm::dot(e1,h); if(a>-E&&a<E)return false;
        float f=1.f/a; glm::vec3 s=o-t.a;
        float u=f*glm::dot(s,h); if(u<0||u>1)return false;
        glm::vec3 q=glm::cross(s,e1);
        float v=f*glm::dot(d,q); if(v<0||u+v>1)return false;
        hit=f*glm::dot(e2,q); return hit>E;
    }
    void raycastNode(int idx,const glm::vec3& o,const glm::vec3& d,float& best,bool& hit)const {
        const BVHNode& n=nodes[idx]; float bt;
        if(!n.box.intersectsRay(o,d,bt)||bt>best) return;
        if(n.isLeaf()){
            for(int i=n.triStart;i<n.triStart+n.triCount;i++){
                float t; if(rayTri((*tris)[triIndices[i]],o,d,t)&&t<best){best=t;hit=true;}
            }
        } else { raycastNode(n.left,o,d,best,hit); raycastNode(n.right,o,d,best,hit); }
    }
};

inline BVHTree bvh;

inline float getGroundY(const glm::vec3& p, float dist=50.f) {
    if(bvh.nodes.empty()) return std::numeric_limits<float>::lowest();
    glm::vec3 o=p+glm::vec3(0,1.f,0); float h;
    if(bvh.raycast(o,{0,-1,0},dist+1.f,h)) return o.y-h;
    return std::numeric_limits<float>::lowest();
}

inline void wallCollide(glm::vec3& p,float r=0.35f) {
    if(bvh.nodes.empty()) return;
    glm::vec3 dirs[]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
    float heights[]={0.2f,0.9f,1.5f};
    for(float hy:heights){
        glm::vec3 o=p+glm::vec3(0,hy,0);
        for(auto& d:dirs){float h; if(bvh.raycast(o,d,r,h)) p-=d*(r-h);}
    }
}

inline bool shootRay(const glm::vec3& o,const glm::vec3& d,glm::vec3& hitPos) {
    if(bvh.nodes.empty()) return false;
    float h=1000.f;
    if(bvh.raycast(o,d,1000.f,h)){hitPos=o+d*h;return true;}
    return false;
}
