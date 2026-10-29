#pragma once

#include "core/math.h"

#include <functional>

struct HavokCollision;
class Bank;

// Driving collision of the streamed island (docs: /tmp/claude-0/spec_freeride.md 2.6).
//
// Every sector bank (islands/<island>/level/area-a/area-a-b/sector-a-b-c-d.bnk)
// holds two Havok 5.5 packfiles [DATA]:
//   - Sector-a-b-c-d-R.shk: the road surfaces (hkpStorageExtendedMeshShape
//     behind a MOPP, friction 0.7);
//   - Sector-a-b-c-d.shk: the static world (buildings, walls: triangle
//     meshes, boxes, capsules for the poles / trunks).
// Their rigid body transforms are area-local (sector-5-0-6-7: T = (4992, 0,
// 5376) = (6 x 768 + 384, 0, 7 x 768)), so world = file frame + the area
// origin (8 x 768 x areaX, 0, 8 x 768 x areaY) [DATA, checked on the roads
// around the EPH_L3DI house].
// The terrain heightfield (257 x 257, 3 m) is queried through a callback
// (EdenWorld::getHeight) where no Havok surface is hit.
class VehicleWorldCollision {
public:
    // Ground under a point: heightfield height at (x, z).
    using HeightFunction = std::function<bool( float x, float z, float &outHeight )>;

    struct Hit {
        float Distance = 0.0f;
        Vector3f Position;
        Vector3f Normal;
        float Friction = 1.0f; // hkpRigidBody material friction (0.7 roads) / terrain [INF 0.7]
        enum class eSurface : uint8_t { Road, Static, Terrain } Surface = eSurface::Road;
    };

    struct Contact {
        Vector3f Normal;  // pushes the sphere out
        float Depth = 0.0f;
        Vector3f Point;   // on the obstacle
    };

public:
    VehicleWorldCollision();
    ~VehicleWorldCollision();

    void setIsland( const char *pIsland );
    void setHeightFunction( HeightFunction function ) { heightFunction = std::move( function ); }
    // Library collision (commonworld.bnk Library.dhk) placed by the sector
    // .pmi instance lists (V / O static models), see loadLibrary.
    bool loadLibrary( const Bank &commonBank );
    // Loads the collision of the sectors around a position (synchronously,
    // once per sector; same brick layout as EdenWorld).
    void streamAround( const Vector3f &position, int32_t sectorRadius );
    void clear();
    // Extra collision (spot banks: <spot>_sc_*.shk in the spot frame).
    // bSectorRoad: a sector -R.shk road surface (FREE_DRIVE road snapping).
    void addCollision( const HavokCollision &collision, const Matrix4x4f &toWorld, bool bRoad, bool bSectorRoad = false );
    // Test mode (-vehtest): an infinite horizontal road plane y = height with
    // the given friction replaces the world (no obstacles).
    void setFlatGround( bool bEnable, float height = 0.0f, float friction = 0.7f ) {
        bFlatGround = bEnable;
        flatHeight = height;
        flatFriction = friction;
    }

    // Closest hit along a ray (Havok triangles, then the heightfield when
    // nothing nearer is found). dir normalized.
    bool raycast( const Vector3f &origin, const Vector3f &dir, float maxDistance, Hit &outHit ) const;
    // Deepest penetration of a sphere into the static world / roads.
    // pPrevious: the centre at the previous step (a centre that went inside a
    // thin box leaves through the face it entered).
    bool sphereContact( const Vector3f &center, float radius, Contact &outContact,
                        const Vector3f *pPrevious = nullptr ) const;

    // Road triangles (-R.shk, spot collision) around a point (minimap);
    // bSectorRoadsOnly: only the sector -R.shk surfaces.
    void forEachRoadTriangle( const Vector3f &center, float radius,
                              const std::function<void( const Vector3f &, const Vector3f &, const Vector3f & )> &callback,
                              bool bSectorRoadsOnly = false ) const;

    inline size_t getNumTriangles() const { return triangles.size(); }
    inline size_t getNumSectors() const { return loadedSectors.size(); }

private:
    struct Triangle {
        Vector3f A, B, C;
        Vector3f Normal;
        float Friction;
        bool bRoad;
        bool bSectorRoad;
    };
    struct Box {
        Matrix4x4f Transform; // centre + orientation
        Matrix4x4f Inverse;
        Vector3f HalfExtents;
        float Friction;
    };
    struct Capsule {
        Vector3f A, B;
        float Radius;
    };

    static constexpr float kCellSize = 8.0f;

    std::string islandFolder;
    bool bFlatGround = false;
    float flatHeight = 0.0f;
    float flatFriction = 0.7f;
    HeightFunction heightFunction;
    std::vector<Triangle> triangles;
    std::vector<Box> boxes;
    std::vector<Capsule> capsules;
    std::unordered_map<uint64_t, std::vector<uint32_t>> triangleCells; // cell key -> triangles
    std::unordered_map<uint64_t, std::vector<uint32_t>> boxCells;
    std::unordered_map<uint64_t, std::vector<uint32_t>> capsuleCells;
    std::unordered_set<uint32_t> loadedSectors; // row * 1024 + column

    static uint64_t CellKey( int32_t x, int32_t z ) {
        return ( static_cast<uint64_t>( static_cast<uint32_t>( x ) ) << 32 ) | static_cast<uint32_t>( z );
    }
    void loadSector( int32_t row, int32_t column );
    void addLibraryInstances( const Bank &sectorBank, const std::string &name, const Vector3f &sectorOrigin );

    // Library.dhk: one hkpPhysicsSystem of fixed bodies (box / capsule /
    // sphere / MOPP mesh). Eden entries {u64 hash, u32 index, u32 type}:
    // type 0 (index 0..n-1) and type 1 (index n..2n-1, hash = the library
    // node hashcode of the .pmi MOB records) per model, type 3 "Phys"+k per
    // body; the extra block lists per model {u32 model; u32; cd x2; u32
    // numBodies; cd; 0 x2} + numBodies x 0x80 (body pose, motion type 7)
    // in body order [DATA, Library.dhk: 461 models, 864 bodies].
    std::unique_ptr<HavokCollision> library;
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> libraryModels; // hash -> first body, count
    // Per body: model-local pose of the extra block x inverse(packfile body
    // transform). The packfile bodies are laid out side by side (export
    // layout, e.g. x -20 m), the extra block holds the model-local pose
    // [DATA: small offsets, same rotations].
    std::vector<Matrix4x4f> libraryBodyCorrections;
    void addCells( std::unordered_map<uint64_t, std::vector<uint32_t>> &cells, const Vector3f &boundsMin,
                   const Vector3f &boundsMax, uint32_t index );
};
