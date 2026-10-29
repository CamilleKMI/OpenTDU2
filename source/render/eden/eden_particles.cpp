#include "shared.h"
#include "eden_particles.h"

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_scene_view.h"
#include "render/gs_render.h"
#include "render/render_file.h"

namespace {
// Object type identifiers of the .prt name table [DATA: the same value in
// every file; GetIdentifier64bit folds of names longer than 8 characters].
constexpr uint64_t kGeneratorType = 0x849b99a086e9ece6ull;
constexpr uint64_t kSpriteType = 0x534552939196998eull; // "....RES"
constexpr uint64_t kCurveSetType = 0x49529a8a8ea690a1ull; // "......RI"
constexpr uint32_t kCurveSize = 0x80;

template <typename T> T Read( const uint8_t *pFile, uint32_t fileSize, uint32_t offset, T missing = T() ) {
    if ( offset + sizeof( T ) > fileSize ) {
        return missing;
    }
    T value;
    memcpy( &value, pFile + offset, sizeof( T ) );
    return value;
}

Vector3f ReadVector( const uint8_t *pFile, uint32_t fileSize, uint32_t offset ) {
    return Vector3f( Read<float>( pFile, fileSize, offset ), Read<float>( pFile, fileSize, offset + 4 ),
                     Read<float>( pFile, fileSize, offset + 8 ) );
}

// First occurrence of an object identifier before the name table (the
// object block holds it).
int64_t FindIdentifier( const uint8_t *pFile, uint32_t end, uint64_t identifier ) {
    for ( uint32_t offset = 0x20; offset + 8 <= end; offset += 4 ) {
        uint64_t value;
        memcpy( &value, pFile + offset, 8 );
        if ( value == identifier ) {
            return offset;
        }
    }
    return -1;
}

// Vertex of a particle quad corner (TD_Particle vs inputs [SHADER]):
// v0 POSITION (center, __World space), v1 NORMAL (half size x / y, depth
// offset), v2 TEXCOORD1 (corner 0 / 1: offset = v1.xy * (1 - 2 v2.xy)),
// v3 TEXCOORD2.x (angle: the quad turns by -v3.x in the view plane), v4
// COLOR, v5 TEXCOORD0 (uv). The corner offset is rotated into the world by
// InvView (camera facing quad, vs 0..7), or (vs 8..11, the fountain
// material: TD_Particle vs10) built around v6 BINORMAL, an axis in object
// space: x along axis x (to the camera), y along the axis tilted toward the
// camera by a quarter of the view elevation angle, v1.z along the remaining
// normal; the colour is v4 * lerp(ambient, sun diffuse, 0.75).
struct ParticleVertex {
    float Position[3];
    float HalfSize[3];
    float Corner[2];
    float Angle[2];
    uint32_t Color; // B8G8R8A8 (D3DCOLOR)
    float Texcoord[2];
    float Axis[3];
};
static_assert( sizeof( ParticleVertex ) == 64, "particle vertex layout" );

uint32_t ToByte( float value ) {
    return static_cast<uint32_t>( Clamp( value, 0.0f, 255.0f ) + 0.5f );
}
} // namespace

bool EdenParticleSystemDesc::parse( const uint8_t *pFile, uint32_t fileSize ) {
    if ( fileSize < 0x60 || memcmp( pFile + 0x0c, ".PRT", 4 ) != 0 || memcmp( pFile + 0x10, ".PRT", 4 ) != 0 ) {
        return false;
    }
    constexpr uint32_t kData = 0x20;
    Step = Read<float>( pFile, fileSize, kData + 0x04, 0.02f );
    const uint32_t nameTable = Read<uint32_t>( pFile, fileSize, kData + 0x08 );
    const uint32_t numNames = Read<uint32_t>( pFile, fileSize, kData + 0x0c );
    const uint32_t forces = Read<uint32_t>( pFile, fileSize, kData + 0x10 );
    MaxParticles = Read<uint32_t>( pFile, fileSize, kData + 0x24 );
    if ( nameTable == 0 || nameTable + numNames * 16 > fileSize || MaxParticles == 0 || MaxParticles > 0x10000 ) {
        return false;
    }
    Id = Read<uint64_t>( pFile, fileSize, nameTable );
    Acceleration = ReadVector( pFile, fileSize, forces );
    DriftMin = ReadVector( pFile, fileSize, forces + 0x10 );
    DriftMax = ReadVector( pFile, fileSize, forces + 0x20 );

    const int64_t generator = FindIdentifier( pFile, nameTable, kGeneratorType );
    if ( generator < 0x100 ) {
        return false;
    }
    const uint32_t g = static_cast<uint32_t>( generator );
    memcpy( &EmitterLocal, pFile + g - 0xf8, sizeof( EmitterLocal ) );
    Direction = ReadVector( pFile, fileSize, g - 0x68 );
    VolumeSize = ReadVector( pFile, fileSize, g - 0x58 );
    Size = Read<float>( pFile, fileSize, g - 0x24 );
    Weight = Read<float>( pFile, fileSize, g - 0x20 );
    SizeRandom = Read<float>( pFile, fileSize, g - 0x14 );
    WeightRandom = Read<float>( pFile, fileSize, g - 0x10 );
    Speed = Read<float>( pFile, fileSize, g + 0x08 );
    SpeedRandom = Read<float>( pFile, fileSize, g + 0x0c );
    ConeAngle = Read<float>( pFile, fileSize, g + 0x10 );
    ConeAngle2 = Read<float>( pFile, fileSize, g + 0x14 );
    Rate = Read<float>( pFile, fileSize, g + 0x18 );
    Life = Read<float>( pFile, fileSize, g + 0x20, 1.0f );
    LifeRandom = Read<float>( pFile, fileSize, g + 0x24 );
    PerEmission = Max( Read<uint32_t>( pFile, fileSize, g + 0x2c, 1 ), 1u );
    Shape = Read<uint32_t>( pFile, fileSize, g + 0x40 );

    const int64_t sprite = FindIdentifier( pFile, nameTable, kSpriteType );
    if ( sprite >= 8 ) {
        const uint32_t r = static_cast<uint32_t>( sprite ) - 8;
        RotationRandom = Read<float>( pFile, fileSize, r + 0x64 );
        SpinMin = Read<float>( pFile, fileSize, r + 0x68 );
        SpinMax = Read<float>( pFile, fileSize, r + 0x6c );
        CellWidth = Read<float>( pFile, fileSize, r + 0x80, 1.0f );
        CellHeight = Read<float>( pFile, fileSize, r + 0x84, 1.0f );
        AtlasColumns = Max( Read<uint32_t>( pFile, fileSize, r + 0x90, 1 ), 1u );
        AtlasRows = Max( Read<uint32_t>( pFile, fileSize, r + 0x94, 1 ), 1u );
        NumRandomFrames = Max( Read<uint32_t>( pFile, fileSize, r + 0xa4, 1 ), 1u );
        NumFrames = Max( Read<uint32_t>( pFile, fileSize, r + 0xa8, 1 ), 1u );
        FrameRate = Read<float>( pFile, fileSize, r + 0xc0 );
    }

    Curves.clear();
    const int64_t curveSet = FindIdentifier( pFile, nameTable, kCurveSetType );
    if ( curveSet > 0 ) {
        const uint32_t c = static_cast<uint32_t>( curveSet );
        const uint32_t numCurves = Read<uint32_t>( pFile, fileSize, c + 0x34 );
        const uint32_t first = Read<uint32_t>( pFile, fileSize, c + 0x38 );
        for ( uint32_t i = 0; i < numCurves && i < 16; i++ ) {
            const uint32_t offset = first + i * kCurveSize;
            const uint32_t tag = Read<uint32_t>( pFile, fileSize, offset + 8 );
            if ( offset + kCurveSize > fileSize || ( tag >> 16 ) != 0xeffe ) {
                break;
            }
            Curve curve;
            curve.Id = tag & 0xffff;
            memcpy( curve.Samples, pFile + offset + 0x10, sizeof( curve.Samples ) );
            Curves.push_back( curve );
        }
    }

    MaterialHash = 0;
    RenderFile::VisitSections( pFile, fileSize, [&]( const RenderFile::Section &section, uint32_t offset, uint32_t ) {
        if ( section.Type == MakeFourCC( 'M', 'A', 'T', '.' ) && section.DataSize >= 0x10 ) {
            MaterialHash = Read<uint64_t>( pFile, fileSize, offset + 0x18 );
            return false;
        }
        return true;
    } );
    return Rate > 0.0f && Life > 0.0f;
}

const EdenParticleSystemDesc::Curve *EdenParticleSystemDesc::findCurve( uint32_t id ) const {
    for ( const Curve &curve : Curves ) {
        if ( curve.Id == id ) {
            return &curve;
        }
    }
    return nullptr;
}

float EdenParticleSystemDesc::evaluate( uint32_t id, float t, float missing ) const {
    const Curve *pCurve = findCurve( id );
    if ( pCurve == nullptr ) {
        return missing;
    }
    const float position = Clamp( t, 0.0f, 1.0f ) * static_cast<float>( kCurveSamples - 1 );
    const uint32_t index = Min( static_cast<uint32_t>( position ), kCurveSamples - 2 );
    const float blend = position - static_cast<float>( index );
    return pCurve->Samples[index] + ( pCurve->Samples[index + 1] - pCurve->Samples[index] ) * blend;
}

void EdenParticleEmitter::initialize( const EdenParticleSystemDesc *pSystem, const Matrix4x4f &frame,
                                      uint32_t randomSeed ) {
    pDesc = pSystem;
    world = frame * pSystem->EmitterLocal;
    origin = world.getTranslation();
    direction = world.transformVector( pSystem->Direction );
    if ( direction.length() < 1e-6f ) {
        direction = Vector3f( 0.0f, 1.0f, 0.0f );
    }
    direction = direction.normalized();
    particles.clear();
    particles.reserve( pSystem->MaxParticles );
    accumulator = 0.0f;
    emission = 0.0f;
    seed = randomSeed != 0 ? randomSeed : 1;
}

float EdenParticleEmitter::random() {
    // Game LCG (FUN_0095b3e0 [CODE]: seed * 0x7ff8a3ed + 0x2aa01d31); its
    // use by the particles is [ENC].
    seed = seed * 0x7ff8a3edu + 0x2aa01d31u;
    return static_cast<float>( seed >> 8 ) * ( 1.0f / 16777216.0f );
}

float EdenParticleEmitter::randomSigned() { return random() * 2.0f - 1.0f; }

void EdenParticleEmitter::emit() {
    const EdenParticleSystemDesc &desc = *pDesc;
    if ( particles.size() >= desc.MaxParticles ) {
        return;
    }
    Particle particle;
    // Spawn point: shape 7 (fountains, most car fx) is a point; the other
    // shapes spread over the emitter volume (VolumeSize, emitter matrix) [INF].
    Vector3f local;
    if ( desc.Shape != 7 ) {
        local = Vector3f( randomSigned() * 0.5f, randomSigned() * 0.5f, randomSigned() * 0.5f );
    }
    particle.Position = world.transformPoint( local );
    // Direction: within the cone of half angle ConeAngle (or ConeAngle2)
    // around the emitter direction [INF].
    const float cone = desc.ConeAngle > 0.0f ? desc.ConeAngle : desc.ConeAngle2;
    Vector3f axis = direction;
    Vector3f side = std::fabs( axis.y ) < 0.99f ? axis.cross( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized()
                                                 : axis.cross( Vector3f( 1.0f, 0.0f, 0.0f ) ).normalized();
    Vector3f up = side.cross( axis );
    const float cosAngle = 1.0f - random() * ( 1.0f - std::cos( cone ) );
    const float sinAngle = std::sqrt( Max( 0.0f, 1.0f - cosAngle * cosAngle ) );
    const float turn = random() * 2.0f * kPI;
    const Vector3f velocityDirection =
        axis * cosAngle + side * ( sinAngle * std::cos( turn ) ) + up * ( sinAngle * std::sin( turn ) );
    const float speed = desc.Speed + randomSigned() * desc.SpeedRandom;
    particle.Velocity = velocityDirection * speed;
    const float driftBlend = random();
    particle.Drift = desc.DriftMin + ( desc.DriftMax - desc.DriftMin ) * driftBlend;
    particle.Life = Max( desc.Life + randomSigned() * desc.LifeRandom, desc.Step );
    particle.Size = Max( desc.Size + randomSigned() * desc.SizeRandom, 0.0f );
    particle.Angle = random() * desc.RotationRandom;
    particle.Spin = desc.SpinMin + ( desc.SpinMax - desc.SpinMin ) * random();
    particle.Frame = static_cast<uint32_t>( random() * static_cast<float>( desc.NumRandomFrames ) ) %
                     desc.NumRandomFrames;
    particles.push_back( particle );
}

void EdenParticleEmitter::step( float dt ) {
    const EdenParticleSystemDesc &desc = *pDesc;
    // Ageing / motion: v += acceleration dt, p += (v + drift) dt [INF].
    for ( size_t i = 0; i < particles.size(); ) {
        Particle &particle = particles[i];
        particle.Age += dt;
        if ( particle.Age >= particle.Life ) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        particle.Velocity += desc.Acceleration * dt;
        particle.Position += ( particle.Velocity + particle.Drift ) * dt;
        particle.Angle += particle.Spin * dt;
        i++;
    }
    // Emission: Rate per second, PerEmission particles each time.
    emission += desc.Rate * dt;
    while ( emission >= 1.0f ) {
        emission -= 1.0f;
        for ( uint32_t i = 0; i < desc.PerEmission; i++ ) {
            emit();
        }
    }
}

void EdenParticleEmitter::update( float deltaTime ) {
    if ( pDesc == nullptr ) {
        return;
    }
    const float stepTime = pDesc->Step > 0.0f ? pDesc->Step : 0.02f;
    accumulator += Min( deltaTime, 0.25f );
    while ( accumulator >= stepTime ) {
        accumulator -= stepTime;
        step( stepTime );
    }
}

EdenParticleLibrary::~EdenParticleLibrary() { clear(); }

void EdenParticleLibrary::clear() {
    if ( gpRender != nullptr ) {
        for ( auto &entry : systems ) {
            if ( entry.second != nullptr ) {
                gpRender->getDevice().destroyBuffer( entry.second->pIndexBuffer );
            }
        }
    }
    systems.clear();
}

const EdenParticleLibrary::System *EdenParticleLibrary::getSystem( EdenRenderer &renderer, const Bank &bank,
                                                                   const BankHashcodes &hashcodes, uint64_t prtRsc,
                                                                   uint64_t materialRsc ) {
    auto it = systems.find( prtRsc );
    if ( it != systems.end() ) {
        return it->second.get();
    }
    std::unique_ptr<System> &slot = systems[prtRsc];
    const BankEntry *pPrt = hashcodes.find( prtRsc, 0, 18 );
    if ( pPrt == nullptr ) {
        return nullptr;
    }
    auto pSystem = std::make_unique<System>();
    if ( !pSystem->Desc.parse( pPrt->pData, pPrt->Size ) ) {
        OTDU_LOG_WARN( "EdenParticleLibrary: '%s' not a particle system\n", pPrt->Filename.c_str() );
        return nullptr;
    }
    const BankEntry *pMaterials = materialRsc != 0 ? hashcodes.find( materialRsc, 0, 2 ) : nullptr;
    if ( pMaterials == nullptr ) {
        pMaterials = bank.findEntry( pPrt->Name.c_str(), "2dm" );
    }
    const MaterialDesc *pMaterial = nullptr;
    if ( pMaterials != nullptr && pSystem->Materials.parse( pMaterials->pData, pMaterials->Size ) ) {
        pMaterial = pSystem->Materials.find( pSystem->Desc.MaterialHash );
        if ( pMaterial == nullptr && !pSystem->Materials.getOrderedMaterials().empty() ) {
            pMaterial = &pSystem->Materials.getOrderedMaterials().front();
        }
    }
    if ( pMaterial == nullptr ) {
        OTDU_LOG_WARN( "EdenParticleLibrary: '%s': no material\n", pPrt->Filename.c_str() );
        return nullptr;
    }
    renderer.setupMaterial( *pMaterial, pSystem->Material, bank.getPath().c_str() );
    const EdenProgram *pProgram = pSystem->Material.pProgram;
    if ( pProgram == nullptr ) {
        OTDU_LOG_WARN( "EdenParticleLibrary: '%s': no TD_Particle program\n", pPrt->Filename.c_str() );
        return nullptr;
    }
    // Soft particles (TD_Particle ps 2 / 3, S_USERTYPE4 = spec 35): sampler
    // s1 gZpass is not a .2DM slot (1 slot: COLOR). It reads the
    // FullscreenDepthTarget encoding exactly (B / A, constants 255, 31.875,
    // 0.125, -15 [SHADER] = postfx_spec 6.2): bound at draw time to the
    // screen depth buffer [INF: the runtime binding is in the encrypted
    // edFxParticles render]. Point sampling: the encoded value must not be
    // filtered (as the haze reads it).
    if ( ( pProgram->PSSamplerMask & 2u ) != 0 && pSystem->Material.pTextures[1] == nullptr &&
         pSystem->Material.NumSlots < 2 ) {
        pSystem->Material.ScreenDepthSlot = 1;
        pSystem->Material.SamplerIndex[1] = 3; // point clamp (eSamplerHeapIndex SH_PointClamp)
    }
    // Static quad index list (2 triangles per particle).
    std::vector<uint32_t> indices( pSystem->Desc.MaxParticles * 6 );
    for ( uint32_t i = 0; i < pSystem->Desc.MaxParticles; i++ ) {
        const uint32_t base = i * 4;
        const uint32_t quad[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
        memcpy( &indices[i * 6], quad, sizeof( quad ) );
    }
    pSystem->pIndexBuffer = gpRender->getDevice().createBuffer( indices.size() * sizeof( uint32_t ),
                                                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );
    OTDU_LOG_DEBUG( "EdenParticleLibrary: %s: rate %.1f/s life %.2f s speed %.1f size %.2f max %u, %u curves, "
                    "program %s%s\n",
                    pPrt->Filename.c_str(), pSystem->Desc.Rate, pSystem->Desc.Life, pSystem->Desc.Speed,
                    pSystem->Desc.Size, pSystem->Desc.MaxParticles,
                    static_cast<uint32_t>( pSystem->Desc.Curves.size() ), pProgram->Name.c_str(),
                    pSystem->Material.ScreenDepthSlot >= 0 ? " (soft: screen depth in s1)" : "" );
    slot = std::move( pSystem );
    return slot.get();
}

std::vector<const EdenParticleLibrary::System *>
EdenParticleLibrary::getSourceSystems( EdenRenderer &renderer, const Bank &bank, const BankHashcodes &hashcodes,
                                       const XmbDocument &entities, uint64_t sourceHash ) {
    std::vector<const System *> result;
    XmbValue root = entities.getRoot();
    auto findByHash = [&]( const char *pExport, const char *pArray, uint64_t hash ) -> XmbValue {
        XmbValue exportType = root[pExport];
        for ( uint32_t i = 0; i < exportType.getArraySize( pArray ); i++ ) {
            XmbValue entry = exportType.getArrayElement( pArray, i );
            if ( entry["__Hc"].asUInt64() == hash ) {
                return entry;
            }
        }
        return XmbValue();
    };
    auto firstRsc = []( XmbValue entry, const char *pField, int32_t subType ) -> uint64_t {
        for ( uint32_t j = 0; j < entry.getArraySize( pField ); j++ ) {
            XmbValue rsc = entry.getArrayElement( pField, j );
            if ( subType < 0 || rsc["rscSubType"].asInt() == subType ) {
                return rsc["rsc"].asUInt64();
            }
        }
        return 0;
    };
    XmbValue source = findByHash( "ExportType_FxSourceData", "ArrayOf_Fx__SourceData", sourceHash );
    if ( !source.isValid() ) {
        return result;
    }
    for ( uint32_t j = 0; j < source.getArraySize( "ParticlesList" ); j++ ) {
        const uint64_t node = source.getArrayElement( "ParticlesList", j )["ParticlesNode"].asUInt64();
        XmbValue particles = findByHash( "ExportType_FxParticles", "ArrayOf_Fx__Particles", node );
        if ( !particles.isValid() ) {
            continue;
        }
        if ( const System *pSystem =
                 getSystem( renderer, bank, hashcodes, firstRsc( particles, "Prt", 18 ), firstRsc( particles, "PrtMat", 2 ) ) ) {
            result.push_back( pSystem );
        }
    }
    return result;
}

void EdenParticleInstance::initialize( const EdenParticleLibrary::System *pNewSystem, const Matrix4x4f &world,
                                       uint32_t seed ) {
    pSystem = pNewSystem;
    emitter.initialize( &pSystem->Desc, world, seed );
    frameNumber = ~0ull;
}

void EdenParticleInstance::draw( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    RenderDevice &device = gpRender->getDevice();
    const auto &particles = emitter.getParticles();
    if ( pSystem == nullptr || particles.empty() ) {
        return;
    }
    const EdenParticleLibrary::System &system = *pSystem;
    const EdenParticleSystemDesc &desc = system.Desc;
    const Vector3f origin = emitter.getWorld().getTranslation();
    // Vertices once per frame (the scene may be drawn several times:
    // environment cube faces, main view).
    if ( frameNumber != device.getFrameNumber() ) {
        const uint32_t count = static_cast<uint32_t>( particles.size() );
        TransientAllocation allocation = device.allocateTransient( count * 4 * sizeof( ParticleVertex ), 16 );
        if ( allocation.pData == nullptr ) {
            return;
        }
        // Back to front inside the emitter [INF].
        std::vector<uint32_t> order( count );
        std::vector<float> distance( count );
        for ( uint32_t i = 0; i < count; i++ ) {
            order[i] = i;
            distance[i] = ( particles[i].Position - cameraPosition ).length();
        }
        std::sort( order.begin(), order.end(), [&]( uint32_t a, uint32_t b ) { return distance[a] > distance[b]; } );
        ParticleVertex *pVertex = static_cast<ParticleVertex *>( allocation.pData );
        static const float kCorners[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        for ( uint32_t i = 0; i < count; i++ ) {
            const EdenParticleEmitter::Particle &particle = particles[order[i]];
            const float t = particle.Age / particle.Life;
            // Curves (0..255) [DATA ids, INF use]: size x / y scale the
            // generator size, rgb + alpha give the vertex colour.
            const float halfX = particle.Size * 0.5f * desc.evaluate( 9, t, 255.0f ) / 255.0f;
            const float halfY = particle.Size * 0.5f * desc.evaluate( 10, t, desc.evaluate( 9, t, 255.0f ) ) / 255.0f;
            const uint32_t color = ( ToByte( desc.evaluate( 12, t, 255.0f ) ) << 24 ) |
                                   ( ToByte( desc.evaluate( 1, t, 255.0f ) ) << 16 ) |
                                   ( ToByte( desc.evaluate( 2, t, 255.0f ) ) << 8 ) |
                                   ToByte( desc.evaluate( 3, t, 255.0f ) );
            uint32_t frame = particle.Frame;
            if ( desc.FrameRate > 0.0f ) {
                frame += static_cast<uint32_t>( particle.Age * desc.FrameRate );
            }
            frame %= desc.NumFrames;
            const float cellU = static_cast<float>( frame % desc.AtlasColumns ) * desc.CellWidth;
            const float cellV = static_cast<float>( ( frame / desc.AtlasColumns ) % desc.AtlasRows ) * desc.CellHeight;
            const Vector3f center = particle.Position - origin;
            // vs 8..11 axis: the particle motion direction [INF].
            const float speed = particle.Velocity.length();
            const Vector3f axis = speed > 1e-4f ? particle.Velocity * ( 1.0f / speed ) : Vector3f( 0.0f, 1.0f, 0.0f );
            for ( uint32_t c = 0; c < 4; c++ ) {
                ParticleVertex &vertex = pVertex[i * 4 + c];
                vertex.Position[0] = center.x;
                vertex.Position[1] = center.y;
                vertex.Position[2] = center.z;
                vertex.HalfSize[0] = halfX;
                vertex.HalfSize[1] = halfY;
                vertex.HalfSize[2] = 0.0f;
                vertex.Corner[0] = kCorners[c][0];
                vertex.Corner[1] = kCorners[c][1];
                vertex.Angle[0] = particle.Angle;
                vertex.Angle[1] = 0.0f;
                vertex.Color = color;
                vertex.Texcoord[0] = cellU + kCorners[c][0] * desc.CellWidth;
                vertex.Texcoord[1] = cellV + kCorners[c][1] * desc.CellHeight;
                vertex.Axis[0] = axis.x;
                vertex.Axis[1] = axis.y;
                vertex.Axis[2] = axis.z;
            }
        }
        stream.Buffer = allocation.Buffer;
        stream.Size = count * 4 * sizeof( ParticleVertex );
        mesh = EdenMesh();
        mesh.NumStreams = 1;
        mesh.pStreamBuffers[0] = &stream;
        mesh.StreamOffsets[0] = allocation.Offset;
        mesh.Strides[0] = sizeof( ParticleVertex );
        auto addAttribute = [&]( uint8_t usage, uint8_t usageIndex, eVertexAttributeFormat format, uint16_t offset ) {
            RawVertexAttribute attribute;
            attribute.Usage = usage;
            attribute.UsageIndex = usageIndex;
            attribute.Format = format;
            attribute.Offset = offset;
            mesh.Attributes.push_back( attribute );
        };
        addAttribute( 0, 0, VAF_POSITION_R32G32B32_SFLOAT, offsetof( ParticleVertex, Position ) );
        addAttribute( 3, 0, VAF_NORMAL_R32G32B32_SFLOAT, offsetof( ParticleVertex, HalfSize ) );
        addAttribute( 5, 1, VAF_TEXCOORD_R32G32_SFLOAT, offsetof( ParticleVertex, Corner ) );
        addAttribute( 5, 2, VAF_TEXCOORD_R32G32_SFLOAT, offsetof( ParticleVertex, Angle ) );
        addAttribute( 10, 0, VAF_COLOR_B8G8R8A8_UNORM, offsetof( ParticleVertex, Color ) );
        addAttribute( 5, 0, VAF_TEXCOORD_R32G32_SFLOAT, offsetof( ParticleVertex, Texcoord ) );
        addAttribute( 7, 0, VAF_TANGENT_R32G32B32_SFLOAT, offsetof( ParticleVertex, Axis ) );
        mesh.pIndexBuffer = system.pIndexBuffer;
        mesh.IndexCount = count * 6;
        mesh.computeLayoutKey();
        frameNumber = device.getFrameNumber();
    }
    if ( mesh.IndexCount != 0 ) {
        renderer.draw( mesh, system.Material, Matrix4x4f::Translation( origin ) );
    }
}

EdenParticleSet::~EdenParticleSet() { clear(); }

void EdenParticleSet::clear() {
    emitters.clear();
    library.clear();
}

uint32_t EdenParticleSet::load( EdenRenderer &renderer, const Bank &bank, const BankHashcodes &hashcodes,
                                const XmbDocument &entities, const std::vector<Matrix4x4f> &frames ) {
    XmbValue root = entities.getRoot();
    uint32_t numEmitters = 0;
    XmbValue entityExport = root["ExportType_Entity"];
    XmbValue fxExport = root["ExportType_Fx"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Fx" ); i++ ) {
        XmbValue entity = entityExport.getArrayElement( "ArrayOf_Entity__Fx", i );
        if ( !entity["AutoStart"].asBool( true ) ) {
            continue;
        }
        uint64_t sourceHash = 0;
        for ( uint32_t r = 0; r < fxExport.getArraySize( "ArrayOf_Fx__Root" ); r++ ) {
            XmbValue fxRoot = fxExport.getArrayElement( "ArrayOf_Fx__Root", r );
            if ( fxRoot["__Hc"].asUInt64() == entity["Fx"].asUInt64() ) {
                sourceHash = fxRoot["SourceData"].asUInt64();
                break;
            }
        }
        if ( sourceHash == 0 ) {
            continue;
        }
        XmbValue placement = entity["orient_pos_scale"];
        const Quaternionf rotation( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                    placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        const Vector3f position( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(),
                                 placement["pos"]["Z"].asFloat() );
        // The .bfx particle node of the fountains has an identity transform
        // [DATA vfx_casino_*.bfx +0x1b0]: the system sits at the entity.
        const Matrix4x4f local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation );
        for ( const EdenParticleLibrary::System *pSystem :
              library.getSourceSystems( renderer, bank, hashcodes, entities, sourceHash ) ) {
            for ( size_t f = 0; f < frames.size(); f++ ) {
                auto pInstance = std::make_unique<EdenParticleInstance>();
                const uint32_t seed =
                    static_cast<uint32_t>( entity["__Hc"].asUInt64() ) ^ static_cast<uint32_t>( f * 0x9e3779b9u );
                pInstance->initialize( pSystem, frames[f] * local, seed );
                emitters.push_back( std::move( pInstance ) );
                numEmitters++;
            }
        }
    }
    return numEmitters;
}

void EdenParticleSet::update( float deltaTime ) {
    for ( auto &pInstance : emitters ) {
        pInstance->update( deltaTime );
    }
}

void EdenParticleSet::draw( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    for ( auto &pInstance : emitters ) {
        pInstance->draw( renderer, cameraPosition );
    }
}
