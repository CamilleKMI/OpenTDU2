#pragma once

#include "core/math.h"

// Skeletons (.XSB "SKEL") and skeletal animations (.anm "ANM!") of the
// avatars and animated props (docs/re/avatar_spec.md sections 1 and 2).
//
// Quaternion convention of the engine: the local matrix is
// S * R(conj q) * T in D3DX row-vector math, i.e. in OpenTDU2 column-vector
// terms Local = T * FromQuaternion(conj q) * S, World = World_parent * Local.

// 4 byte case sensitive name fold (character i added into byte i & 3).
uint32_t EdenNameHash4( const char *pName );

struct BoneTransform {
    Quaternionf Rotation; // engine quaternion (rotates by its conjugate)
    Vector3f Translation;
    Vector3f Scale = Vector3f( 1.0f, 1.0f, 1.0f );
};

class EdenSkeleton {
public:
    bool parse( const uint8_t *pData, size_t size );

    inline uint32_t getNumBones() const { return static_cast<uint32_t>( parents.size() ); }
    inline int32_t getParent( uint32_t bone ) const { return parents[bone]; }
    inline uint32_t getHash( uint32_t bone ) const { return hashes[bone]; }
    inline const BoneTransform &getBindLocal( uint32_t bone ) const { return bindLocals[bone]; }
    inline const Matrix4x4f &getInverseBind( uint32_t bone ) const { return inverseBinds[bone]; }
    int32_t findBone( uint32_t hash4 ) const;

    // Local poses -> skin matrices (world * inverse bind, model space).
    void computeSkinMatrices( const std::vector<BoneTransform> &locals, std::vector<Matrix4x4f> &outWorld,
                              std::vector<Matrix4x4f> &outSkin ) const;

private:
    std::vector<int32_t> parents;
    std::vector<uint32_t> hashes;
    std::vector<BoneTransform> bindLocals;
    std::vector<Matrix4x4f> inverseBinds; // column-vector convention
    std::unordered_map<uint32_t, int32_t> boneByHash;
};

class EdenAnimation {
public:
    bool parse( const uint8_t *pData, size_t size );

    inline float getDuration() const { return duration; }
    // Overwrites the animated channels of 'locals' (bind pose for the
    // others) at clip time t (wrapped by the clip duration when bLoop).
    void sample( const EdenSkeleton &skeleton, float time, bool bLoop, std::vector<BoneTransform> &locals ) const;

private:
    struct Channel {
        std::vector<float> Times;
        std::vector<Vector4f> Values; // quaternion xyzw or vector xyz
    };
    struct Track {
        uint32_t BoneHash = 0;
        bool bRotation = false, bTranslation = false, bScale = false;
        Channel Rotation, Translation, Scale;
    };

    float duration = 0.0f;     // DATA+0x24: clamp of the non looping playback
    float loopDuration = 0.0f; // DATA+0x28 (dur2): wrap period (FUN_007cdfa0)
    std::vector<Track> tracks;

    static bool ParseChannel( const uint8_t *pData, size_t size, size_t &offset, bool bRotation, Channel &out );
    static Vector4f SampleChannel( const Channel &channel, float time, bool bRotation );
};

// Driver IK (docs: /tmp/claude-0/spec_driver_seat.md). The IK chains are the
// ones of avatarsdriver.bnk anims\car\driveiklayer6.bas [DATA]: Spine ->
// IK_Back (node type 6), Arm_x / ForeArm_x -> IK_x_Hand and UpLeg_x / Leg_x
// -> IK_x_Foot (type 2). The solver itself is in the encrypted edAnim
// library [ENC]; these are an analytic stand-in [INF]. Goals are in the
// skeleton model space; 'locals' is modified in place.
//
// Two-bone IK: rotates 'root' and 'mid' in the plane of the current bend so
// that 'effector' reaches 'goal'. 'keepWorld' (or -1): bone whose model-space
// rotation is restored after the solve (the hand / foot keeps the clip
// orientation) [INF].
void SolveTwoBoneIK( const EdenSkeleton &skeleton, std::vector<BoneTransform> &locals, int32_t root, int32_t mid,
                     int32_t effector, const Vector3f &goal, int32_t keepWorld = -1 );
// One-bone aim: rotates 'bone' so that 'effector' lies on the bone -> goal
// line.
void SolveAimIK( const EdenSkeleton &skeleton, std::vector<BoneTransform> &locals, int32_t bone, int32_t effector,
                 const Vector3f &goal );

// Per bone blend of two local poses (slerp / lerp).
void BlendPoses( const std::vector<BoneTransform> &a, const std::vector<BoneTransform> &b, float weightB,
                 std::vector<BoneTransform> &out );
