#pragma once

#include "render/2dm.h"
#include "render/eden/eden_renderer.h"

class Bank;
class BankHashcodes;
class XmbDocument;

// Eden particle systems (.prt, docs/re/particles_spec.md).
//
// The simulation of the original (edFxParticles, vtable 0x00f48ff4, class
// descriptor written by FUN_00e36db0) is in the encrypted part of the
// executable (0x005e4000..0x005ef000) [ENC]: the field meanings below come
// from the data (76 .prt files compared) and from the TD_Particle shaders;
// every behaviour that is not a plain read of a value is tagged [INF] in the
// spec.
//
// .prt layout (file offsets, RenderFile header + ".PRT" section v0x1b):
//   +0x24 f32 simulation step (0.02 in every file), +0x28 u32 name table,
//   +0x2c u32 name count, +0x30 u32 force block, +0x44 u32 max particles.
//   Name table: { u64 identifier, u64 'XXXXXXXX' }[n]; [0] the system,
//   [1] the generator, [2] the sprite renderer ("....RES"), then the
//   curve set ("......RI"), curve owners (".......R") and GRAVITY.
//   Force block: +0x00 acceleration (GRAVITY, m/s^2: -9.81 in one file),
//   +0x10 / +0x20 constant velocity range (min / max).
//   Generator (g = offset of its identifier): emitter matrix g-0xf8 (D3D
//   rows), direction g-0x68, volume size g-0x58, size g-0x24, weight g-0x20,
//   size / weight random g-0x14 / g-0x10, speed g+0x08, speed random g+0x0c,
//   cone angles g+0x10 / g+0x14 (rad), rate g+0x18 (/s), period g+0x1c,
//   life g+0x20, life random g+0x24, particles per emission g+0x2c, shape
//   g+0x40.
//   Sprite renderer (r = offset of its identifier - 8): rotation random /
//   spin min / spin max r+0x64..0x6c, atlas cell size r+0x80, atlas columns
//   / rows r+0x90, random start frames / frames r+0xa4 / r+0xa8, frame rate
//   r+0xc0.
//   Curve set (c = offset of its identifier): count c+0x34, first curve
//   c+0x38; curves are 0x80 bytes: { u32 flags; f32; u32 0xeffe00<id>; u32;
//   f32 value[28] } over the particle life, 0..255: id 1/2/3 red / green /
//   blue, 9/10 size x / y, 12 alpha, 13 (always 1).
//   "MAT." section: u64 material hashcode (the PrtMat .2DM material).
struct EdenParticleSystemDesc {
    static constexpr uint32_t kCurveSamples = 28;
    struct Curve {
        uint32_t Id = 0;
        float Samples[kCurveSamples] = {};
    };

    uint64_t Id = 0;
    uint64_t MaterialHash = 0;
    float Step = 0.02f;
    uint32_t MaxParticles = 0;
    Vector3f Acceleration;
    Vector3f DriftMin;
    Vector3f DriftMax;
    Matrix4x4f EmitterLocal;
    Vector3f Direction = Vector3f( 0.0f, 1.0f, 0.0f );
    Vector3f VolumeSize = Vector3f( 1.0f, 1.0f, 1.0f );
    uint32_t Shape = 7;
    float Size = 1.0f;
    float SizeRandom = 0.0f;
    float Weight = 1.0f;
    float WeightRandom = 0.0f;
    float Speed = 0.0f;
    float SpeedRandom = 0.0f;
    float ConeAngle = 0.0f;
    float ConeAngle2 = 0.0f;
    float Rate = 0.0f;
    float Life = 1.0f;
    float LifeRandom = 0.0f;
    uint32_t PerEmission = 1;
    float RotationRandom = 0.0f;
    float SpinMin = 0.0f;
    float SpinMax = 0.0f;
    float CellWidth = 1.0f;
    float CellHeight = 1.0f;
    uint32_t AtlasColumns = 1;
    uint32_t AtlasRows = 1;
    uint32_t NumFrames = 1;
    uint32_t NumRandomFrames = 1;
    float FrameRate = 0.0f;
    std::vector<Curve> Curves;

    bool parse( const uint8_t *pFile, uint32_t fileSize );
    const Curve *findCurve( uint32_t id ) const;
    // Curve value at life fraction t (0..1), linear between the samples;
    // 'missing' when the system has no such curve.
    float evaluate( uint32_t id, float t, float missing ) const;
};

// One emitter instance (Entity__Fx of a bank) of a particle system.
class EdenParticleEmitter {
public:
    struct Particle {
        Vector3f Position; // world
        Vector3f Velocity;
        Vector3f Drift;
        float Age = 0.0f;
        float Life = 1.0f;
        float Size = 1.0f;
        float Angle = 0.0f;
        float Spin = 0.0f;
        uint32_t Frame = 0;
    };

    void initialize( const EdenParticleSystemDesc *pDesc, const Matrix4x4f &world, uint32_t seed );
    // Fixed steps of EdenParticleSystemDesc::Step.
    void update( float deltaTime );
    inline const std::vector<Particle> &getParticles() const { return particles; }
    inline const EdenParticleSystemDesc *getDesc() const { return pDesc; }
    inline const Matrix4x4f &getWorld() const { return world; }

private:
    const EdenParticleSystemDesc *pDesc = nullptr;
    Matrix4x4f world;  // emitter frame (bank frame * entity * system emitter matrix)
    Vector3f origin;
    Vector3f direction;
    std::vector<Particle> particles;
    float accumulator = 0.0f;
    float emission = 0.0f;
    uint32_t seed = 1;

    float random();      // [0, 1)
    float randomSigned(); // [-1, 1)
    void emit();
    void step( float dt );
};

// Particle systems (.prt + PrtMat .2DM, TD_Particle material) of banks,
// shared by their emitters. Fx__SourceData (entities.xmb ExportType_
// FxSourceData) -> ParticlesList[] -> Fx__Particles { Prt (0/18), PrtMat
// (0/2) } -> one system per .prt.
class EdenParticleLibrary {
public:
    struct System {
        EdenParticleSystemDesc Desc;
        Render2DM Materials;
        EdenMaterial Material;
        GPUBuffer *pIndexBuffer = nullptr;
    };

    EdenParticleLibrary() = default;
    ~EdenParticleLibrary();
    EdenParticleLibrary( const EdenParticleLibrary & ) = delete;
    EdenParticleLibrary &operator=( const EdenParticleLibrary & ) = delete;

    void clear();
    // System of a .prt resource of the bank (cached; nullptr when it is not
    // a particle system or has no TD_Particle program).
    const System *getSystem( EdenRenderer &renderer, const Bank &bank, const BankHashcodes &hashcodes, uint64_t prtRsc,
                             uint64_t materialRsc );
    // Systems of a Fx__SourceData (by __Hc) of 'entities'.
    std::vector<const System *> getSourceSystems( EdenRenderer &renderer, const Bank &bank,
                                                  const BankHashcodes &hashcodes, const XmbDocument &entities,
                                                  uint64_t sourceHash );

private:
    std::unordered_map<uint64_t, std::unique_ptr<System>> systems; // by .prt rsc (null: not a system)
};

// One running emitter of a system, with its per frame vertex stream.
class EdenParticleInstance {
public:
    void initialize( const EdenParticleLibrary::System *pSystem, const Matrix4x4f &world, uint32_t seed );
    void update( float deltaTime ) { emitter.update( deltaTime ); }
    // Blended draw (deferred and sorted by the renderer at the emitter
    // origin); the vertices are built once per frame.
    void draw( EdenRenderer &renderer, const Vector3f &cameraPosition );
    inline const EdenParticleEmitter &getEmitter() const { return emitter; }
    inline const EdenParticleLibrary::System *getSystem() const { return pSystem; }

private:
    const EdenParticleLibrary::System *pSystem = nullptr;
    EdenParticleEmitter emitter;
    EdenMesh mesh;
    GPUBuffer stream; // transient buffer wrapper (not owned)
    uint64_t frameNumber = ~0ull;
};

// Particle systems of a bank (Entity__Fx -> Fx__Root -> Fx__SourceData ->
// ParticlesList -> Fx__Particles { Prt, PrtMat }), drawn with TD_Particle.
class EdenParticleSet {
public:
    EdenParticleSet() = default;
    ~EdenParticleSet();

    // Emitters of the Entity__Fx entries of 'entities' (AutoStart ones), one
    // instance per frame. Returns the number of emitters.
    uint32_t load( EdenRenderer &renderer, const Bank &bank, const BankHashcodes &hashcodes,
                   const XmbDocument &entities, const std::vector<Matrix4x4f> &frames );
    void clear();
    void update( float deltaTime );
    // Blended draws (deferred and sorted by the renderer), one per emitter.
    void draw( EdenRenderer &renderer, const Vector3f &cameraPosition );
    inline size_t getNumEmitters() const { return emitters.size(); }
    inline const std::vector<std::unique_ptr<EdenParticleInstance>> &getEmitters() const { return emitters; }

private:
    EdenParticleLibrary library;
    std::vector<std::unique_ptr<EdenParticleInstance>> emitters;
};
