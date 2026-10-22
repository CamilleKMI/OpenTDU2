#include "shared.h"
#include "pmi.h"

namespace {
template <typename T> T ReadAt( const uint8_t *pFile, uint32_t offset ) {
    T value;
    memcpy( &value, pFile + offset, sizeof( T ) );
    return value;
}

constexpr float kPosition = 0.0156252384185791f; // 0x00f4ae38
constexpr float kPositionOffset = 128.0f;         // 0x00f49e08
constexpr float kScale = 6.103608757257462e-05f;  // 0x00f4ae30
constexpr float kAngle = 9.587526437826455e-05f;  // 0x00f4ae40
constexpr float kBox = 0.0625009536743164f;       // 0x00f4ae28
constexpr float kBoxOffset = 1024.0f;             // 0x00f4add0

struct D3DMatrix {
    float m[4][4]; // [row][column], row vectors
    static D3DMatrix Identity() {
        D3DMatrix r {};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }
    D3DMatrix operator*( const D3DMatrix &o ) const {
        D3DMatrix r {};
        for ( int i = 0; i < 4; i++ ) {
            for ( int j = 0; j < 4; j++ ) {
                for ( int k = 0; k < 4; k++ ) {
                    r.m[i][j] += m[i][k] * o.m[k][j];
                }
            }
        }
        return r;
    }
};

// D3DX conventions (checked on the stored AABBs, see the spec).
D3DMatrix RotationX( float a ) {
    D3DMatrix r = D3DMatrix::Identity();
    const float c = std::cos( a ), s = std::sin( a );
    r.m[1][1] = c;
    r.m[1][2] = s;
    r.m[2][1] = -s;
    r.m[2][2] = c;
    return r;
}

D3DMatrix RotationY( float a ) {
    D3DMatrix r = D3DMatrix::Identity();
    const float c = std::cos( a ), s = std::sin( a );
    r.m[0][0] = c;
    r.m[0][2] = -s;
    r.m[2][0] = s;
    r.m[2][2] = c;
    return r;
}

D3DMatrix RotationZ( float a ) {
    D3DMatrix r = D3DMatrix::Identity();
    const float c = std::cos( a ), s = std::sin( a );
    r.m[0][0] = c;
    r.m[0][1] = s;
    r.m[1][0] = -s;
    r.m[1][1] = c;
    return r;
}
} // namespace

bool ParsePmi( const uint8_t *pFile, uint32_t fileSize, const Vector3f &sectorOrigin,
               std::vector<PmiInstance> &outInstances ) {
    if ( fileSize < 0x1d0 || memcmp( pFile + 0x0c, ".PMI", 4 ) != 0 || memcmp( pFile + 0x10, "MOBA", 4 ) != 0 ) {
        return false;
    }
    const uint32_t numMob = ReadAt<uint32_t>( pFile, 0x20 );
    const float heightMin = ReadAt<float>( pFile, 0x24 );
    const float heightMax = ReadAt<float>( pFile, 0x28 );
    const float heightScale = ( heightMax - heightMin ) / 65535.0f;
    uint32_t offset = 0x10 + ReadAt<uint32_t>( pFile, 0x1c ); // MOBA data size includes its header (0x1d0)
    for ( uint32_t mob = 0; mob < numMob; mob++ ) {
        if ( offset + 0x20 > fileSize || memcmp( pFile + offset, "MOB.", 4 ) != 0 ) {
            return false;
        }
        const uint32_t sectionSize = ReadAt<uint32_t>( pFile, offset + 8 );
        const uint64_t hashcode = ReadAt<uint64_t>( pFile, offset + 0x10 );
        const uint16_t count = ReadAt<uint16_t>( pFile, offset + 0x18 );
        const uint16_t extraSize = ReadAt<uint16_t>( pFile, offset + 0x1a );
        const uint8_t type = pFile[offset + 0x1e];
        const uint32_t stride = 0x20 + ( ( extraSize + 15u ) & ~15u );
        if ( sectionSize < 0x20 + count * stride || offset + sectionSize > fileSize ) {
            return false;
        }
        for ( uint32_t i = 0; i < count && type != 0; i++ ) {
            const uint32_t record = offset + 0x20 + i * stride;
            uint16_t w[16];
            memcpy( w, pFile + record, sizeof( w ) );
            PmiInstance instance;
            instance.Hashcode = hashcode;
            instance.Type = type;
            const Vector3f local( w[0] * kPosition - kPositionOffset, heightMin + w[1] * heightScale,
                                  w[2] * kPosition - kPositionOffset );
            instance.Position = Vector3f( sectorOrigin.x + local.x, local.y, sectorOrigin.z + local.z );
            D3DMatrix scale = D3DMatrix::Identity();
            scale.m[0][0] = w[3] * kScale;
            scale.m[1][1] = w[4] * kScale;
            scale.m[2][2] = w[5] * kScale;
            D3DMatrix matrix =
                scale * RotationX( w[6] * kAngle ) * RotationZ( w[8] * kAngle ) * RotationY( w[7] * kAngle );
            matrix.m[3][0] = instance.Position.x;
            matrix.m[3][1] = instance.Position.y;
            matrix.m[3][2] = instance.Position.z;
            for ( int c = 0; c < 4; c++ ) {
                for ( int r = 0; r < 4; r++ ) {
                    instance.World.m[c][r] = matrix.m[c][r];
                }
            }
            instance.BoxMin = instance.Position + Vector3f( w[10] * kBox - kBoxOffset, w[11] * kBox - kBoxOffset,
                                                            w[12] * kBox - kBoxOffset );
            instance.BoxMax = instance.Position + Vector3f( w[13] * kBox - kBoxOffset, w[14] * kBox - kBoxOffset,
                                                            w[15] * kBox - kBoxOffset );
            if ( extraSize > 0 ) {
                instance.pPayload = pFile + record + 0x20;
                instance.PayloadSize = extraSize;
            }
            outInstances.push_back( instance );
        }
        offset += sectionSize;
    }
    return true;
}
