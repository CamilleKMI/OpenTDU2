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

// Per bone blend of two local poses (slerp / lerp).
void BlendPoses( const std::vector<BoneTransform> &a, const std::vector<BoneTransform> &b, float weightB,
                 std::vector<BoneTransform> &out );
