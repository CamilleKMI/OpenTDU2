#pragma once

#include "core/math.h"
#include "game/gs_base.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace PhysicsLayers {
static constexpr JPH::ObjectLayer kStatic = 0;
static constexpr JPH::ObjectLayer kMoving = 1;
static constexpr JPH::ObjectLayer kCount = 2;
} // namespace PhysicsLayers

// Physics service (Jolt Physics). TDU2 used Havok; collision data (.dhk) is
// not converted yet, so the world collision is built from simple shapes.
class GSPhysics : public GameSystem {
public:
    inline JPH::PhysicsSystem *getPhysicsSystem() { return pPhysicsSystem.get(); }
    inline JPH::BodyInterface &getBodyInterface() { return pPhysicsSystem->GetBodyInterface(); }

public:
    GSPhysics();
    ~GSPhysics();

    const char *getName() const override { return "Service : Physics"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void terminate() override;
    void drawDebug() override;

    void setSimulationEnabled( bool bEnabled ) { bSimulate = bEnabled; }

    JPH::BodyID createStaticBox( const Vector3f &center, const Vector3f &halfExtents,
                                 const Quaternionf &rotation = Quaternionf(), float friction = 1.0f );
    JPH::BodyID createStaticMesh( const std::vector<Vector3f> &vertices,
                                  const std::vector<uint32_t> &indices, float friction = 1.0f );
    void removeBody( JPH::BodyID bodyID );
    void removeAllBodies();

    static inline JPH::Vec3 ToJolt( const Vector3f &v ) { return JPH::Vec3( v.x, v.y, v.z ); }
    static inline Vector3f FromJolt( JPH::Vec3Arg v ) { return Vector3f( v.GetX(), v.GetY(), v.GetZ() ); }
    static inline JPH::Quat ToJolt( const Quaternionf &q ) { return JPH::Quat( q.x, q.y, q.z, q.w ); }
    static inline Quaternionf FromJolt( JPH::QuatArg q ) {
        return Quaternionf( q.GetX(), q.GetY(), q.GetZ(), q.GetW() );
    }

private:
    class BroadPhaseLayers;
    class ObjectVsBroadPhaseFilter;
    class ObjectPairFilter;

    std::unique_ptr<JPH::TempAllocatorImpl> pTempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> pJobSystem;
    std::unique_ptr<BroadPhaseLayers> pBroadPhaseLayers;
    std::unique_ptr<ObjectVsBroadPhaseFilter> pObjectVsBroadPhaseFilter;
    std::unique_ptr<ObjectPairFilter> pObjectPairFilter;
    std::unique_ptr<JPH::PhysicsSystem> pPhysicsSystem;
    std::vector<JPH::BodyID> bodies;
    float accumulator;
    bool bSimulate;
    bool bInitialized;
};

extern GSPhysics *gpPhysics;
