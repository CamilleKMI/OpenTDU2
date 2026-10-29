#include "shared.h"
#include "vehicle_collision.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "physics/havok_collision.h"
#include "world/pmi.h"

namespace {
constexpr float kSectorSize = 768.0f; // worldconfig.cpr / FUN_00990020 (EdenWorld)
constexpr int32_t kSectorsPerArea = 8;
// Terrain (heightfield) friction: not in the sector data [INF: the road value].
constexpr float kTerrainFriction = 0.7f;

Vector3f ClosestPointOnTriangle( const Vector3f &p, const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
    // Ericson, Real-Time Collision Detection 5.1.5.
    const Vector3f ab = b - a, ac = c - a, ap = p - a;
    const float d1 = ab.dot( ap ), d2 = ac.dot( ap );
    if ( d1 <= 0.0f && d2 <= 0.0f ) {
        return a;
    }
    const Vector3f bp = p - b;
    const float d3 = ab.dot( bp ), d4 = ac.dot( bp );
    if ( d3 >= 0.0f && d4 <= d3 ) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f ) {
        return a + ab * ( d1 / ( d1 - d3 ) );
    }
    const Vector3f cp = p - c;
    const float d5 = ab.dot( cp ), d6 = ac.dot( cp );
    if ( d6 >= 0.0f && d5 <= d6 ) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f ) {
        return a + ac * ( d2 / ( d2 - d6 ) );
    }
    const float va = d3 * d6 - d5 * d4;
    if ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f ) {
        return b + ( c - b ) * ( ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) ) );
    }
    const float denominator = 1.0f / ( va + vb + vc );
    return a + ab * ( vb * denominator ) + ac * ( vc * denominator );
}

Vector3f ClosestPointOnSegment( const Vector3f &p, const Vector3f &a, const Vector3f &b ) {
    const Vector3f ab = b - a;
    const float lengthSquared = ab.dot( ab );
    const float t = lengthSquared > 1e-8f ? Clamp( ( p - a ).dot( ab ) / lengthSquared, 0.0f, 1.0f ) : 0.0f;
    return a + ab * t;
}
} // namespace

VehicleWorldCollision::VehicleWorldCollision() { setIsland( "ibiza" ); }

VehicleWorldCollision::~VehicleWorldCollision() {}

bool VehicleWorldCollision::loadLibrary( const Bank &commonBank ) {
    const BankEntry *pEntry = commonBank.findEntry( "Library.dhk" );
    if ( pEntry == nullptr || pEntry->Size < 0x40 ) {
        return false;
    }
    library = std::make_unique<HavokCollision>();
    if ( !LoadHavokCollision( pEntry->pData, pEntry->Size, *library, false ) ) {
        library.reset();
        return false;
    }
    const uint8_t *p = pEntry->pData;
    uint16_t numEntries = 0;
    uint32_t extraSize = 0;
    memcpy( &numEntries, p + 2, 2 );
    memcpy( &extraSize, p + 8, 4 );
    // Models: type 1 entries (library node hash), index - numModels.
    std::vector<uint64_t> modelHashes;
    uint32_t numModels = 0;
    for ( uint32_t i = 0; i < numEntries; i++ ) {
        uint32_t type = 0;
        memcpy( &type, p + 0x20 + i * 0x20 + 0xc, 4 );
        numModels += type == 0 ? 1 : 0;
    }
    modelHashes.assign( numModels, 0 );
    for ( uint32_t i = 0; i < numEntries; i++ ) {
        uint64_t hash = 0;
        uint32_t index = 0, type = 0;
        memcpy( &hash, p + 0x20 + i * 0x20, 8 );
        memcpy( &index, p + 0x20 + i * 0x20 + 8, 4 );
        memcpy( &type, p + 0x20 + i * 0x20 + 0xc, 4 );
        if ( type == 1 && index >= numModels && index - numModels < numModels ) {
            modelHashes[index - numModels] = hash;
        }
    }
    // Extra block: per model record, bodies in order.
    size_t offset = 0x20 + static_cast<size_t>( numEntries ) * 0x20;
    const size_t end = Min( offset + extraSize, static_cast<size_t>( pEntry->Size ) );
    uint32_t firstBody = 0;
    while ( offset + 0x20 <= end ) {
        uint32_t model = 0, numBodies = 0;
        memcpy( &model, p + offset, 4 );
        memcpy( &numBodies, p + offset + 0x10, 4 );
        if ( model >= numModels || numBodies > 64 ) {
            break;
        }
        if ( modelHashes[model] != 0 ) {
            libraryModels[modelHashes[model]] = std::make_pair( firstBody, numBodies );
        }
        for ( uint32_t b = 0; b < numBodies; b++ ) {
            // hkTransform: rotation columns (3 x vec4) then translation.
            float values[16];
            memcpy( values, p + offset + 0x20 + b * 0x80, sizeof( values ) );
            Matrix4x4f pose = Matrix4x4f::Identity();
            for ( int column = 0; column < 4; column++ ) {
                for ( int row = 0; row < 3; row++ ) {
                    pose.m[column][row] = values[column * 4 + row];
                }
            }
            const uint32_t body = firstBody + b;
            const Matrix4x4f packed = body < library->Bodies.size() ? library->Bodies[body].Transform : Matrix4x4f::Identity();
            libraryBodyCorrections.push_back( pose * packed.inverse() );
        }
        firstBody += numBodies;
        offset += 0x20 + static_cast<size_t>( numBodies ) * 0x80;
    }
    OTDU_LOG_INFO( "VehicleWorldCollision: Library.dhk %zu bodies, %u models, %zu placed models (%u bodies mapped)\n",
                   library->Bodies.size(), numModels, libraryModels.size(), firstBody );
    return true;
}

void VehicleWorldCollision::addLibraryInstances( const Bank &sectorBank, const std::string &name,
                                                 const Vector3f &sectorOrigin ) {
    if ( !library ) {
        return;
    }
    size_t added = 0;
    for ( const char *pSuffix : { "-v", "-o" } ) {
        const BankEntry *pEntry = sectorBank.findEntry( ( name + pSuffix ).c_str(), "pmi" );
        std::vector<PmiInstance> instances;
        if ( pEntry == nullptr || !ParsePmi( pEntry->pData, pEntry->Size, sectorOrigin, instances ) ) {
            continue;
        }
        for ( const PmiInstance &instance : instances ) {
            auto it = libraryModels.find( instance.Hashcode );
            if ( it == libraryModels.end() ) {
                continue;
            }
            HavokCollision subset;
            for ( uint32_t b = it->second.first; b < it->second.first + it->second.second && b < library->Bodies.size(); b++ ) {
                const HavokCollision::Body &body = library->Bodies[b];
                const Matrix4x4f correction =
                    b < libraryBodyCorrections.size() ? libraryBodyCorrections[b] : Matrix4x4f::Identity();
                HavokCollision::Body copy = body;
                copy.FirstTriangle = static_cast<uint32_t>( subset.Indices.size() / 3 );
                for ( uint32_t t = body.FirstTriangle; t < body.FirstTriangle + body.NumTriangles; t++ ) {
                    for ( int k = 0; k < 3; k++ ) {
                        subset.Indices.push_back( static_cast<uint32_t>( subset.Vertices.size() ) );
                        subset.Vertices.push_back( correction.transformPoint( library->Vertices[library->Indices[t * 3 + k]] ) );
                    }
                }
                for ( uint32_t x = body.FirstBox; x < body.FirstBox + body.NumBoxes; x++ ) {
                    HavokCollision::Box box = library->Boxes[x];
                    box.Transform = correction * box.Transform;
                    box.BodyIndex = static_cast<uint32_t>( subset.Bodies.size() );
                    subset.Boxes.push_back( box );
                }
                for ( uint32_t x = body.FirstCapsule; x < body.FirstCapsule + body.NumCapsules; x++ ) {
                    HavokCollision::Capsule capsule = library->Capsules[x];
                    capsule.A = correction.transformPoint( capsule.A );
                    capsule.B = correction.transformPoint( capsule.B );
                    subset.Capsules.push_back( capsule );
                }
                subset.Bodies.push_back( copy );
            }
            static const char *pProbe = getenv( "OTDU_COLLISION_PROBE" ); // "x,z": log the instances near
            if ( pProbe != nullptr ) {
                float px = 0.0f, pz = 0.0f;
                sscanf( pProbe, "%f,%f", &px, &pz );
                if ( std::abs( instance.Position.x - px ) < 6.0f && std::abs( instance.Position.z - pz ) < 6.0f ) {
                    char hashName[9] = {};
                    memcpy( hashName, &instance.Hashcode, 8 );
                    OTDU_LOG_INFO( "Collision probe: %s '%s' %016llx at (%.2f %.2f %.2f) bodies %u: %zu tris %zu boxes %zu caps\n",
                                   name.c_str(), hashName, static_cast<unsigned long long>( instance.Hashcode ),
                                   instance.Position.x, instance.Position.y, instance.Position.z, it->second.second,
                                   subset.Indices.size() / 3, subset.Boxes.size(), subset.Capsules.size() );
                    for ( const HavokCollision::Box &box : subset.Boxes ) {
                        const Vector3f c = ( instance.World * box.Transform ).getTranslation();
                        const Vector3f l = box.Transform.getTranslation();
                        const Matrix4x4f &w = instance.World;
                        OTDU_LOG_INFO( "   box centre (%.2f %.2f %.2f) local (%.2f %.2f %.2f) half (%.2f %.2f %.2f) world rows "
                                       "[%.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f]\n",
                                       c.x, c.y, c.z, l.x, l.y, l.z, box.HalfExtents.x, box.HalfExtents.y, box.HalfExtents.z,
                                       w.m[0][0], w.m[0][1], w.m[0][2], w.m[1][0], w.m[1][1], w.m[1][2], w.m[2][0], w.m[2][1],
                                       w.m[2][2], w.m[3][0], w.m[3][1], w.m[3][2] );
                    }
                }
            }
            addCollision( subset, instance.World, false );
            added++;
        }
    }
    if ( added > 0 ) {
        OTDU_LOG_INFO( "VehicleWorldCollision: %s: %zu library instances\n", name.c_str(), added );
    }
}

void VehicleWorldCollision::setIsland( const char *pIsland ) {
    islandFolder = std::string( "islands/" ) + pIsland + "/level/";
}

void VehicleWorldCollision::clear() {
    triangles.clear();
    boxes.clear();
    capsules.clear();
    triangleCells.clear();
    boxCells.clear();
    capsuleCells.clear();
    loadedSectors.clear();
}

void VehicleWorldCollision::addCells( std::unordered_map<uint64_t, std::vector<uint32_t>> &cells,
                                      const Vector3f &boundsMin, const Vector3f &boundsMax, uint32_t index ) {
    const int32_t x0 = static_cast<int32_t>( std::floor( boundsMin.x / kCellSize ) );
    const int32_t x1 = static_cast<int32_t>( std::floor( boundsMax.x / kCellSize ) );
    const int32_t z0 = static_cast<int32_t>( std::floor( boundsMin.z / kCellSize ) );
    const int32_t z1 = static_cast<int32_t>( std::floor( boundsMax.z / kCellSize ) );
    // Huge primitives (sea level planes...): capped, they are still found
    // from the cells they cover near their centre.
    if ( ( x1 - x0 ) > 256 || ( z1 - z0 ) > 256 ) {
        return;
    }
    for ( int32_t z = z0; z <= z1; z++ ) {
        for ( int32_t x = x0; x <= x1; x++ ) {
            cells[CellKey( x, z )].push_back( index );
        }
    }
}

void VehicleWorldCollision::streamAround( const Vector3f &position, int32_t sectorRadius ) {
    // EdenWorld::getSectorAt: rows of 768 m, odd rows shifted by 384 m.
    const int32_t row = static_cast<int32_t>( std::floor( position.z / kSectorSize ) );
    const float offset = ( row & 1 ) ? kSectorSize * 0.5f : 0.0f;
    const int32_t column = static_cast<int32_t>( std::floor( ( position.x - offset ) / kSectorSize ) );
    for ( int32_t r = row - sectorRadius; r <= row + sectorRadius; r++ ) {
        for ( int32_t c = column - sectorRadius; c <= column + sectorRadius; c++ ) {
            if ( r < 0 || c < 0 ) {
                continue;
            }
            const uint32_t key = static_cast<uint32_t>( r * 1024 + c );
            if ( loadedSectors.insert( key ).second ) {
                loadSector( r, c );
            }
        }
    }
}

void VehicleWorldCollision::loadSector( int32_t row, int32_t column ) {
    const int32_t areaX = column / kSectorsPerArea, areaY = row / kSectorsPerArea;
    const int32_t sectorX = column % kSectorsPerArea, sectorY = row % kSectorsPerArea;
    char name[64];
    snprintf( name, sizeof( name ), "sector-%d-%d-%d-%d", areaX, areaY, sectorX, sectorY );
    char path[256];
    snprintf( path, sizeof( path ), "%s%sarea-%d/area-%d-%d/%s.bnk", gpFile->getBankFolder().c_str(),
              islandFolder.c_str(), areaX, areaX, areaY, name );
    if ( !gpFile->fileExists( path ) ) {
        return;
    }
    Bank bank;
    if ( !bank.load( path ) ) {
        return;
    }
    // Area-local Havok frame -> world.
    const Vector3f areaOrigin( areaX * kSectorsPerArea * kSectorSize, 0.0f, areaY * kSectorsPerArea * kSectorSize );
    const size_t firstTriangle = triangles.size();
    const size_t firstBox = boxes.size();
    const size_t firstCapsule = capsules.size();
    for ( const BankEntry *pEntry : bank.findEntriesByExtension( "shk" ) ) {
        const bool bRoad = pEntry->Name.size() > 2 && ( pEntry->Name.compare( pEntry->Name.size() - 2, 2, "-R" ) == 0 ||
                                                        pEntry->Name.compare( pEntry->Name.size() - 2, 2, "-r" ) == 0 );
        HavokCollision collision;
        if ( !LoadHavokCollision( pEntry->pData, pEntry->Size, collision, true ) ) {
            OTDU_LOG_WARN( "VehicleWorldCollision: '%s' not readable\n", pEntry->Filename.c_str() );
            continue;
        }
        addCollision( collision, Matrix4x4f::Translation( areaOrigin ), bRoad, bRoad );
    }
    // Sector origin (EdenWorld::getSectorOrigin: odd rows shifted).
    const Vector3f sectorOrigin( ( areaX * kSectorsPerArea + sectorX ) * kSectorSize + ( ( sectorY & 1 ) ? kSectorSize * 0.5f : 0.0f ),
                                 0.0f, ( areaY * kSectorsPerArea + sectorY ) * kSectorSize );
    addLibraryInstances( bank, name, sectorOrigin );
    OTDU_LOG_INFO( "VehicleWorldCollision: %s: %zu triangles, %zu boxes, %zu capsules\n", name,
                   triangles.size() - firstTriangle, boxes.size() - firstBox, capsules.size() - firstCapsule );
}

void VehicleWorldCollision::addCollision( const HavokCollision &collision, const Matrix4x4f &toWorld, bool bRoad,
                                          bool bSectorRoad ) {
    for ( const HavokCollision::Body &body : collision.Bodies ) {
        for ( uint32_t t = body.FirstTriangle; t < body.FirstTriangle + body.NumTriangles; t++ ) {
            Triangle triangle;
            triangle.A = toWorld.transformPoint( collision.Vertices[collision.Indices[t * 3 + 0]] );
            triangle.B = toWorld.transformPoint( collision.Vertices[collision.Indices[t * 3 + 1]] );
            triangle.C = toWorld.transformPoint( collision.Vertices[collision.Indices[t * 3 + 2]] );
            triangle.Normal = ( triangle.B - triangle.A ).cross( triangle.C - triangle.A ).normalized();
            if ( triangle.Normal.length() < 0.5f ) {
                continue; // degenerate
            }
            // Two-sided: the up facing side.
            if ( triangle.Normal.y < 0.0f ) {
                triangle.Normal = -triangle.Normal;
            }
            triangle.Friction = body.Friction;
            triangle.bRoad = bRoad;
            triangle.bSectorRoad = bSectorRoad;
            const uint32_t index = static_cast<uint32_t>( triangles.size() );
            triangles.push_back( triangle );
            const Vector3f boundsMin( Min( triangle.A.x, Min( triangle.B.x, triangle.C.x ) ), 0.0f,
                                      Min( triangle.A.z, Min( triangle.B.z, triangle.C.z ) ) );
            const Vector3f boundsMax( Max( triangle.A.x, Max( triangle.B.x, triangle.C.x ) ), 0.0f,
                                      Max( triangle.A.z, Max( triangle.B.z, triangle.C.z ) ) );
            addCells( triangleCells, boundsMin, boundsMax, index );
        }
    }
    for ( const HavokCollision::Box &source : collision.Boxes ) {
        Box box;
        box.Transform = toWorld * source.Transform;
        box.Inverse = box.Transform.inverse();
        box.HalfExtents = source.HalfExtents + Vector3f( source.ConvexRadius, source.ConvexRadius, source.ConvexRadius );
        box.Friction = source.BodyIndex < collision.Bodies.size() ? collision.Bodies[source.BodyIndex].Friction : 0.7f;
        const uint32_t index = static_cast<uint32_t>( boxes.size() );
        boxes.push_back( box );
        const float radius = box.HalfExtents.length();
        const Vector3f center = box.Transform.getTranslation();
        addCells( boxCells, center - Vector3f( radius, 0, radius ), center + Vector3f( radius, 0, radius ), index );
    }
    for ( const HavokCollision::Capsule &source : collision.Capsules ) {
        Capsule capsule;
        capsule.A = toWorld.transformPoint( source.A );
        capsule.B = toWorld.transformPoint( source.B );
        capsule.Radius = source.Radius;
        const uint32_t index = static_cast<uint32_t>( capsules.size() );
        capsules.push_back( capsule );
        const Vector3f boundsMin( Min( capsule.A.x, capsule.B.x ) - capsule.Radius, 0.0f,
                                  Min( capsule.A.z, capsule.B.z ) - capsule.Radius );
        const Vector3f boundsMax( Max( capsule.A.x, capsule.B.x ) + capsule.Radius, 0.0f,
                                  Max( capsule.A.z, capsule.B.z ) + capsule.Radius );
        addCells( capsuleCells, boundsMin, boundsMax, index );
    }
}

bool VehicleWorldCollision::raycast( const Vector3f &origin, const Vector3f &dir, float maxDistance,
                                     Hit &outHit ) const {
    if ( bFlatGround ) {
        if ( dir.y >= -1e-6f || origin.y < flatHeight ) {
            return false;
        }
        const float distance = ( origin.y - flatHeight ) / -dir.y;
        if ( distance > maxDistance ) {
            return false;
        }
        outHit.Distance = distance;
        outHit.Position = origin + dir * distance;
        outHit.Normal = Vector3f( 0.0f, 1.0f, 0.0f );
        outHit.Friction = flatFriction;
        outHit.Surface = Hit::eSurface::Road;
        return true;
    }
    bool bHit = false;
    float best = maxDistance;
    const Vector3f end = origin + dir * maxDistance;
    const int32_t x0 = static_cast<int32_t>( std::floor( Min( origin.x, end.x ) / kCellSize ) );
    const int32_t x1 = static_cast<int32_t>( std::floor( Max( origin.x, end.x ) / kCellSize ) );
    const int32_t z0 = static_cast<int32_t>( std::floor( Min( origin.z, end.z ) / kCellSize ) );
    const int32_t z1 = static_cast<int32_t>( std::floor( Max( origin.z, end.z ) / kCellSize ) );
    if ( ( x1 - x0 ) <= 64 && ( z1 - z0 ) <= 64 ) {
        for ( int32_t z = z0; z <= z1; z++ ) {
            for ( int32_t x = x0; x <= x1; x++ ) {
                auto it = triangleCells.find( CellKey( x, z ) );
                if ( it == triangleCells.end() ) {
                    continue;
                }
                for ( uint32_t index : it->second ) {
                    // Moller-Trumbore, two-sided.
                    const Triangle &triangle = triangles[index];
                    const Vector3f e1 = triangle.B - triangle.A, e2 = triangle.C - triangle.A;
                    const Vector3f p = dir.cross( e2 );
                    const float det = e1.dot( p );
                    if ( std::abs( det ) < 1e-9f ) {
                        continue;
                    }
                    const float inverse = 1.0f / det;
                    const Vector3f s = origin - triangle.A;
                    const float u = s.dot( p ) * inverse;
                    if ( u < 0.0f || u > 1.0f ) {
                        continue;
                    }
                    const Vector3f q = s.cross( e1 );
                    const float v = dir.dot( q ) * inverse;
                    if ( v < 0.0f || u + v > 1.0f ) {
                        continue;
                    }
                    const float t = e2.dot( q ) * inverse;
                    if ( t >= 0.0f && t < best ) {
                        best = t;
                        bHit = true;
                        outHit.Distance = t;
                        outHit.Normal = triangle.Normal;
                        outHit.Friction = triangle.Friction;
                        outHit.Surface = triangle.bRoad ? Hit::eSurface::Road : Hit::eSurface::Static;
                    }
                }
            }
        }
    }
    // Terrain: vertical rays only (wheels), from above the heightfield (the
    // road meshes of the villages can lie under it).
    if ( heightFunction && std::abs( dir.y ) > 0.7f ) {
        float height = 0.0f;
        if ( heightFunction( origin.x, origin.z, height ) ) {
            const float t = ( origin.y - height ) / -dir.y;
            if ( t >= 0.0f && t < best ) {
                best = t;
                bHit = true;
                outHit.Distance = t;
                // Normal from the heightfield gradient (1 m central differences).
                float hx0 = height, hx1 = height, hz0 = height, hz1 = height;
                heightFunction( origin.x - 1.0f, origin.z, hx0 );
                heightFunction( origin.x + 1.0f, origin.z, hx1 );
                heightFunction( origin.x, origin.z - 1.0f, hz0 );
                heightFunction( origin.x, origin.z + 1.0f, hz1 );
                outHit.Normal = Vector3f( ( hx0 - hx1 ) * 0.5f, 1.0f, ( hz0 - hz1 ) * 0.5f ).normalized();
                outHit.Friction = kTerrainFriction;
                outHit.Surface = Hit::eSurface::Terrain;
            }
        }
    }
    if ( bHit ) {
        outHit.Position = origin + dir * outHit.Distance;
    }
    return bHit;
}

bool VehicleWorldCollision::sphereContact( const Vector3f &center, float radius, Contact &outContact,
                                           const Vector3f *pPrevious ) const {
    if ( bFlatGround ) {
        (void)pPrevious;
        if ( center.y - radius >= flatHeight ) {
            return false;
        }
        outContact.Normal = Vector3f( 0.0f, 1.0f, 0.0f );
        outContact.Depth = flatHeight - ( center.y - radius );
        outContact.Point = Vector3f( center.x, flatHeight, center.z );
        return true;
    }
    bool bFound = false;
    float deepest = 0.0f;
    const int32_t x0 = static_cast<int32_t>( std::floor( ( center.x - radius ) / kCellSize ) );
    const int32_t x1 = static_cast<int32_t>( std::floor( ( center.x + radius ) / kCellSize ) );
    const int32_t z0 = static_cast<int32_t>( std::floor( ( center.z - radius ) / kCellSize ) );
    const int32_t z1 = static_cast<int32_t>( std::floor( ( center.z + radius ) / kCellSize ) );
    auto consider = [&]( const Vector3f &closest, const Vector3f &fallbackNormal, float signedDistance ) {
        const float depth = radius - signedDistance;
        if ( depth > deepest ) {
            deepest = depth;
            bFound = true;
            Vector3f normal = center - closest;
            const float length = normal.length();
            normal = length > 1e-5f ? normal * ( 1.0f / length ) : fallbackNormal;
            outContact.Normal = signedDistance < 0.0f ? fallbackNormal : normal;
            outContact.Depth = depth;
            outContact.Point = closest;
        }
    };
    // Terrain heightfield (approximately: vertical distance).
    if ( heightFunction ) {
        float height = 0.0f;
        if ( heightFunction( center.x, center.z, height ) && center.y - height < radius && center.y - height > -0.3f ) {
            consider( Vector3f( center.x, height, center.z ), Vector3f( 0.0f, 1.0f, 0.0f ), center.y - height );
        }
    }
    std::unordered_set<uint32_t> seen;
    for ( int32_t z = z0; z <= z1; z++ ) {
        for ( int32_t x = x0; x <= x1; x++ ) {
            auto it = triangleCells.find( CellKey( x, z ) );
            if ( it != triangleCells.end() ) {
                for ( uint32_t index : it->second ) {
                    if ( !seen.insert( index ).second ) {
                        continue;
                    }
                    const Triangle &triangle = triangles[index];
                    const Vector3f closest = ClosestPointOnTriangle( center, triangle.A, triangle.B, triangle.C );
                    const float distance = ( center - closest ).length();
                    if ( distance < radius ) {
                        consider( closest, triangle.Normal, distance );
                    }
                }
            }
            auto itBox = boxCells.find( CellKey( x, z ) );
            if ( itBox != boxCells.end() ) {
                for ( uint32_t index : itBox->second ) {
                    if ( !seen.insert( 0x80000000u | index ).second ) {
                        continue;
                    }
                    const Box &box = boxes[index];
                    const Vector3f local = box.Inverse.transformPoint( center );
                    const Vector3f clamped( Clamp( local.x, -box.HalfExtents.x, box.HalfExtents.x ),
                                            Clamp( local.y, -box.HalfExtents.y, box.HalfExtents.y ),
                                            Clamp( local.z, -box.HalfExtents.z, box.HalfExtents.z ) );
                    const Vector3f delta = local - clamped;
                    const float distance = delta.length();
                    if ( distance >= radius ) {
                        continue;
                    }
                    if ( distance > 1e-5f ) {
                        consider( box.Transform.transformPoint( clamped ), Vector3f( 0, 1, 0 ), distance );
                    } else {
                        // Centre inside: push out through the nearest face.
                        const float dx = box.HalfExtents.x - std::abs( local.x );
                        const float dy = box.HalfExtents.y - std::abs( local.y );
                        const float dz = box.HalfExtents.z - std::abs( local.z );
                        Vector3f axis( local.x < 0 ? -1.0f : 1.0f, 0, 0 );
                        float d = dx;
                        if ( dy < d ) {
                            d = dy;
                            axis = Vector3f( 0, local.y < 0 ? -1.0f : 1.0f, 0 );
                        }
                        if ( dz < d ) {
                            d = dz;
                            axis = Vector3f( 0, 0, local.z < 0 ? -1.0f : 1.0f );
                        }
                        if ( pPrevious != nullptr ) {
                            // Entry face: the axis along which the previous
                            // centre was the most outside.
                            const Vector3f previous = box.Inverse.transformPoint( *pPrevious );
                            const float ox = std::abs( previous.x ) - box.HalfExtents.x;
                            const float oy = std::abs( previous.y ) - box.HalfExtents.y;
                            const float oz = std::abs( previous.z ) - box.HalfExtents.z;
                            if ( ox > 0.0f || oy > 0.0f || oz > 0.0f ) {
                                if ( ox >= oy && ox >= oz ) {
                                    axis = Vector3f( previous.x < 0 ? -1.0f : 1.0f, 0, 0 );
                                    d = box.HalfExtents.x + std::abs( local.x ) * ( ( previous.x < 0 ) == ( local.x < 0 ) ? -1.0f : 1.0f );
                                } else if ( oy >= oz ) {
                                    axis = Vector3f( 0, previous.y < 0 ? -1.0f : 1.0f, 0 );
                                    d = box.HalfExtents.y + std::abs( local.y ) * ( ( previous.y < 0 ) == ( local.y < 0 ) ? -1.0f : 1.0f );
                                } else {
                                    axis = Vector3f( 0, 0, previous.z < 0 ? -1.0f : 1.0f );
                                    d = box.HalfExtents.z + std::abs( local.z ) * ( ( previous.z < 0 ) == ( local.z < 0 ) ? -1.0f : 1.0f );
                                }
                            }
                        }
                        const Vector3f normal = box.Transform.transformVector( axis ).normalized();
                        consider( center - normal * d, normal, -d );
                    }
                }
            }
            auto itCapsule = capsuleCells.find( CellKey( x, z ) );
            if ( itCapsule != capsuleCells.end() ) {
                for ( uint32_t index : itCapsule->second ) {
                    if ( !seen.insert( 0x40000000u | index ).second ) {
                        continue;
                    }
                    const Capsule &capsule = capsules[index];
                    const Vector3f axisPoint = ClosestPointOnSegment( center, capsule.A, capsule.B );
                    const Vector3f delta = center - axisPoint;
                    const float distance = delta.length();
                    if ( distance < radius + capsule.Radius && distance > 1e-5f ) {
                        const Vector3f normal = delta * ( 1.0f / distance );
                        consider( axisPoint + normal * capsule.Radius, normal, distance - capsule.Radius );
                    }
                }
            }
        }
    }
    return bFound;
}

void VehicleWorldCollision::forEachRoadTriangle(
    const Vector3f &center, float radius,
    const std::function<void( const Vector3f &, const Vector3f &, const Vector3f & )> &callback,
    bool bSectorRoadsOnly ) const {
    const int32_t x0 = static_cast<int32_t>( std::floor( ( center.x - radius ) / kCellSize ) );
    const int32_t x1 = static_cast<int32_t>( std::floor( ( center.x + radius ) / kCellSize ) );
    const int32_t z0 = static_cast<int32_t>( std::floor( ( center.z - radius ) / kCellSize ) );
    const int32_t z1 = static_cast<int32_t>( std::floor( ( center.z + radius ) / kCellSize ) );
    std::unordered_set<uint32_t> seen;
    for ( int32_t z = z0; z <= z1; z++ ) {
        for ( int32_t x = x0; x <= x1; x++ ) {
            auto it = triangleCells.find( CellKey( x, z ) );
            if ( it == triangleCells.end() ) {
                continue;
            }
            for ( uint32_t index : it->second ) {
                const Triangle &triangle = triangles[index];
                if ( triangle.bRoad && ( !bSectorRoadsOnly || triangle.bSectorRoad ) && seen.insert( index ).second ) {
                    callback( triangle.A, triangle.B, triangle.C );
                }
            }
        }
    }
}
