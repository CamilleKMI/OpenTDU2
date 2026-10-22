#pragma once

#include "core/math.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Reader for TDU2 Havok collision files (.shk static / .dhk dynamic, e.g.
// intro_lph_l3di_*.shk, garage_int_*.shk/.dhk). Each file is an "Eden" header
// (02 02, u16 count, 0x20-byte entries) followed by a Havok-5.5.0-r1 binary
// packfile (32-bit little-endian layout) holding one hkpPhysicsSystem.
// Layouts are read from the hkClass reflection embedded in each file's
// __types__ section (see /tmp/claude-0/spec_havok.md).
//
// Everything is flattened into the file's world frame (the rigid body
// transforms are applied): for the house this is house-local space (the
// same frame as the .sce/entities; floor at y = 31.12), for the garage it is
// garage-local space (floor at y = 0).
struct HavokCollision {
    // Triangle soup (hkpStorageExtendedMeshShape subparts), world frame.
    std::vector<Vector3f> Vertices;
    std::vector<uint32_t> Indices; // 3 per triangle

    struct Box {
        Matrix4x4f Transform; // box centre/orientation, world frame (rigid, no scale)
        Vector3f HalfExtents;
        float ConvexRadius = 0.0f; // hkpConvexShape::m_radius (0.01 in the TDU2 data)
        uint32_t BodyIndex = 0;
    };
    std::vector<Box> Boxes;

    struct Capsule {
        Vector3f A; // segment end points, world frame
        Vector3f B;
        float Radius = 0.0f;
        uint32_t BodyIndex = 0;
    };
    std::vector<Capsule> Capsules;

    struct Body {
        Matrix4x4f Transform;    // hkMotionState::m_transform
        uint8_t MotionType = 0;  // hkpMotion::m_type (7 = fixed in .shk, 4 = box inertia in .dhk)
        float Friction = 0.0f;
        uint32_t FirstTriangle = 0, NumTriangles = 0;
        uint32_t FirstBox = 0, NumBoxes = 0;
        uint32_t FirstCapsule = 0, NumCapsules = 0;
        bool isFixed() const { return MotionType == 7; }
    };
    std::vector<Body> Bodies;

    // hkpLimitedHingeConstraintData (.dhk hinged parts). Pivots/axes are in the
    // local frame of the respective body.
    struct Hinge {
        uint32_t BodyA = 0, BodyB = 0;
        Vector3f PivotA, AxisA;
        Vector3f PivotB, AxisB;
        float MinAngle = 0.0f, MaxAngle = 0.0f;
        bool LimitEnabled = false;
    };
    std::vector<Hinge> Hinges;

    Vector3f BoundsMin;
    Vector3f BoundsMax;

    size_t getNumTriangles() const { return Indices.size() / 3; }
    void clear();
};

// Parses an Eden-wrapped Havok 5.5 packfile. When bStaticOnly is set, bodies
// that are not fixed (motion type != 7) are skipped.
bool LoadHavokCollision( const uint8_t *pData, size_t size, HavokCollision &out,
                         bool bStaticOnly = false );

// Closest hit of a ray against every primitive (triangles are two-sided).
// dir does not need to be normalized; outT is in units of |dir|.
bool RaycastHavokCollision( const HavokCollision &collision, const Vector3f &origin,
                            const Vector3f &dir, float maxDist, float &outT,
                            Vector3f *pOutNormal = nullptr );

// Casts down from (pos.y + maxStepUp) to (pos.y - maxDrop); returns the
// highest floor surface found below that start point.
bool FindHavokFloor( const HavokCollision &collision, const Vector3f &pos, float maxStepUp,
                     float maxDrop, float &outFloorY, Vector3f *pOutNormal = nullptr );

// Pushes a sphere out of every primitive it penetrates (a few relaxation
// iterations). Returns the corrected centre.
Vector3f ResolveSphereHavokCollision( const HavokCollision &collision, Vector3f center,
                                      float radius, uint32_t iterations = 4 );
