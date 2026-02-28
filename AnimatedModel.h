#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

// ──────────────────────────────────────────────
//  Helpers
// ──────────────────────────────────────────────
inline glm::mat4 ai2glm(const aiMatrix4x4& m){
    return glm::transpose(glm::make_mat4(&m.a1));
}
inline glm::vec3 ai2vec(const aiVector3D& v){ return {v.x,v.y,v.z}; }
inline glm::quat ai2quat(const aiQuaternion& q){ return {q.w,q.x,q.y,q.z}; }

// ──────────────────────────────────────────────
//  GPUMesh
// ──────────────────────────────────────────────
struct GPUMesh {
    unsigned int VAO=0,VBO=0,EBO=0;
    unsigned int indexCount=0;
    unsigned int texID=0;
    bool         skinned=false;
};

// ──────────────────────────────────────────────
//  BoneInfo
// ──────────────────────────────────────────────
struct BoneInfo { int id; glm::mat4 offset; };

// ══════════════════════════════════════════════
//  AnimatedModel
// ══════════════════════════════════════════════
struct AnimatedModel
{
    static const int MAX_BONES = 100;

    std::vector<GPUMesh>                       meshes;
    std::unordered_map<std::string, BoneInfo>  boneMap;
    int                                        boneCount = 0;
    glm::mat4                                  globalInvT{1.f};

    std::map<std::string,int> animIndex;

    std::string curAnim  = "";
    std::string nextAnim = "";
    float animTime  = 0.f;
    float animSpeed = 1.f;
    bool  looping   = true;
    bool  animDone  = false;

    std::vector<glm::mat4> boneFinal;

    Assimp::Importer importer;
    const aiScene*   scene = nullptr;

    // ── Texture loader (set before calling load) ──
    // We call back into ModelLoader's loadTexture via function pointer
    std::function<unsigned int(const std::string&)> texLoader;

    // ── API ──
    bool load(const std::string& path, const std::string& texDir);

    void play(const std::string& name, bool loop=true, float speed=1.f)
    {
        if (!animIndex.count(name)){std::cerr<<"[ANIM] Not found:"<<name<<"\n";return;}
        if (curAnim==name) return;
        curAnim=name; animTime=0.f; looping=loop; animSpeed=speed; animDone=false;
    }

    void playOnce(const std::string& name, const std::string& ret, float speed=1.f)
    {
        if (!animIndex.count(name)){std::cerr<<"[ANIM] Not found:"<<name<<"\n";return;}
        nextAnim=ret; looping=false; animSpeed=speed;
        curAnim=name; animTime=0.f; animDone=false;
    }

    void update(float dt);
    bool isDone()  const { return animDone; }
    bool hasAnim(const std::string& n) const { return animIndex.count(n)>0; }

private:
    void loadNode(aiNode* node, const std::string& texDir);
    void loadMesh(aiMesh* am,   const std::string& texDir);

    glm::vec3 lerpPos  (const aiNodeAnim* ch, double t);
    glm::quat slerpRot (const aiNodeAnim* ch, double t);
    glm::vec3 lerpScale(const aiNodeAnim* ch, double t);

    const aiNodeAnim* findCh(const aiAnimation* a, const std::string& name){
        for (unsigned int i=0;i<a->mNumChannels;i++)
            if (std::string(a->mChannels[i]->mNodeName.C_Str())==name)
                return a->mChannels[i];
        return nullptr;
    }

    void calcBones(const aiAnimation* anim, double t,
                   aiNode* node, const glm::mat4& parent);
};

// ──────────────────────────────────────────────
//  Implementations
// ──────────────────────────────────────────────
#include <functional>

inline bool AnimatedModel::load(const std::string& path, const std::string& texDir)
{
    scene=importer.ReadFile(path,
        aiProcess_Triangulate|aiProcess_GenNormals|
        aiProcess_LimitBoneWeights|aiProcess_OptimizeMeshes);
    if(!scene||!scene->mRootNode){
        std::cerr<<"[ANIM] Cannot load:"<<path<<"\n"; return false;
    }
    globalInvT=glm::inverse(ai2glm(scene->mRootNode->mTransformation));
    boneFinal.assign(MAX_BONES,glm::mat4(1.f));
    for(unsigned int i=0;i<scene->mNumAnimations;i++){
        std::string n=scene->mAnimations[i]->mName.C_Str();
        animIndex[n]=(int)i;
        std::cout<<"[ANIM] "<<n<<"\n";
    }
    loadNode(scene->mRootNode,texDir);
    std::cout<<"[ANIM] Loaded:"<<path<<" meshes:"<<meshes.size()
             <<" bones:"<<boneCount<<" anims:"<<animIndex.size()<<"\n";
    return true;
}

inline void AnimatedModel::update(float dt)
{
    if(curAnim.empty()||!scene) return;
    auto it=animIndex.find(curAnim);
    if(it==animIndex.end()) return;
    const aiAnimation* anim=scene->mAnimations[it->second];
    double tps=anim->mTicksPerSecond>0?anim->mTicksPerSecond:25.0;
    double dur=anim->mDuration;
    animTime+=dt*animSpeed*(float)tps;
    if(animTime>=(float)dur){
        if(looping){ animTime=fmodf(animTime,(float)dur); }
        else{
            animTime=(float)dur-0.001f; animDone=true;
            if(!nextAnim.empty()){std::string r=nextAnim;nextAnim="";play(r,true);return;}
        }
    }
    calcBones(anim,(double)animTime,scene->mRootNode,glm::mat4(1.f));
}

inline void AnimatedModel::loadNode(aiNode* node, const std::string& texDir)
{
    for(unsigned int i=0;i<node->mNumMeshes;i++)
        loadMesh(scene->mMeshes[node->mMeshes[i]],texDir);
    for(unsigned int i=0;i<node->mNumChildren;i++)
        loadNode(node->mChildren[i],texDir);
}

inline void AnimatedModel::loadMesh(aiMesh* am, const std::string& texDir)
{
    std::vector<float>        verts;
    std::vector<unsigned int> idx;
    std::vector<glm::ivec4>   bIDs(am->mNumVertices,glm::ivec4(-1));
    std::vector<glm::vec4>    bWts(am->mNumVertices,glm::vec4(0.f));

    if(am->HasBones()){
        for(unsigned int b=0;b<am->mNumBones;b++){
            aiBone* bone=am->mBones[b];
            std::string bn=bone->mName.C_Str();
            int bid;
            if(!boneMap.count(bn)){bid=boneCount++;boneMap[bn]={bid,ai2glm(bone->mOffsetMatrix)};}
            else bid=boneMap[bn].id;
            for(unsigned int w=0;w<bone->mNumWeights;w++){
                unsigned int vid=bone->mWeights[w].mVertexId;
                float wt=bone->mWeights[w].mWeight;
                for(int s=0;s<4;s++) if(bIDs[vid][s]<0){bIDs[vid][s]=bid;bWts[vid][s]=wt;break;}
            }
        }
    }

    for(unsigned int i=0;i<am->mNumVertices;i++){
        verts.push_back(am->mVertices[i].x);
        verts.push_back(am->mVertices[i].y);
        verts.push_back(am->mVertices[i].z);
        if(am->HasNormals()){verts.push_back(am->mNormals[i].x);verts.push_back(am->mNormals[i].y);verts.push_back(am->mNormals[i].z);}
        else{verts.push_back(0);verts.push_back(1);verts.push_back(0);}
        if(am->HasTextureCoords(0)){verts.push_back(am->mTextureCoords[0][i].x);verts.push_back(am->mTextureCoords[0][i].y);}
        else{verts.push_back(0);verts.push_back(0);}
        verts.push_back((float)bIDs[i][0]);verts.push_back((float)bIDs[i][1]);
        verts.push_back((float)bIDs[i][2]);verts.push_back((float)bIDs[i][3]);
        verts.push_back(bWts[i][0]);verts.push_back(bWts[i][1]);
        verts.push_back(bWts[i][2]);verts.push_back(bWts[i][3]);
    }
    for(unsigned int i=0;i<am->mNumFaces;i++)
        for(unsigned int j=0;j<am->mFaces[i].mNumIndices;j++)
            idx.push_back(am->mFaces[i].mIndices[j]);

    // Texture — use texLoader callback if set, else skip
    unsigned int texID=0;
    if(texLoader && am->mMaterialIndex<scene->mNumMaterials){
        aiMaterial* mat=scene->mMaterials[am->mMaterialIndex];
        aiString tp;
        if(mat->GetTexture(aiTextureType_DIFFUSE,0,&tp)==AI_SUCCESS){
            texID=texLoader(texDir+"/"+tp.C_Str());
            if(!texID){
                std::string n=tp.C_Str();
                size_t sl=n.find_last_of("/\\");
                if(sl!=std::string::npos) n=n.substr(sl+1);
                texID=texLoader(texDir+"/"+n);
            }
        }
    }

    GPUMesh mesh;
    mesh.indexCount=(unsigned int)idx.size();
    mesh.texID=texID; mesh.skinned=am->HasBones();
    const int STR=16*sizeof(float);
    glGenVertexArrays(1,&mesh.VAO);glGenBuffers(1,&mesh.VBO);glGenBuffers(1,&mesh.EBO);
    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER,mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(float),verts.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(unsigned int),idx.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,STR,(void*)0);                  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,STR,(void*)(3*sizeof(float)));  glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,STR,(void*)(6*sizeof(float)));  glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,STR,(void*)(8*sizeof(float)));  glEnableVertexAttribArray(3);
    glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,STR,(void*)(12*sizeof(float))); glEnableVertexAttribArray(4);
    glBindVertexArray(0);
    meshes.push_back(mesh);
}

inline glm::vec3 AnimatedModel::lerpPos(const aiNodeAnim* ch, double t){
    if(ch->mNumPositionKeys==1) return ai2vec(ch->mPositionKeys[0].mValue);
    for(unsigned int i=0;i<ch->mNumPositionKeys-1;i++)
        if(t<ch->mPositionKeys[i+1].mTime){
            float f=(float)((t-ch->mPositionKeys[i].mTime)/
                    (ch->mPositionKeys[i+1].mTime-ch->mPositionKeys[i].mTime));
            return glm::mix(ai2vec(ch->mPositionKeys[i].mValue),ai2vec(ch->mPositionKeys[i+1].mValue),f);
        }
    return ai2vec(ch->mPositionKeys[ch->mNumPositionKeys-1].mValue);
}
inline glm::quat AnimatedModel::slerpRot(const aiNodeAnim* ch, double t){
    if(ch->mNumRotationKeys==1) return ai2quat(ch->mRotationKeys[0].mValue);
    for(unsigned int i=0;i<ch->mNumRotationKeys-1;i++)
        if(t<ch->mRotationKeys[i+1].mTime){
            float f=(float)((t-ch->mRotationKeys[i].mTime)/
                    (ch->mRotationKeys[i+1].mTime-ch->mRotationKeys[i].mTime));
            return glm::normalize(glm::slerp(ai2quat(ch->mRotationKeys[i].mValue),
                                             ai2quat(ch->mRotationKeys[i+1].mValue),f));
        }
    return ai2quat(ch->mRotationKeys[ch->mNumRotationKeys-1].mValue);
}
inline glm::vec3 AnimatedModel::lerpScale(const aiNodeAnim* ch, double t){
    if(ch->mNumScalingKeys==1) return ai2vec(ch->mScalingKeys[0].mValue);
    for(unsigned int i=0;i<ch->mNumScalingKeys-1;i++)
        if(t<ch->mScalingKeys[i+1].mTime){
            float f=(float)((t-ch->mScalingKeys[i].mTime)/
                    (ch->mScalingKeys[i+1].mTime-ch->mScalingKeys[i].mTime));
            return glm::mix(ai2vec(ch->mScalingKeys[i].mValue),ai2vec(ch->mScalingKeys[i+1].mValue),f);
        }
    return ai2vec(ch->mScalingKeys[ch->mNumScalingKeys-1].mValue);
}

inline void AnimatedModel::calcBones(const aiAnimation* anim,double t,
                                      aiNode* node,const glm::mat4& parent)
{
    std::string name=node->mName.C_Str();
    glm::mat4 nodeT=ai2glm(node->mTransformation);
    const aiNodeAnim* ch=findCh(anim,name);
    if(ch){
        glm::mat4 T=glm::translate(glm::mat4(1.f),lerpPos(ch,t));
        glm::mat4 R=glm::mat4_cast(slerpRot(ch,t));
        glm::mat4 S=glm::scale(glm::mat4(1.f),lerpScale(ch,t));
        nodeT=T*R*S;
    }
    glm::mat4 global=parent*nodeT;
    auto it=boneMap.find(name);
    if(it!=boneMap.end()&&it->second.id<MAX_BONES)
        boneFinal[it->second.id]=globalInvT*global*it->second.offset;
    for(unsigned int i=0;i<node->mNumChildren;i++)
        calcBones(anim,t,node->mChildren[i],global);
}
