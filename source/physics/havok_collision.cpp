#include "shared.h"
#include "havok_collision.h"

#include <cfloat>
#include <string>
#include <unordered_map>

// Havok 5.5.0-r1 packfile reader. All offsets below are [DATA]: they come
// from the hkClass/hkClassMember reflection serialized in the __types__
// section of the TDU2 files themselves (32-bit layout, layoutRules 4/1/0/1).

namespace {
template <typename T> T ReadAt( const uint8_t *p, size_t offset ) {
    T value;
    memcpy( &value, p + offset, sizeof( T ) );
    return value;
}

constexpr uint8_t kPackfileMagic[8] = { 0x57, 0xe0, 0xe0, 0x57, 0x10, 0xc0, 0xc0, 0x10 };
constexpr size_t kPackfileHeaderSize = 0x40;
constexpr size_t kSectionHeaderSize = 0x30;
constexpr size_t kEdenHeaderSize = 0x20;
constexpr size_t kEdenEntrySize = 0x20;

// hkpPhysicsSystem
constexpr uint32_t kPhysSysRigidBodies = 0x08; // hkArray<hkpRigidBody*>
constexpr uint32_t kPhysSysConstraints = 0x14; // hkArray<hkpConstraintInstance*>
// hkpRigidBody (hkpEntity / hkpWorldObject)
constexpr uint32_t kBodyShape = 0x10;          // m_collidable.m_shape
constexpr uint32_t kBodyFriction = 0x84;       // m_material.m_friction
constexpr uint32_t kBodyMotionType = 0xd8;     // m_motion.m_type (u8)
constexpr uint32_t kBodyTransform = 0xe0;      // m_motion.m_motionState.m_transform
// hkpConvexShape
constexpr uint32_t kConvexRadius = 0x10;
// hkpBoxShape
constexpr uint32_t kBoxHalfExtents = 0x20;
// hkpCapsuleShape
constexpr uint32_t kCapsuleVertexA = 0x20;
constexpr uint32_t kCapsuleVertexB = 0x30;
// hkpMoppBvTreeShape: m_child (hkpSingleShapeContainer @0x30, vtable then m_childShape)
constexpr uint32_t kMoppChildShape = 0x34;
// hkpConvexTransformShape: m_childShape container @0x14, m_transform @0x20
constexpr uint32_t kConvexTransformChild = 0x18;
constexpr uint32_t kConvexTransformTransform = 0x20;
constexpr uint32_t kTransformShapeChild = 0x14;     // hkpTransformShape childShape.childShape
constexpr uint32_t kTransformShapeTransform = 0x30; // hkpTransformShape transform
// hkpListShape
constexpr uint32_t kListChildInfo = 0x18; // hkArray<hkpListShapeChildInfo> (0x10 each, shape @0)
constexpr uint32_t kListChildInfoSize = 0x10;
// hkpExtendedMeshShape / hkpStorageExtendedMeshShape
constexpr uint32_t kEmsScaling = 0x20;
constexpr uint32_t kEmsTrianglesSubparts = 0x50; // hkSimpleArray (ptr, size)
constexpr uint32_t kEmsEmbeddedSubpart = 0x70;
constexpr uint32_t kEmsMeshStorage = 0xc0;       // hkArray<MeshSubpartStorage*>
// hkpExtendedMeshShape::TrianglesSubpart (0x40)
constexpr uint32_t kSubpartSize = 0x40;
constexpr uint32_t kSubpartNumTriangles = 0x10;
constexpr uint32_t kSubpartVertexStriding = 0x18;
constexpr uint32_t kSubpartNumVertices = 0x1c;
constexpr uint32_t kSubpartIndexStriding = 0x34;
constexpr uint32_t kSubpartStridingType = 0x38; // 1 = int16, 2 = int32
constexpr uint32_t kSubpartFlipAlternate = 0x39;
// hkpStorageExtendedMeshShape::MeshSubpartStorage
constexpr uint32_t kStorageVertices = 0x08;  // hkArray<hkVector4>
constexpr uint32_t kStorageIndices16 = 0x14; // hkArray<hkUint16>
constexpr uint32_t kStorageIndices32 = 0x20; // hkArray<hkUint32>
// hkpConstraintInstance
constexpr uint32_t kConstraintData = 0x0c;
constexpr uint32_t kConstraintEntityA = 0x14;
constexpr uint32_t kConstraintEntityB = 0x18;
// hkpLimitedHingeConstraintData: m_atoms @0x10
constexpr uint32_t kHingeTransformA = 0x10 + 0x10; // atoms.transforms.transformA
constexpr uint32_t kHingeTransformB = 0x10 + 0x50; // atoms.transforms.transformB
constexpr uint32_t kHingeAngLimit = 0x10 + 0xb0;   // atoms.angLimit

constexpr uint8_t kMotionFixed = 7;

struct Packfile {
    const uint8_t *pData = nullptr; // __data__ section start
    uint32_t dataSize = 0;          // bytes before the fixup tables
    std::unordered_map<uint32_t, uint32_t> pointers;     // src -> dst (both in __data__)
    std::unordered_map<uint32_t, std::string> classes;   // object offset -> class name

    bool valid( uint32_t offset, uint32_t size ) const {
        return offset <= dataSize && size <= dataSize - offset;
    }
    uint32_t ptr( uint32_t offset ) const {
        auto it = pointers.find( offset );
        return it != pointers.end() ? it->second : UINT32_MAX;
    }
    const std::string *classOf( uint32_t offset ) const {
        auto it = classes.find( offset );
        return it != classes.end() ? &it->second : nullptr;
    }
    float f32( uint32_t offset ) const { return ReadAt<float>( pData, offset ); }
    uint32_t u32( uint32_t offset ) const { return ReadAt<uint32_t>( pData, offset ); }
    Vector3f vec3( uint32_t offset ) const {
        return Vector3f( f32( offset ), f32( offset + 4 ), f32( offset + 8 ) );
    }
    // hkTransform: hkRotation (3 column hkVector4) + translation hkVector4.
    Matrix4x4f transform( uint32_t offset ) const {
        Matrix4x4f m = Matrix4x4f::Identity();
        for ( int c = 0; c < 4; c++ ) {
            for ( int r = 0; r < 3; r++ ) {
                m.m[c][r] = f32( offset + c * 16 + r * 4 );
            }
        }
        return m;
    }
    // hkArray / hkSimpleArray: { T* data; int size; ... }
    bool array( uint32_t offset, uint32_t elemSize, uint32_t &outData, uint32_t &outCount ) const {
        outCount = u32( offset + 4 );
        if ( outCount == 0 ) {
            outData = 0;
            return true;
        }
        outData = ptr( offset );
        return outData != UINT32_MAX && valid( outData, outCount * elemSize );
    }
    bool is( uint32_t offset, const char *pClass ) const {
        const std::string *pName = classOf( offset );
        return pName != nullptr && *pName == pClass;
    }
};

bool ParsePackfile( const uint8_t *p, size_t size, Packfile &out ) {
    if ( size < kPackfileHeaderSize || memcmp( p, kPackfileMagic, 8 ) != 0 ) {
        return false;
    }
    const int32_t fileVersion = ReadAt<int32_t>( p, 0x0c );
    const uint8_t bytesInPointer = p[0x10];
    const uint8_t littleEndian = p[0x11];
    const int32_t numSections = ReadAt<int32_t>( p, 0x14 );
    if ( fileVersion != 5 || bytesInPointer != 4 || littleEndian != 1 || numSections <= 0 ||
         numSections > 16 || kPackfileHeaderSize + numSections * kSectionHeaderSize > size ) {
        OTDU_LOG_WARN( "Havok: unsupported packfile (version %d, ptr %u, le %u, %d sections)\n",
                       fileVersion, bytesInPointer, littleEndian, numSections );
        return false;
    }

    struct Section {
        char tag[20];
        uint32_t start, local, global, virt, exports, imports, end;
    };
    std::vector<Section> sections( numSections );
    int32_t classNamesIndex = -1, dataIndex = -1;
    for ( int32_t i = 0; i < numSections; i++ ) {
        const uint8_t *h = p + kPackfileHeaderSize + i * kSectionHeaderSize;
        Section &s = sections[i];
        memcpy( s.tag, h, 19 );
        s.tag[19] = '\0';
        s.start = ReadAt<uint32_t>( h, 0x14 );
        s.local = ReadAt<uint32_t>( h, 0x18 );
        s.global = ReadAt<uint32_t>( h, 0x1c );
        s.virt = ReadAt<uint32_t>( h, 0x20 );
        s.exports = ReadAt<uint32_t>( h, 0x24 );
        s.imports = ReadAt<uint32_t>( h, 0x28 );
        s.end = ReadAt<uint32_t>( h, 0x2c );
        if ( s.start > size || s.end > size - s.start || s.local > s.global || s.global > s.virt ||
             s.virt > s.exports ) {
            OTDU_LOG_WARN( "Havok: bad section header %d\n", i );
            return false;
        }
        if ( strcmp( s.tag, "__classnames__" ) == 0 ) {
            classNamesIndex = i;
        } else if ( strcmp( s.tag, "__data__" ) == 0 ) {
            dataIndex = i;
        }
    }
    if ( classNamesIndex < 0 || dataIndex < 0 ) {
        OTDU_LOG_WARN( "Havok: missing __classnames__ or __data__ section\n" );
        return false;
    }

    const Section &names = sections[classNamesIndex];
    const Section &data = sections[dataIndex];
    const uint8_t *pNames = p + names.start;
    const uint8_t *pData = p + data.start;
    out.pData = pData;
    out.dataSize = data.local;

    // Local fixups: { i32 src, i32 dst } within the section.
    for ( uint32_t o = data.local; o + 8 <= data.global; o += 8 ) {
        const int32_t src = ReadAt<int32_t>( pData, o );
        const int32_t dst = ReadAt<int32_t>( pData, o + 4 );
        if ( src < 0 ) {
            continue;
        }
        out.pointers[src] = dst;
    }
    // Global fixups: { i32 src, i32 dstSection, i32 dst }; only __data__ targets are used.
    for ( uint32_t o = data.global; o + 12 <= data.virt; o += 12 ) {
        const int32_t src = ReadAt<int32_t>( pData, o );
        const int32_t dstSection = ReadAt<int32_t>( pData, o + 4 );
        const int32_t dst = ReadAt<int32_t>( pData, o + 8 );
        if ( src < 0 || dstSection != dataIndex ) {
            continue;
        }
        out.pointers[src] = dst;
    }
    // Virtual fixups: { i32 object, i32 classNameSection, i32 classNameOffset }.
    // The class names section holds { u32 signature, u8 0x09, char name[] }.
    for ( uint32_t o = data.virt; o + 12 <= data.exports; o += 12 ) {
        const int32_t obj = ReadAt<int32_t>( pData, o );
        const int32_t nameSection = ReadAt<int32_t>( pData, o + 4 );
        const int32_t nameOffset = ReadAt<int32_t>( pData, o + 8 );
        if ( obj < 0 || nameSection != classNamesIndex || nameOffset < 0 ||
             static_cast<uint32_t>( nameOffset ) >= names.local ) {
            continue;
        }
        const char *pName = reinterpret_cast<const char *>( pNames + nameOffset );
        const size_t maxLen = names.local - nameOffset;
        out.classes[obj] = std::string( pName, strnlen( pName, maxLen ) );
    }
    return true;
}

struct Builder {
    const Packfile &pack;
    HavokCollision &out;
    uint32_t bodyIndex = 0;

    void addShape( uint32_t shape, const Matrix4x4f &xform, uint32_t depth ) {
        if ( shape == UINT32_MAX || depth > 16 ) {
            return;
        }
        const std::string *pClass = pack.classOf( shape );
        if ( pClass == nullptr ) {
            OTDU_LOG_WARN( "Havok: shape at 0x%x has no class\n", shape );
            return;
        }
        if ( *pClass == "hkpBoxShape" && pack.valid( shape, 0x30 ) ) {
            HavokCollision::Box box;
            box.Transform = xform;
            box.HalfExtents = pack.vec3( shape + kBoxHalfExtents );
            box.ConvexRadius = pack.f32( shape + kConvexRadius );
            box.BodyIndex = bodyIndex;
            out.Boxes.push_back( box );
        } else if ( *pClass == "hkpCapsuleShape" && pack.valid( shape, 0x40 ) ) {
            HavokCollision::Capsule capsule;
            capsule.A = xform.transformPoint( pack.vec3( shape + kCapsuleVertexA ) );
            capsule.B = xform.transformPoint( pack.vec3( shape + kCapsuleVertexB ) );
            capsule.Radius = pack.f32( shape + kConvexRadius );
            capsule.BodyIndex = bodyIndex;
            out.Capsules.push_back( capsule );
        } else if ( *pClass == "hkpMoppBvTreeShape" && pack.valid( shape, 0x40 ) ) {
            addShape( pack.ptr( shape + kMoppChildShape ), xform, depth + 1 );
        } else if ( *pClass == "hkpConvexTransformShape" && pack.valid( shape, 0x60 ) ) {
            const Matrix4x4f local = pack.transform( shape + kConvexTransformTransform );
            addShape( pack.ptr( shape + kConvexTransformChild ), xform * local, depth + 1 );
        } else if ( *pClass == "hkpTransformShape" && pack.valid( shape, 0x70 ) ) {
            // hkpTransformShape (0x70, reflection): childShape
            // hkpSingleShapeContainer at +0x10 (pointer at +0x14), rotation
            // quaternion +0x20, transform hkTransform +0x30 [DATA:
            // eph_l3di_sc_*.shk].
            const Matrix4x4f local = pack.transform( shape + kTransformShapeTransform );
            addShape( pack.ptr( shape + kTransformShapeChild ), xform * local, depth + 1 );
        } else if ( *pClass == "hkpSphereShape" && pack.valid( shape, 0x20 ) ) {
            // hkpSphereShape (0x20): radius = hkpConvexShape +0x10, centred
            // on the shape origin [DATA: Library.dhk]. Stored as a capsule
            // with a zero length segment.
            HavokCollision::Capsule sphere;
            sphere.A = sphere.B = xform.transformPoint( Vector3f( 0.0f, 0.0f, 0.0f ) );
            sphere.Radius = pack.f32( shape + kConvexRadius );
            sphere.BodyIndex = bodyIndex;
            out.Capsules.push_back( sphere );
        } else if ( *pClass == "hkpListShape" && pack.valid( shape, 0x70 ) ) {
            uint32_t children = 0, count = 0;
            if ( !pack.array( shape + kListChildInfo, kListChildInfoSize, children, count ) ) {
                return;
            }
            for ( uint32_t i = 0; i < count; i++ ) {
                addShape( pack.ptr( children + i * kListChildInfoSize ), xform, depth + 1 );
            }
        } else if ( *pClass == "hkpStorageExtendedMeshShape" && pack.valid( shape, 0xe0 ) ) {
            addMesh( shape, xform );
        } else {
            OTDU_LOG_WARN( "Havok: unsupported shape class '%s'\n", pClass->c_str() );
        }
    }

    void addMesh( uint32_t shape, const Matrix4x4f &xform ) {
        const Vector3f scale = pack.vec3( shape + kEmsScaling );
        const uint32_t numSubparts = pack.u32( shape + kEmsTrianglesSubparts + 4 );
        uint32_t subparts = pack.ptr( shape + kEmsTrianglesSubparts );
        if ( subparts == UINT32_MAX && numSubparts == 1 ) {
            subparts = shape + kEmsEmbeddedSubpart;
        }
        uint32_t storages = 0, numStorages = 0;
        if ( numSubparts == 0 || subparts == UINT32_MAX ||
             !pack.valid( subparts, numSubparts * kSubpartSize ) ||
             !pack.array( shape + kEmsMeshStorage, 4, storages, numStorages ) ) {
            return;
        }
        // The subpart vertex/index bases are not serialized: the storage
        // arrays (one per triangle subpart, same order) hold the data.
        for ( uint32_t s = 0; s < numSubparts && s < numStorages; s++ ) {
            const uint32_t sub = subparts + s * kSubpartSize;
            const uint32_t storage = pack.ptr( storages + s * 4 );
            if ( storage == UINT32_MAX || !pack.valid( storage, 0x50 ) ) {
                continue;
            }
            const uint32_t numTriangles = pack.u32( sub + kSubpartNumTriangles );
            const uint32_t vertexStriding = pack.u32( sub + kSubpartVertexStriding );
            const uint32_t indexStriding = pack.u32( sub + kSubpartIndexStriding );
            const uint8_t stridingType = pack.pData[sub + kSubpartStridingType];
            const bool bFlipAlternate = pack.pData[sub + kSubpartFlipAlternate] != 0;

            uint32_t vertices = 0, numVertices = 0;
            if ( vertexStriding != 16 ||
                 !pack.array( storage + kStorageVertices, 16, vertices, numVertices ) ) {
                OTDU_LOG_WARN( "Havok: unsupported mesh vertex storage (stride %u)\n", vertexStriding );
                continue;
            }
            const bool b16 = stridingType == 1;
            const uint32_t indexSize = b16 ? 2 : 4;
            uint32_t indices = 0, numIndices = 0;
            if ( ( stridingType != 1 && stridingType != 2 ) || indexStriding < 3 * indexSize ||
                 !pack.array( storage + ( b16 ? kStorageIndices16 : kStorageIndices32 ), indexSize,
                              indices, numIndices ) ) {
                OTDU_LOG_WARN( "Havok: unsupported mesh index storage (type %u)\n", stridingType );
                continue;
            }
            const uint32_t indicesPerTriangle = indexStriding / indexSize;
            if ( numTriangles == 0 || ( numTriangles - 1 ) * indicesPerTriangle + 3 > numIndices ) {
                continue;
            }

            const uint32_t base = static_cast<uint32_t>( out.Vertices.size() );
            for ( uint32_t v = 0; v < numVertices; v++ ) {
                Vector3f p = pack.vec3( vertices + v * 16 );
                p = Vector3f( p.x * scale.x, p.y * scale.y, p.z * scale.z );
                out.Vertices.push_back( xform.transformPoint( p ) );
            }
            for ( uint32_t t = 0; t < numTriangles; t++ ) {
                uint32_t tri[3];
                for ( uint32_t k = 0; k < 3; k++ ) {
                    const uint32_t at = indices + ( t * indicesPerTriangle + k ) * indexSize;
                    tri[k] = b16 ? ReadAt<uint16_t>( pack.pData, at ) : pack.u32( at );
                }
                if ( tri[0] >= numVertices || tri[1] >= numVertices || tri[2] >= numVertices ) {
                    continue;
                }
                if ( bFlipAlternate && ( t & 1 ) ) {
                    std::swap( tri[1], tri[2] );
                }
                out.Indices.push_back( base + tri[0] );
                out.Indices.push_back( base + tri[1] );
                out.Indices.push_back( base + tri[2] );
            }
        }
    }
};

void ExpandBounds( HavokCollision &out, const Vector3f &p ) {
    out.BoundsMin = Vector3f( Min( out.BoundsMin.x, p.x ), Min( out.BoundsMin.y, p.y ), Min( out.BoundsMin.z, p.z ) );
    out.BoundsMax = Vector3f( Max( out.BoundsMax.x, p.x ), Max( out.BoundsMax.y, p.y ), Max( out.BoundsMax.z, p.z ) );
}

Vector3f BoxAxis( const HavokCollision::Box &box, int axis ) {
    return Vector3f( box.Transform.m[axis][0], box.Transform.m[axis][1], box.Transform.m[axis][2] );
}

// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5).
Vector3f ClosestPointOnTriangle( const Vector3f &p, const Vector3f &a, const Vector3f &b,
                                 const Vector3f &c ) {
    const Vector3f ab = b - a, ac = c - a, ap = p - a;
    const float d1 = ab.dot( ap ), d2 = ac.dot( ap );
    if ( d1 <= 0.0f && d2 <= 0.0f ) return a;
    const Vector3f bp = p - b;
    const float d3 = ab.dot( bp ), d4 = ac.dot( bp );
    if ( d3 >= 0.0f && d4 <= d3 ) return b;
    const float vc = d1 * d4 - d3 * d2;
    if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f ) return a + ab * ( d1 / ( d1 - d3 ) );
    const Vector3f cp = p - c;
    const float d5 = ab.dot( cp ), d6 = ac.dot( cp );
    if ( d6 >= 0.0f && d5 <= d6 ) return c;
    const float vb = d5 * d2 - d1 * d6;
    if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f ) return a + ac * ( d2 / ( d2 - d6 ) );
    const float va = d3 * d6 - d5 * d4;
    if ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f ) {
        return b + ( c - b ) * ( ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) ) );
    }
    const float denom = 1.0f / ( va + vb + vc );
    return a + ab * ( vb * denom ) + ac * ( vc * denom );
}

Vector3f ClosestPointOnSegment( const Vector3f &p, const Vector3f &a, const Vector3f &b ) {
    const Vector3f ab = b - a;
    const float len2 = ab.dot( ab );
    const float t = len2 > 1e-12f ? Saturate( ( p - a ).dot( ab ) / len2 ) : 0.0f;
    return a + ab * t;
}

bool RaySphere( const Vector3f &o, const Vector3f &d, const Vector3f &c, float r, float &t ) {
    const Vector3f m = o - c;
    const float a = d.dot( d );
    const float b = m.dot( d );
    const float cc = m.dot( m ) - r * r;
    if ( cc > 0.0f && b > 0.0f ) return false;
    const float disc = b * b - a * cc;
    if ( disc < 0.0f || a < 1e-12f ) return false;
    t = Max( ( -b - std::sqrt( disc ) ) / a, 0.0f );
    return true;
}

// Ray vs capsule: infinite cylinder clipped to the segment, then end spheres.
bool RayCapsule( const Vector3f &o, const Vector3f &d, const HavokCollision::Capsule &cap,
                 float &outT, Vector3f &outN ) {
    bool bHit = false;
    float best = FLT_MAX;
    const Vector3f axis = cap.B - cap.A;
    const float axisLen2 = axis.dot( axis );
    if ( axisLen2 > 1e-12f ) {
        const Vector3f m = o - cap.A;
        const float md = m.dot( axis ) / axisLen2, dd = d.dot( axis ) / axisLen2;
        const Vector3f mp = m - axis * md, dp = d - axis * dd; // perpendicular parts
        const float a = dp.dot( dp ), b = mp.dot( dp ), c = mp.dot( mp ) - cap.Radius * cap.Radius;
        const float disc = b * b - a * c;
        if ( a > 1e-12f && disc >= 0.0f ) {
            float t = ( -b - std::sqrt( disc ) ) / a;
            if ( c <= 0.0f ) t = 0.0f; // starts inside the infinite cylinder
            const float s = md + dd * t;
            if ( t >= 0.0f && s >= 0.0f && s <= 1.0f ) {
                best = t;
                bHit = true;
                const Vector3f p = o + d * t;
                outN = ( p - ( cap.A + axis * s ) ).normalized();
            }
        }
    }
    const Vector3f ends[2] = { cap.A, cap.B };
    for ( const Vector3f &e : ends ) {
        float t;
        if ( RaySphere( o, d, e, cap.Radius, t ) && t < best ) {
            best = t;
            bHit = true;
            outN = ( o + d * t - e ).normalized();
        }
    }
    outT = best;
    return bHit;
}

bool RayBox( const Vector3f &o, const Vector3f &d, const HavokCollision::Box &box, float &outT,
             Vector3f &outN ) {
    const Vector3f rel = o - box.Transform.getTranslation();
    float tMin = 0.0f, tMax = FLT_MAX;
    int hitAxis = -1;
    float hitSign = 1.0f;
    for ( int i = 0; i < 3; i++ ) {
        const Vector3f axis = BoxAxis( box, i );
        const float e = ( &box.HalfExtents.x )[i] + box.ConvexRadius;
        const float lo = rel.dot( axis ), ld = d.dot( axis );
        if ( std::fabs( ld ) < 1e-12f ) {
            if ( lo < -e || lo > e ) return false;
            continue;
        }
        float t1 = ( -e - lo ) / ld, t2 = ( e - lo ) / ld;
        float sign = -1.0f;
        if ( t1 > t2 ) {
            std::swap( t1, t2 );
            sign = 1.0f;
        }
        if ( t1 > tMin ) {
            tMin = t1;
            hitAxis = i;
            hitSign = sign;
        }
        tMax = Min( tMax, t2 );
        if ( tMin > tMax ) return false;
    }
    outT = tMin;
    outN = hitAxis >= 0 ? BoxAxis( box, hitAxis ) * hitSign : -d.normalized();
    return true;
}
} // namespace

void HavokCollision::clear() {
    Vertices.clear();
    Indices.clear();
    Boxes.clear();
    Capsules.clear();
    Bodies.clear();
    Hinges.clear();
    BoundsMin = Vector3f( 0, 0, 0 );
    BoundsMax = Vector3f( 0, 0, 0 );
}

bool LoadHavokCollision( const uint8_t *pData, size_t size, HavokCollision &out, bool bStaticOnly ) {
    out.clear();
    if ( pData == nullptr || size < kEdenHeaderSize ) {
        return false;
    }

    // Eden header: 02 02, u16 entry count, ..., u32 @0x18 = packfile size; the
    // packfile ends the file. Fall back to a magic scan past the entry table.
    size_t packOffset = SIZE_MAX;
    if ( pData[0] == 0x02 && pData[1] == 0x02 ) {
        const uint32_t packSize = ReadAt<uint32_t>( pData, 0x18 );
        if ( packSize >= kPackfileHeaderSize && packSize <= size &&
             memcmp( pData + size - packSize, kPackfileMagic, 8 ) == 0 ) {
            packOffset = size - packSize;
        }
    }
    if ( packOffset == SIZE_MAX ) {
        for ( size_t o = 0; o + 8 <= size; o += 0x10 ) {
            if ( memcmp( pData + o, kPackfileMagic, 8 ) == 0 ) {
                packOffset = o;
                break;
            }
        }
    }
    if ( packOffset == SIZE_MAX ) {
        OTDU_LOG_WARN( "Havok: packfile magic not found\n" );
        return false;
    }

    Packfile pack;
    if ( !ParsePackfile( pData + packOffset, size - packOffset, pack ) ) {
        return false;
    }

    std::vector<uint32_t> systems;
    for ( const auto &it : pack.classes ) {
        if ( it.second == "hkpPhysicsSystem" ) {
            systems.push_back( it.first );
        }
    }
    std::sort( systems.begin(), systems.end() );

    Builder builder{ pack, out };
    std::unordered_map<uint32_t, uint32_t> bodyIndexFromOffset;
    for ( uint32_t system : systems ) {
        if ( !pack.valid( system, 0x44 ) ) {
            continue;
        }
        uint32_t bodies = 0, numBodies = 0;
        if ( !pack.array( system + kPhysSysRigidBodies, 4, bodies, numBodies ) ) {
            continue;
        }
        for ( uint32_t i = 0; i < numBodies; i++ ) {
            const uint32_t body = pack.ptr( bodies + i * 4 );
            if ( body == UINT32_MAX || !pack.valid( body, 0x200 ) || !pack.is( body, "hkpRigidBody" ) ) {
                continue;
            }
            HavokCollision::Body entry;
            entry.Transform = pack.transform( body + kBodyTransform );
            entry.MotionType = pack.pData[body + kBodyMotionType];
            entry.Friction = pack.f32( body + kBodyFriction );
            if ( bStaticOnly && entry.MotionType != kMotionFixed ) {
                continue;
            }
            entry.FirstTriangle = static_cast<uint32_t>( out.Indices.size() / 3 );
            entry.FirstBox = static_cast<uint32_t>( out.Boxes.size() );
            entry.FirstCapsule = static_cast<uint32_t>( out.Capsules.size() );
            builder.bodyIndex = static_cast<uint32_t>( out.Bodies.size() );
            builder.addShape( pack.ptr( body + kBodyShape ), entry.Transform, 0 );
            entry.NumTriangles = static_cast<uint32_t>( out.Indices.size() / 3 ) - entry.FirstTriangle;
            entry.NumBoxes = static_cast<uint32_t>( out.Boxes.size() ) - entry.FirstBox;
            entry.NumCapsules = static_cast<uint32_t>( out.Capsules.size() ) - entry.FirstCapsule;
            bodyIndexFromOffset[body] = builder.bodyIndex;
            out.Bodies.push_back( entry );
        }

        uint32_t constraints = 0, numConstraints = 0;
        if ( !pack.array( system + kPhysSysConstraints, 4, constraints, numConstraints ) ) {
            continue;
        }
        for ( uint32_t i = 0; i < numConstraints; i++ ) {
            const uint32_t instance = pack.ptr( constraints + i * 4 );
            if ( instance == UINT32_MAX || !pack.valid( instance, 0x2c ) ) {
                continue;
            }
            const uint32_t data = pack.ptr( instance + kConstraintData );
            if ( data == UINT32_MAX || !pack.valid( data, 0xe0 ) ||
                 !pack.is( data, "hkpLimitedHingeConstraintData" ) ) {
                continue;
            }
            auto itA = bodyIndexFromOffset.find( pack.ptr( instance + kConstraintEntityA ) );
            auto itB = bodyIndexFromOffset.find( pack.ptr( instance + kConstraintEntityB ) );
            if ( itA == bodyIndexFromOffset.end() || itB == bodyIndexFromOffset.end() ) {
                continue;
            }
            HavokCollision::Hinge hinge;
            hinge.BodyA = itA->second;
            hinge.BodyB = itB->second;
            hinge.AxisA = pack.vec3( data + kHingeTransformA );
            hinge.PivotA = pack.vec3( data + kHingeTransformA + 0x30 );
            hinge.AxisB = pack.vec3( data + kHingeTransformB );
            hinge.PivotB = pack.vec3( data + kHingeTransformB + 0x30 );
            hinge.LimitEnabled = pack.pData[data + kHingeAngLimit + 2] != 0;
            hinge.MinAngle = pack.f32( data + kHingeAngLimit + 4 );
            hinge.MaxAngle = pack.f32( data + kHingeAngLimit + 8 );
            out.Hinges.push_back( hinge );
        }
    }

    if ( out.Bodies.empty() ) {
        OTDU_LOG_WARN( "Havok: no rigid body found\n" );
        return false;
    }

    out.BoundsMin = Vector3f( FLT_MAX, FLT_MAX, FLT_MAX );
    out.BoundsMax = Vector3f( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    for ( const Vector3f &v : out.Vertices ) {
        ExpandBounds( out, v );
    }
    for ( const HavokCollision::Box &box : out.Boxes ) {
        for ( int corner = 0; corner < 8; corner++ ) {
            const Vector3f local( ( corner & 1 ) ? box.HalfExtents.x : -box.HalfExtents.x,
                                  ( corner & 2 ) ? box.HalfExtents.y : -box.HalfExtents.y,
                                  ( corner & 4 ) ? box.HalfExtents.z : -box.HalfExtents.z );
            ExpandBounds( out, box.Transform.transformPoint( local ) );
        }
    }
    for ( const HavokCollision::Capsule &cap : out.Capsules ) {
        const Vector3f r( cap.Radius, cap.Radius, cap.Radius );
        ExpandBounds( out, cap.A - r );
        ExpandBounds( out, cap.A + r );
        ExpandBounds( out, cap.B - r );
        ExpandBounds( out, cap.B + r );
    }

    OTDU_LOG_DEBUG( "Havok: %zu bodies, %zu triangles, %zu boxes, %zu capsules, %zu hinges\n",
                    out.Bodies.size(), out.getNumTriangles(), out.Boxes.size(), out.Capsules.size(),
                    out.Hinges.size() );
    return true;
}

bool RaycastHavokCollision( const HavokCollision &collision, const Vector3f &origin,
                            const Vector3f &dir, float maxDist, float &outT, Vector3f *pOutNormal ) {
    float best = maxDist;
    bool bHit = false;
    Vector3f bestNormal( 0.0f, 1.0f, 0.0f );

    // Triangles (Moller-Trumbore, two-sided).
    for ( size_t i = 0; i + 2 < collision.Indices.size(); i += 3 ) {
        const Vector3f &a = collision.Vertices[collision.Indices[i]];
        const Vector3f &b = collision.Vertices[collision.Indices[i + 1]];
        const Vector3f &c = collision.Vertices[collision.Indices[i + 2]];
        const Vector3f e1 = b - a, e2 = c - a;
        const Vector3f pv = dir.cross( e2 );
        const float det = e1.dot( pv );
        if ( std::fabs( det ) < 1e-12f ) continue;
        const float inv = 1.0f / det;
        const Vector3f tv = origin - a;
        const float u = tv.dot( pv ) * inv;
        if ( u < 0.0f || u > 1.0f ) continue;
        const Vector3f qv = tv.cross( e1 );
        const float v = dir.dot( qv ) * inv;
        if ( v < 0.0f || u + v > 1.0f ) continue;
        const float t = e2.dot( qv ) * inv;
        if ( t >= 0.0f && t < best ) {
            best = t;
            bHit = true;
            Vector3f n = e1.cross( e2 ).normalized();
            bestNormal = n.dot( dir ) > 0.0f ? -n : n;
        }
    }
    for ( const HavokCollision::Box &box : collision.Boxes ) {
        float t;
        Vector3f n;
        if ( RayBox( origin, dir, box, t, n ) && t < best ) {
            best = t;
            bHit = true;
            bestNormal = n;
        }
    }
    for ( const HavokCollision::Capsule &cap : collision.Capsules ) {
        float t;
        Vector3f n;
        if ( RayCapsule( origin, dir, cap, t, n ) && t < best ) {
            best = t;
            bHit = true;
            bestNormal = n;
        }
    }
    if ( bHit ) {
        outT = best;
        if ( pOutNormal != nullptr ) {
            *pOutNormal = bestNormal;
        }
    }
    return bHit;
}

bool FindHavokFloor( const HavokCollision &collision, const Vector3f &pos, float maxStepUp,
                     float maxDrop, float &outFloorY, Vector3f *pOutNormal ) {
    const Vector3f start( pos.x, pos.y + maxStepUp, pos.z );
    float t = 0.0f;
    if ( !RaycastHavokCollision( collision, start, Vector3f( 0.0f, -1.0f, 0.0f ), maxStepUp + maxDrop,
                                 t, pOutNormal ) ) {
        return false;
    }
    outFloorY = start.y - t;
    return true;
}

Vector3f ResolveSphereHavokCollision( const HavokCollision &collision, Vector3f center, float radius,
                                      uint32_t iterations ) {
    const float r2 = radius * radius;
    for ( uint32_t iter = 0; iter < iterations; iter++ ) {
        bool bMoved = false;
        auto pushFrom = [&]( const Vector3f &closest, const Vector3f &fallbackNormal ) {
            const Vector3f delta = center - closest;
            const float d2 = delta.dot( delta );
            if ( d2 >= r2 ) return;
            const float d = std::sqrt( d2 );
            const Vector3f n = d > 1e-6f ? delta * ( 1.0f / d ) : fallbackNormal;
            center += n * ( radius - d );
            bMoved = true;
        };

        for ( size_t i = 0; i + 2 < collision.Indices.size(); i += 3 ) {
            const Vector3f &a = collision.Vertices[collision.Indices[i]];
            const Vector3f &b = collision.Vertices[collision.Indices[i + 1]];
            const Vector3f &c = collision.Vertices[collision.Indices[i + 2]];
            const Vector3f closest = ClosestPointOnTriangle( center, a, b, c );
            if ( ( center - closest ).dot( center - closest ) < r2 ) {
                Vector3f n = ( b - a ).cross( c - a ).normalized();
                if ( n.dot( center - a ) < 0.0f ) n = -n;
                pushFrom( closest, n );
            }
        }

        for ( const HavokCollision::Box &box : collision.Boxes ) {
            const Vector3f rel = center - box.Transform.getTranslation();
            float local[3], ext[3];
            bool bInside = true;
            for ( int k = 0; k < 3; k++ ) {
                local[k] = rel.dot( BoxAxis( box, k ) );
                ext[k] = ( &box.HalfExtents.x )[k] + box.ConvexRadius;
                if ( std::fabs( local[k] ) > ext[k] ) bInside = false;
            }
            if ( bInside ) {
                // Centre inside the box: exit through the closest face.
                int axis = 0;
                float minPen = FLT_MAX;
                for ( int k = 0; k < 3; k++ ) {
                    const float pen = ext[k] - std::fabs( local[k] );
                    if ( pen < minPen ) {
                        minPen = pen;
                        axis = k;
                    }
                }
                const float sign = local[axis] >= 0.0f ? 1.0f : -1.0f;
                center += BoxAxis( box, axis ) * ( sign * ( minPen + radius ) );
                bMoved = true;
                continue;
            }
            Vector3f closest = box.Transform.getTranslation();
            for ( int k = 0; k < 3; k++ ) {
                closest += BoxAxis( box, k ) * Clamp( local[k], -ext[k], ext[k] );
            }
            pushFrom( closest, Vector3f( 0.0f, 1.0f, 0.0f ) );
        }

        for ( const HavokCollision::Capsule &cap : collision.Capsules ) {
            const Vector3f onAxis = ClosestPointOnSegment( center, cap.A, cap.B );
            const Vector3f delta = center - onAxis;
            const float d = delta.length();
            const float minDist = cap.Radius + radius;
            if ( d < minDist ) {
                const Vector3f n = d > 1e-6f ? delta * ( 1.0f / d ) : Vector3f( 1.0f, 0.0f, 0.0f );
                center += n * ( minDist - d );
                bMoved = true;
            }
        }

        if ( !bMoved ) {
            break;
        }
    }
    return center;
}
