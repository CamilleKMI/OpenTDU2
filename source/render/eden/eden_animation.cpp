#include "shared.h"
#include "eden_animation.h"

namespace {
template <typename T> T ReadAt( const uint8_t *p, size_t offset ) {
    T value;
    memcpy( &value, p + offset, sizeof( T ) );
    return value;
}

Quaternionf Normalize( const Quaternionf &q ) {
    const float length = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
    return length > 0.0f ? Quaternionf( q.x / length, q.y / length, q.z / length, q.w / length ) : Quaternionf();
}

Quaternionf Slerp( const Quaternionf &a, Quaternionf b, float t ) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if ( dot < 0.0f ) {
        b = Quaternionf( -b.x, -b.y, -b.z, -b.w );
        dot = -dot;
    }
    float wa = 1.0f - t, wb = t;
    if ( dot < 0.9995f ) {
        const float theta = std::acos( Min( dot, 1.0f ) );
        const float s = std::sin( theta );
        wa = std::sin( ( 1.0f - t ) * theta ) / s;
        wb = std::sin( t * theta ) / s;
    }
    return Normalize( Quaternionf( a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb ) );
}

Matrix4x4f LocalMatrix( const BoneTransform &t ) {
    return Matrix4x4f::Translation( t.Translation ) *
           Matrix4x4f::FromQuaternion( Quaternionf( -t.Rotation.x, -t.Rotation.y, -t.Rotation.z, t.Rotation.w ) ) *
           Matrix4x4f::Scale( t.Scale );
}
} // namespace

uint32_t EdenNameHash4( const char *pName ) {
    uint8_t bytes[4] = { 0, 0, 0, 0 };
    for ( size_t i = 0; pName[i] != '\0'; i++ ) {
        bytes[i & 3] = static_cast<uint8_t>( bytes[i & 3] + static_cast<uint8_t>( pName[i] ) );
    }
    return bytes[0] | ( bytes[1] << 8 ) | ( bytes[2] << 16 ) | ( static_cast<uint32_t>( bytes[3] ) << 24 );
}

// .XSB: +0x0c ".XSB", +0x10 "SKEL", +0x20 u32 n, s32 parent[n],
// u32 sortedHash4[n], u32 sortedIndex[n], align 16, {f32 q[4], t[4], s[4]}[n]
// (bind local), f32 invBind[n][16] (D3D row-major), u8 id8[n][8].
bool EdenSkeleton::parse( const uint8_t *pData, size_t size ) {
    parents.clear();
    if ( size < 0x24 || memcmp( pData + 0x0c, ".XSB", 4 ) != 0 || memcmp( pData + 0x10, "SKEL", 4 ) != 0 ) {
        return false;
    }
    const uint32_t count = ReadAt<uint32_t>( pData, 0x20 );
    size_t offset = 0x24;
    if ( offset + count * ( 12 + 48 + 64 ) + 16 > size ) {
        return false;
    }
    parents.resize( count );
    hashes.resize( count );
    bindLocals.resize( count );
    inverseBinds.resize( count );
    for ( uint32_t i = 0; i < count; i++ ) {
        parents[i] = ReadAt<int32_t>( pData, offset + i * 4 );
    }
    offset += count * 4;
    const size_t sortedHashes = offset;
    offset += count * 4;
    for ( uint32_t j = 0; j < count; j++ ) {
        const uint32_t bone = ReadAt<uint32_t>( pData, offset + j * 4 );
        if ( bone < count ) {
            hashes[bone] = ReadAt<uint32_t>( pData, sortedHashes + j * 4 );
        }
    }
    offset += count * 4;
    offset = ( offset + 15 ) & ~static_cast<size_t>( 15 );
    for ( uint32_t i = 0; i < count; i++ ) {
        const size_t base = offset + i * 48;
        BoneTransform &bone = bindLocals[i];
        bone.Rotation = Quaternionf( ReadAt<float>( pData, base ), ReadAt<float>( pData, base + 4 ),
                                     ReadAt<float>( pData, base + 8 ), ReadAt<float>( pData, base + 12 ) );
        bone.Translation = Vector3f( ReadAt<float>( pData, base + 16 ), ReadAt<float>( pData, base + 20 ),
                                     ReadAt<float>( pData, base + 24 ) );
        bone.Scale = Vector3f( ReadAt<float>( pData, base + 32 ), ReadAt<float>( pData, base + 36 ),
                               ReadAt<float>( pData, base + 40 ) );
    }
    offset += count * 48;
    for ( uint32_t i = 0; i < count; i++ ) {
        for ( int c = 0; c < 4; c++ ) {
            for ( int r = 0; r < 4; r++ ) {
                inverseBinds[i].m[c][r] = ReadAt<float>( pData, offset + i * 64 + ( c * 4 + r ) * 4 );
            }
        }
    }
    boneByHash.clear();
    for ( uint32_t i = 0; i < count; i++ ) {
        boneByHash.emplace( hashes[i], static_cast<int32_t>( i ) );
    }
    return true;
}

int32_t EdenSkeleton::findBone( uint32_t hash4 ) const {
    auto it = boneByHash.find( hash4 );
    return it != boneByHash.end() ? it->second : -1;
}

void EdenSkeleton::computeSkinMatrices( const std::vector<BoneTransform> &locals, std::vector<Matrix4x4f> &outWorld,
                                        std::vector<Matrix4x4f> &outSkin ) const {
    const uint32_t count = getNumBones();
    outWorld.resize( count );
    outSkin.resize( count );
    for ( uint32_t i = 0; i < count; i++ ) {
        const Matrix4x4f local = LocalMatrix( i < locals.size() ? locals[i] : bindLocals[i] );
        outWorld[i] = parents[i] >= 0 ? outWorld[parents[i]] * local : local;
        outSkin[i] = outWorld[i] * inverseBinds[i];
    }
}

bool EdenAnimation::ParseChannel( const uint8_t *pData, size_t size, size_t &offset, bool bRotation, Channel &out ) {
    if ( offset + 8 > size ) {
        return false;
    }
    const uint16_t numKeys = ReadAt<uint16_t>( pData, offset );
    const uint16_t format = ReadAt<uint16_t>( pData, offset + 2 );
    const float subDuration = ReadAt<float>( pData, offset + 4 );
    offset += 8;
    out.Times.clear();
    out.Values.clear();
    if ( format & 1 ) {
        // u16 normalized times, key 0 implicit at 0.
        out.Times.push_back( 0.0f );
        for ( uint32_t k = 1; k < numKeys; k++ ) {
            out.Times.push_back( ReadAt<uint16_t>( pData, offset ) / 65535.0f * subDuration );
            offset += 2;
        }
    } else {
        for ( uint32_t k = 0; k < numKeys; k++ ) {
            out.Times.push_back( ReadAt<float>( pData, offset ) );
            offset += 4;
        }
    }
    offset = ( offset + 3 ) & ~static_cast<size_t>( 3 );
    float rangeMin = 0.0f, rangeExtent = 0.0f;
    if ( !bRotation && ( format & 4 ) ) {
        rangeMin = ReadAt<float>( pData, offset );
        rangeExtent = ReadAt<float>( pData, offset + 4 );
        offset += 8;
    }
    for ( uint32_t k = 0; k < numKeys; k++ ) {
        const bool bCompressed = ( format & 6 ) != 0;
        if ( format & 0x20 ) {
            offset += bCompressed ? 4 : 12; // reserved prefix
        }
        if ( offset + 16 > size ) {
            return false;
        }
        Vector4f value( 0.0f, 0.0f, 0.0f, 0.0f );
        if ( bRotation ) {
            if ( format & 2 ) {
                const int16_t a = ReadAt<int16_t>( pData, offset );
                const int16_t b = ReadAt<int16_t>( pData, offset + 2 );
                const int16_t c = ReadAt<int16_t>( pData, offset + 4 );
                const float x = a / 32767.0f, y = b / 32767.0f, z = static_cast<int16_t>( c & ~1 ) / 32767.0f;
                float w = std::sqrt( Max( 0.0f, 1.0f - x * x - y * y - z * z ) );
                if ( c & 1 ) {
                    w = -w;
                }
                value = Vector4f( x, y, z, w );
                offset += 6;
            } else {
                value = Vector4f( ReadAt<float>( pData, offset ), ReadAt<float>( pData, offset + 4 ),
                                  ReadAt<float>( pData, offset + 8 ), ReadAt<float>( pData, offset + 12 ) );
                offset += 16;
            }
        } else if ( format & 4 ) {
            value = Vector4f( rangeMin + ReadAt<uint16_t>( pData, offset ) / 65535.0f * rangeExtent,
                              rangeMin + ReadAt<uint16_t>( pData, offset + 2 ) / 65535.0f * rangeExtent,
                              rangeMin + ReadAt<uint16_t>( pData, offset + 4 ) / 65535.0f * rangeExtent, 0.0f );
            offset += 6;
        } else if ( format & 2 ) {
            value = Vector4f( ReadAt<int16_t>( pData, offset ) / 16384.0f, ReadAt<int16_t>( pData, offset + 2 ) / 16384.0f,
                              ReadAt<int16_t>( pData, offset + 4 ) / 16384.0f, 0.0f );
            offset += 6;
        } else {
            value = Vector4f( ReadAt<float>( pData, offset ), ReadAt<float>( pData, offset + 4 ),
                              ReadAt<float>( pData, offset + 8 ), 0.0f );
            offset += 12;
        }
        if ( ( format & 0x20 ) && bCompressed ) {
            offset += 2;
        }
        out.Values.push_back( value );
    }
    offset = ( offset + 3 ) & ~static_cast<size_t>( 3 );
    return true;
}

// ANM!: +0x18 "DATA", +0x20 u32 numTracks, f32 duration, f32 duration2,
// +0x38 tracks { u16 kind (1 rotation, 2 translation, 4 scale, 8 IK);
// u16 0xffff; u32 size; u32 boneHash4; channels }.
bool EdenAnimation::parse( const uint8_t *pData, size_t size ) {
    tracks.clear();
    if ( size < 0x38 || memcmp( pData, "ANM!", 4 ) != 0 || memcmp( pData + 0x18, "DATA", 4 ) != 0 ) {
        return false;
    }
    const uint32_t numTracks = ReadAt<uint32_t>( pData, 0x20 );
    duration = ReadAt<float>( pData, 0x24 );
    loopDuration = ReadAt<float>( pData, 0x28 );
    if ( !( loopDuration > 0.0f ) ) {
        loopDuration = duration;
    }
    size_t offset = 0x38;
    for ( uint32_t i = 0; i < numTracks && offset + 12 <= size; i++ ) {
        const uint16_t kind = ReadAt<uint16_t>( pData, offset );
        const uint32_t trackSize = ReadAt<uint32_t>( pData, offset + 4 );
        if ( trackSize < 12 || offset + trackSize > size ) {
            break;
        }
        Track track;
        track.BoneHash = ReadAt<uint32_t>( pData, offset + 8 );
        size_t channelOffset = offset + 12;
        bool bOk = ( kind & 8 ) == 0; // IK constraint tracks: ignored
        if ( bOk && ( kind & 1 ) ) {
            track.bRotation = ParseChannel( pData, size, channelOffset, true, track.Rotation );
        }
        if ( bOk && ( kind & 2 ) ) {
            track.bTranslation = ParseChannel( pData, size, channelOffset, false, track.Translation );
        }
        if ( bOk && ( kind & 4 ) ) {
            track.bScale = ParseChannel( pData, size, channelOffset, false, track.Scale );
        }
        if ( bOk ) {
            tracks.push_back( std::move( track ) );
        }
        offset += trackSize;
    }
    return !tracks.empty();
}

Vector4f EdenAnimation::SampleChannel( const Channel &channel, float time, bool bRotation ) {
    const std::vector<float> &times = channel.Times;
    if ( times.empty() ) {
        return Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    }
    if ( times.size() == 1 || time <= times.front() ) {
        return channel.Values.front();
    }
    if ( time >= times.back() ) {
        return channel.Values.back();
    }
    const size_t k = static_cast<size_t>( std::upper_bound( times.begin(), times.end(), time ) - times.begin() ) - 1;
    const float span = times[k + 1] - times[k];
    const float u = span > 0.0f ? ( time - times[k] ) / span : 0.0f;
    const Vector4f &a = channel.Values[k];
    const Vector4f &b = channel.Values[k + 1];
    if ( bRotation ) {
        const Quaternionf q = Slerp( Quaternionf( a.x, a.y, a.z, a.w ), Quaternionf( b.x, b.y, b.z, b.w ), u );
        return Vector4f( q.x, q.y, q.z, q.w );
    }
    return Vector4f( a.x + ( b.x - a.x ) * u, a.y + ( b.y - a.y ) * u, a.z + ( b.z - a.z ) * u, 0.0f );
}

void EdenAnimation::sample( const EdenSkeleton &skeleton, float time, bool bLoop,
                            std::vector<BoneTransform> &locals ) const {
    // FUN_007ce070: loop mode t' = fmod(t - D, D) with D = dur2 (the
    // [dur, dur2] tail holds the last pose); otherwise clamped on dur.
    if ( bLoop && loopDuration > 0.0f ) {
        time = std::fmod( time, loopDuration );
        if ( time < 0.0f ) {
            time += loopDuration;
        }
    } else {
        time = Clamp( time, 0.0f, duration );
    }
    for ( const Track &track : tracks ) {
        const int32_t bone = skeleton.findBone( track.BoneHash );
        if ( bone < 0 || static_cast<size_t>( bone ) >= locals.size() ) {
            continue;
        }
        BoneTransform &local = locals[bone];
        if ( track.bRotation ) {
            const Vector4f q = SampleChannel( track.Rotation, time, true );
            local.Rotation = Quaternionf( q.x, q.y, q.z, q.w );
        }
        if ( track.bTranslation ) {
            const Vector4f t = SampleChannel( track.Translation, time, false );
            local.Translation = Vector3f( t.x, t.y, t.z );
        }
        if ( track.bScale ) {
            const Vector4f s = SampleChannel( track.Scale, time, false );
            local.Scale = Vector3f( s.x, s.y, s.z );
        }
    }
}

void BlendPoses( const std::vector<BoneTransform> &a, const std::vector<BoneTransform> &b, float weightB,
                 std::vector<BoneTransform> &out ) {
    out.resize( Min( a.size(), b.size() ) );
    for ( size_t i = 0; i < out.size(); i++ ) {
        out[i].Rotation = Slerp( a[i].Rotation, b[i].Rotation, weightB );
        out[i].Translation = a[i].Translation + ( b[i].Translation - a[i].Translation ) * weightB;
        out[i].Scale = a[i].Scale + ( b[i].Scale - a[i].Scale ) * weightB;
    }
}

namespace {
// Model-space rotation (actual rotation, i.e. the conjugate of the engine
// quaternion composed down the chain) and position of every bone.
void ComputeModelPose( const EdenSkeleton &skeleton, const std::vector<BoneTransform> &locals,
                       std::vector<Quaternionf> &outRotations, std::vector<Vector3f> &outPositions ) {
    std::vector<Matrix4x4f> world, skin;
    skeleton.computeSkinMatrices( locals, world, skin );
    const uint32_t count = skeleton.getNumBones();
    outRotations.resize( count );
    outPositions.resize( count );
    for ( uint32_t i = 0; i < count; i++ ) {
        const Quaternionf &q = locals[i].Rotation;
        const Quaternionf local = Normalize( Quaternionf( -q.x, -q.y, -q.z, q.w ) );
        const int32_t parent = skeleton.getParent( i );
        outRotations[i] = parent >= 0 ? Normalize( outRotations[parent] * local ) : local;
        outPositions[i] = world[i].getTranslation();
    }
}

Quaternionf Conjugate( const Quaternionf &q ) {
    return Quaternionf( -q.x, -q.y, -q.z, q.w );
}

// Model-space rotation 'delta' applied to 'bone' (about its own origin).
void RotateBoneModel( const EdenSkeleton &skeleton, std::vector<BoneTransform> &locals,
                      const std::vector<Quaternionf> &rotations, int32_t bone, const Quaternionf &delta ) {
    const int32_t parent = skeleton.getParent( bone );
    const Quaternionf parentRotation = parent >= 0 ? rotations[parent] : Quaternionf();
    const Quaternionf world = Normalize( delta * rotations[bone] );
    const Quaternionf local = Normalize( Conjugate( parentRotation ) * world );
    locals[bone].Rotation = Conjugate( local );
}

// Rotation taking direction a onto direction b.
Quaternionf RotationBetween( const Vector3f &a, const Vector3f &b ) {
    const Vector3f na = a.normalized(), nb = b.normalized();
    const float cosAngle = Clamp( na.dot( nb ), -1.0f, 1.0f );
    Vector3f axis = na.cross( nb );
    if ( axis.length() < 1e-6f ) {
        return Quaternionf();
    }
    return Quaternionf::FromAxisAngle( axis, std::acos( cosAngle ) );
}
} // namespace

namespace {
void SolveTwoBoneOnce( const EdenSkeleton &skeleton, std::vector<BoneTransform> &locals, int32_t root, int32_t mid,
                       int32_t effector, const Vector3f &goal, int32_t keepWorld, const Quaternionf &keptRotation ) {
    std::vector<Quaternionf> rotations;
    std::vector<Vector3f> positions;
    ComputeModelPose( skeleton, locals, rotations, positions );
    const Vector3f a = positions[root], b = positions[mid], c = positions[effector];
    const float lab = ( b - a ).length(), lcb = ( c - b ).length();
    const float lat = Clamp( ( goal - a ).length(), 1e-4f, ( lab + lcb ) * 0.9999f );
    if ( lab < 1e-5f || lcb < 1e-5f ) {
        return;
    }
    auto angleBetween = []( const Vector3f &u, const Vector3f &v ) {
        return std::acos( Clamp( u.normalized().dot( v.normalized() ), -1.0f, 1.0f ) );
    };
    const float rootAngle0 = angleBetween( c - a, b - a );
    const float midAngle0 = angleBetween( a - b, c - b );
    const float rootAngle1 = std::acos( Clamp( ( lcb * lcb - lab * lab - lat * lat ) / ( -2.0f * lab * lat ), -1.0f, 1.0f ) );
    const float midAngle1 = std::acos( Clamp( ( lat * lat - lab * lab - lcb * lcb ) / ( -2.0f * lab * lcb ), -1.0f, 1.0f ) );
    // Bend plane of the clip pose.
    Vector3f axis = ( c - a ).cross( b - a );
    if ( axis.length() < 1e-6f ) {
        axis = ( goal - a ).cross( b - a );
    }
    if ( axis.length() < 1e-6f ) {
        return;
    }
    axis = axis.normalized();
    RotateBoneModel( skeleton, locals, rotations, mid, Quaternionf::FromAxisAngle( axis, midAngle1 - midAngle0 ) );
    ComputeModelPose( skeleton, locals, rotations, positions );
    RotateBoneModel( skeleton, locals, rotations, root, Quaternionf::FromAxisAngle( axis, rootAngle1 - rootAngle0 ) );
    ComputeModelPose( skeleton, locals, rotations, positions );
    RotateBoneModel( skeleton, locals, rotations, root,
                     RotationBetween( positions[effector] - positions[root], goal - positions[root] ) );
    if ( keepWorld >= 0 ) {
        ComputeModelPose( skeleton, locals, rotations, positions );
        const int32_t parent = skeleton.getParent( keepWorld );
        const Quaternionf parentRotation = parent >= 0 ? rotations[parent] : Quaternionf();
        locals[keepWorld].Rotation = Conjugate( Normalize( Conjugate( parentRotation ) * keptRotation ) );
    }
}
} // namespace

void SolveTwoBoneIK( const EdenSkeleton &skeleton, std::vector<BoneTransform> &locals, int32_t root, int32_t mid,
                     int32_t effector, const Vector3f &goal, int32_t keepWorld ) {
    if ( root < 0 || mid < 0 || effector < 0 ) {
        return;
    }
    std::vector<Quaternionf> rotations;
    std::vector<Vector3f> positions;
    ComputeModelPose( skeleton, locals, rotations, positions );
    const Quaternionf keptRotation = keepWorld >= 0 ? rotations[keepWorld] : Quaternionf();
    // The kept bone carries the effector: re-solve until it settles.
    for ( int i = 0; i < ( keepWorld >= 0 ? 6 : 1 ); i++ ) {
        SolveTwoBoneOnce( skeleton, locals, root, mid, effector, goal, keepWorld, keptRotation );
    }
}

void SolveAimIK( const EdenSkeleton &skeleton, std::vector<BoneTransform> &locals, int32_t bone, int32_t effector,
                 const Vector3f &goal ) {
    if ( bone < 0 || effector < 0 ) {
        return;
    }
    std::vector<Quaternionf> rotations;
    std::vector<Vector3f> positions;
    ComputeModelPose( skeleton, locals, rotations, positions );
    RotateBoneModel( skeleton, locals, rotations, bone,
                     RotationBetween( positions[effector] - positions[bone], goal - positions[bone] ) );
}
