#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_animation.h"

// FaceFX (OC3 Entertainment FaceFX SDK 1.7, archive version 1720, linked in
// the readable exe at 0x6e3000-0x70a000) and the Eden glue that drives the
// facial bones of the avatars (FUN_00b77b20). docs/re/facefx_spec.md.
//
// - Actors: avatar/avatar_face_fx.bnk "female.fxa" / "male.fxa" (FxActor:
//   master bone list = the 42 facial bones of the avatar skeleton with their
//   reference pose and the bone poses of the face graph nodes, compiled face
//   graph = 445 / 448 nodes with link functions).
// - Animations: the cinematic banks "*.fxe" (FxAnimSet: one FxAnimGroup of
//   FxAnim = curves keyed on face graph node names: phonemes "open", "W",
//   "PBM"..., action units "AU25_M_Lips_Part"..., "Blink", "Gaze Eye Yaw").
// - Cinematics: the .sce actor track "Morph" (0x70726fb5) starts a FaceFX
//   animation (REFS 0x0306 "<path>\female.fxa::<group>:<anim>").

class FaceFxActor {
public:
    // FaceFX quaternions are stored (w, x, y, z) in the 3ds Max space of the
    // actor (centimetres); FUN_00b77b20 converts to the Eden skeleton.
    struct Transform {
        float Position[3] = {};
        float Rotation[4] = { 1.0f, 0.0f, 0.0f, 0.0f }; // w x y z
        float Scale[3] = { 1.0f, 1.0f, 1.0f };
    };
    struct BonePose {
        uint32_t Node = 0; // compiled face graph node (FxBonePoseNode)
        Transform Pose;    // position / scale: deltas, rotation: absolute
    };
    struct Bone {
        std::string Name;
        uint32_t NameHash4 = 0; // EdenNameHash4: skeleton bone lookup (FUN_007cfd10)
        Transform Reference;
        float InverseRotation[4] = { 1.0f, 0.0f, 0.0f, 0.0f }; // FxMasterBoneList +0x30
        std::vector<BonePose> Poses;
    };
    struct Link {
        uint32_t Input = 0;
        uint32_t Function = 0; // FUN_006e9b80 switch: 0 null, 1 linear ... 10 clamped linear
        std::vector<float> Parameters;
    };
    // FxCompiledFaceGraph node (0x48 bytes): +0 type, +4 name, +8 min,
    // +0xc 1/min, +0x10 max, +0x14 1/max, +0x24 input operation, inputs.
    struct Node {
        uint32_t Type = 0; // 0 combiner, 1 delta, 2 current time, 3 generic target, 4 bone pose, 5 morph target
        std::string Name;
        float Min = 0.0f, InverseMin = 1.0f, Max = 1.0f, InverseMax = 1.0f;
        uint32_t InputOperation = 0; // 0 sum, 1 multiply, 2 max, 3 min
        std::vector<Link> Inputs;
    };

    bool parse( const uint8_t *pData, size_t size );
    inline const std::string &getName() const { return name; }
    inline const std::vector<Bone> &getBones() const { return bones; }
    inline const std::vector<Node> &getNodes() const { return nodes; }
    int32_t findNode( const std::string &nodeName ) const;

private:
    std::string name;
    std::vector<Bone> bones;
    std::vector<Node> nodes;
    std::unordered_map<std::string, uint32_t> nodeByName;
};

struct FaceFxCurve {
    struct Key {
        float Time, Value, SlopeIn, SlopeOut;
    };
    std::string Name;
    uint32_t Interpolation = 0; // 0 hermite, 1 linear (FxAnimCurve +0x1c)
    std::vector<Key> Keys;
    // FxAnimCurve::EvaluateAt (FUN_007033c0); *pCache: last key index.
    float evaluate( float time, uint32_t *pCache = nullptr ) const;
};

struct FaceFxAnim {
    std::string Name;
    std::vector<FaceFxCurve> Curves;
    float StartTime = 0.0f; // FUN_006eeb90: min / max key time of the curves
    float EndTime = 1.0f;
    float BlendInTime = 0.16f, BlendOutTime = 0.22f; // serialized, not used by Eden's tick
};

class FaceFxAnimSet {
public:
    bool parse( const uint8_t *pData, size_t size );
    inline const std::string &getActorName() const { return actorName; }
    inline const std::string &getGroupName() const { return groupName; }
    inline const std::vector<FaceFxAnim> &getAnims() const { return anims; }
    const FaceFxAnim *findAnim( const std::string &animName ) const;

private:
    std::string actorName;
    std::string groupName;
    std::vector<FaceFxAnim> anims;
};

// Result of one evaluation, in the Eden skeleton convention (BoneTransform
// of eden_animation.h), plus the TD_Skinface facialAnimCoef1 / 2 values.
struct FaceFxPose {
    std::vector<std::pair<uint32_t, BoneTransform>> Bones; // EdenNameHash4, local transform
    Vector4f FacialCoef1; // (Anim_Nmap_Nose, Anim_Nmap_Mouth_R, Anim_Nmap_Mouth_L, 0)
    Vector4f FacialCoef2; // (Anim_Nmap_Brow, Anim_Nmap_Eye_R, Anim_Nmap_Eye_L, 0)
};

class FaceFxLibrary {
public:
    // avatar/avatar_face_fx.bnk (female.fxa, male.fxa).
    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    // Every .fxe of a bank (the cinematic bank of the current mode).
    void addAnimSets( const Bank &bank );
    void clearAnimSets() { animSets.clear(); }

    const FaceFxActor *getActor( bool bWoman ) const;
    // REFS 0x0306 name "...\female.fxa::Intro_:Intro_Fiesta_01": the
    // animation of the group / name after "::" (split like FUN_00b77a50:
    // group between the last two ':', name after the last one).
    const FaceFxAnim *findAnim( const FaceFxActor &actor, const std::string &reference ) const;

    // FxActorInstance::Tick (FUN_006ece00, audio time = anim time) + the
    // compiled face graph (FUN_006e8a70) + the bone blending of FUN_00b77b20.
    // faceParams: the 49 FaceParamArray values of the .avt (FUN_00b76550:
    // register "replace" value / 100 on the C_* customisation nodes).
    static void Evaluate( const FaceFxActor &actor, const FaceFxAnim &anim, float time, const int8_t *pFaceParams,
                          FaceFxPose &outPose ) {
        Evaluate( actor, &anim, time, pFaceParams, nullptr, outPose );
    }
    // pAnim nullptr: the animator ticks without an animation (FUN_00b77b20
    // with the "running" flag +0x1ec0 set by the look-at, FUN_00b75f50):
    // no curve, registers only. pLookAt: registers LookAt_T2_side /
    // LookAt_T2_vertical (FUN_00b75e10: replace, clamped to the node range).
    static void Evaluate( const FaceFxActor &actor, const FaceFxAnim *pAnim, float time, const int8_t *pFaceParams,
                          const float *pLookAt, FaceFxPose &outPose );

private:
    Bank bank;
    FaceFxActor female;
    FaceFxActor male;
    std::vector<std::unique_ptr<FaceFxAnimSet>> animSets;
    bool bLoaded = false;
};

// FUN_00b770b0: the 49 customisation nodes set from the .avt FaceParamArray.
extern const char *const kFaceFxCustomNodes[49];
