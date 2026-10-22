#include "shared.h"
#include "gs_physics.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/RegisterTypes.h>

#include "imgui.h"

GSPhysics *gpPhysics = nullptr;

namespace {
constexpr float kFixedTimeStep = 1.0f / 120.0f;
constexpr uint32_t kMaxSubSteps = 8;

namespace BroadPhaseLayerIDs {
static constexpr JPH::BroadPhaseLayer kStatic( 0 );
static constexpr JPH::BroadPhaseLayer kMoving( 1 );
static constexpr uint32_t kCount = 2;
} // namespace BroadPhaseLayerIDs

void JoltTrace( const char *pFormat, ... ) {
    char message[1024];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( message, sizeof( message ), pFormat, args );
    va_end( args );
    OTDU_LOG_DEBUG( "[Jolt] %s\n", message );
}
} // namespace

class GSPhysics::BroadPhaseLayers : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayerIDs::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer( JPH::ObjectLayer layer ) const override {
        return layer == PhysicsLayers::kStatic ? BroadPhaseLayerIDs::kStatic
                                               : BroadPhaseLayerIDs::kMoving;
    }
#if defined( JPH_EXTERNAL_PROFILE ) || defined( JPH_PROFILE_ENABLED )
    const char *GetBroadPhaseLayerName( JPH::BroadPhaseLayer layer ) const override {
        return layer == BroadPhaseLayerIDs::kStatic ? "Static" : "Moving";
    }
#endif
};

class GSPhysics::ObjectVsBroadPhaseFilter : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide( JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer ) const override {
        if ( layer == PhysicsLayers::kStatic ) {
            return broadPhaseLayer == BroadPhaseLayerIDs::kMoving;
        }
        return true;
    }
};

class GSPhysics::ObjectPairFilter : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide( JPH::ObjectLayer a, JPH::ObjectLayer b ) const override {
        return !( a == PhysicsLayers::kStatic && b == PhysicsLayers::kStatic );
    }
};

GSPhysics::GSPhysics() : accumulator( 0.0f ), bSimulate( true ), bInitialized( false ) {
    gpPhysics = this;
}

GSPhysics::~GSPhysics() { gpPhysics = nullptr; }

bool GSPhysics::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    JPH::RegisterDefaultAllocator();
    JPH::Trace = JoltTrace;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    pTempAllocator = std::make_unique<JPH::TempAllocatorImpl>( 32 * 1024 * 1024 );
    const int32_t numThreads = Max( 1, static_cast<int32_t>( std::thread::hardware_concurrency() ) - 2 );
    pJobSystem = std::make_unique<JPH::JobSystemThreadPool>( JPH::cMaxPhysicsJobs,
                                                             JPH::cMaxPhysicsBarriers, numThreads );

    pBroadPhaseLayers = std::make_unique<BroadPhaseLayers>();
    pObjectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseFilter>();
    pObjectPairFilter = std::make_unique<ObjectPairFilter>();

    pPhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
    pPhysicsSystem->Init( 16384, 0, 16384, 8192, *pBroadPhaseLayers, *pObjectVsBroadPhaseFilter,
                          *pObjectPairFilter );
    pPhysicsSystem->SetGravity( JPH::Vec3( 0.0f, -9.81f, 0.0f ) );
    bInitialized = true;
    return true;
}

void GSPhysics::terminate() {
    if ( !bInitialized ) {
        return;
    }
    removeAllBodies();
    pPhysicsSystem.reset();
    pJobSystem.reset();
    pTempAllocator.reset();
    pObjectPairFilter.reset();
    pObjectVsBroadPhaseFilter.reset();
    pBroadPhaseLayers.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    bInitialized = false;
}

void GSPhysics::tick( float deltaTime ) {
    if ( !bSimulate ) {
        return;
    }
    accumulator += deltaTime;
    uint32_t numSteps = 0;
    while ( accumulator >= kFixedTimeStep && numSteps < kMaxSubSteps ) {
        pPhysicsSystem->Update( kFixedTimeStep, 1, pTempAllocator.get(), pJobSystem.get() );
        accumulator -= kFixedTimeStep;
        numSteps++;
    }
    if ( numSteps == kMaxSubSteps ) {
        accumulator = 0.0f;
    }
}

JPH::BodyID GSPhysics::createStaticBox( const Vector3f &center, const Vector3f &halfExtents,
                                        const Quaternionf &rotation, float friction ) {
    JPH::BodyCreationSettings settings( new JPH::BoxShape( ToJolt( halfExtents ) ),
                                        JPH::RVec3( center.x, center.y, center.z ),
                                        ToJolt( rotation ), JPH::EMotionType::Static,
                                        PhysicsLayers::kStatic );
    settings.mFriction = friction;
    JPH::BodyID id = getBodyInterface().CreateAndAddBody( settings, JPH::EActivation::DontActivate );
    bodies.push_back( id );
    return id;
}

JPH::BodyID GSPhysics::createStaticMesh( const std::vector<Vector3f> &vertices,
                                         const std::vector<uint32_t> &indices, float friction ) {
    JPH::VertexList joltVertices;
    joltVertices.reserve( vertices.size() );
    for ( const Vector3f &v : vertices ) {
        joltVertices.push_back( JPH::Float3( v.x, v.y, v.z ) );
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve( indices.size() / 3 );
    for ( size_t i = 0; i + 2 < indices.size(); i += 3 ) {
        triangles.push_back( JPH::IndexedTriangle( indices[i], indices[i + 1], indices[i + 2], 0 ) );
    }
    JPH::MeshShapeSettings meshSettings( joltVertices, triangles );
    JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
    if ( result.HasError() ) {
        OTDU_LOG_WARN( "Failed to create mesh shape: %s\n", result.GetError().c_str() );
        return JPH::BodyID();
    }
    JPH::BodyCreationSettings settings( result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                        JPH::EMotionType::Static, PhysicsLayers::kStatic );
    settings.mFriction = friction;
    JPH::BodyID id = getBodyInterface().CreateAndAddBody( settings, JPH::EActivation::DontActivate );
    bodies.push_back( id );
    return id;
}

void GSPhysics::removeBody( JPH::BodyID bodyID ) {
    if ( bodyID.IsInvalid() ) {
        return;
    }
    JPH::BodyInterface &bodyInterface = getBodyInterface();
    bodyInterface.RemoveBody( bodyID );
    bodyInterface.DestroyBody( bodyID );
    bodies.erase( std::remove( bodies.begin(), bodies.end(), bodyID ), bodies.end() );
}

void GSPhysics::removeAllBodies() {
    JPH::BodyInterface &bodyInterface = getBodyInterface();
    for ( JPH::BodyID id : bodies ) {
        bodyInterface.RemoveBody( id );
        bodyInterface.DestroyBody( id );
    }
    bodies.clear();
}

void GSPhysics::drawDebug() {
    if ( !ImGui::Begin( "Physics" ) ) {
        ImGui::End();
        return;
    }
    ImGui::Checkbox( "Simulate", &bSimulate );
    ImGui::Text( "Bodies: %u (active %u)", pPhysicsSystem->GetNumBodies(),
                 pPhysicsSystem->GetNumActiveBodies( JPH::EBodyType::RigidBody ) );
    ImGui::End();
}
