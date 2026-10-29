#include "shared.h"
#include "eden_rain.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "world/eden_world.h"

namespace {
constexpr const char *kWeatherFxOwner = "Euro/Bnk/fx/weatherfx.bnk";
constexpr uint64_t kTD_Rain = 0xe1b3c51172e345ffull;    // FUN_00de2be0 push 0xe1b3c511 / 0x72e345ff
constexpr uint64_t kTD_Gouraud = 0x73078865ca37336dull; // FUN_00aa5aa0 / FUN_00aa4ab0

// WFX_Rain (FUN_00de1f40, FUN_00dbed00): block size 75 (DAT_00f4a39c), quad
// corners +-37.5 (DAT_00f4acf8 / DAT_00f4acfc), height 75.
constexpr float kBlockSize = 75.0f;
constexpr float kBlockHalf = 37.5f;
// +0x898 = f(2 * 75^2) * 0.5, f = CRT thunk 0x6c8d3e [ENC], sqrt [INF]:
// half the block diagonal.
const float kBlockLead = std::sqrt( 2.0f * kBlockSize * kBlockSize ) * 0.5f;
// Distance fade of the block slant (FUN_00de24f0): |centre - camera| / 100.
// Slant = lerp(0, v * 200 * DAT_0117ac84 / 30, t) [FUN_0045b810 ENC]; the
// car speed factor DAT_0117ac84 has no readable writer -> 0 [INF].
constexpr float kCarSpeedFactor = 0.0f;

// WFX_Drop ctor FUN_00aa5940.
constexpr int32_t kDropBox = 40;          // +0x7b74
constexpr float kDropPeriod = 0.05f;      // +0x7b48 (DAT_00eded2c)
constexpr float kDropLead = 12.0f;        // +0x7b4c (DAT_00eae5bc)
constexpr float kDropFall = 3.5f;         // +0x7b50 (DAT_00f4ad80) * 9.81 (DAT_00f4a440)
constexpr float kDropLeadScale = 1.0f;    // +0x7b54
constexpr float kDropLength = 1.5f;       // +0x7b58 (DAT_00edf2cc)
constexpr float kDropWidth = 0.06f;       // +0x7b5c (DAT_00f4af88)
constexpr float kDropBoost = 3.0f;        // +0x7b60
constexpr int32_t kDropSpawn = 50;        // +0x7b68
constexpr int32_t kDropMax = 200;         // +0x7b70
constexpr float kDropKillMax = 20.0f;     // DAT_00eddfd8

// WFX_Splash ctor FUN_00aa4930 / FUN_00aa3960.
constexpr float kSplashWidth = 25.0f;     // +0xad8 (DAT_00edd53c)
constexpr float kZoneSize = 8.333333f;    // DAT_00f4a280 / DAT_00f4a278 (-), weight DAT_00f4a288
constexpr float kZoneHalf = 4.1666665f;   // DAT_00f4b338
constexpr int32_t kSplashFrames = 9;      // +0x4d3c
constexpr float kSplashCell = 1.0f / 3.0f; // +0x4d44 (DAT_00f4a330)
constexpr int32_t kSplashSpawn = 20;      // +0x4d50
constexpr float kSplashPeriod = 0.01f;    // +0x4d58 (DAT_00ede410)
constexpr float kSplashSize = 0.35f;      // +0x4d5c (DAT_00f7e828)
constexpr float kSplashLife = 0.35f;      // +0x4d60 (DAT_00f7e828)

constexpr uint32_t kRainColor = 0x00c8c8c8; // (200, 200, 200), D3DCOLOR

inline uint32_t MakeColor( int32_t alpha ) { return ( static_cast<uint32_t>( alpha & 0xff ) << 24 ) | kRainColor; }

// fmod of the drop box (FUN_00aa4c90): integer remainder in 1/100 m.
inline float BoxModulo( float value, int32_t box ) {
    const int32_t scaled = static_cast<int32_t>( value * 100.0f ); // _ftol (truncation)
    return static_cast<float>( scaled % ( box * 100 ) ) / 100.0f;
}

void FillVertex( void *pOut, const Vector3f &position, uint32_t color, float u, float v ) {
    float data[6];
    data[0] = position.x;
    data[1] = position.y;
    data[2] = position.z;
    memcpy( &data[3], &color, 4 );
    data[4] = u;
    data[5] = v;
    memcpy( pOut, data, sizeof( data ) );
}

void SetupMesh( EdenMesh &mesh, GPUBuffer &stream, const TransientAllocation &allocation, uint32_t size,
                GPUBuffer *pIndices, uint32_t indexCount ) {
    stream.Buffer = allocation.Buffer;
    stream.Size = size;
    mesh = EdenMesh();
    mesh.NumStreams = 1;
    mesh.pStreamBuffers[0] = &stream;
    mesh.StreamOffsets[0] = allocation.Offset;
    mesh.Strides[0] = 24;
    auto addAttribute = [&]( uint8_t usage, eVertexAttributeFormat format, uint16_t offset ) {
        RawVertexAttribute attribute;
        attribute.Usage = usage;
        attribute.UsageIndex = 0;
        attribute.Format = format;
        attribute.Offset = offset;
        mesh.Attributes.push_back( attribute );
    };
    addAttribute( 0, VAF_POSITION_R32G32B32_SFLOAT, 0 );
    addAttribute( 10, VAF_COLOR_B8G8R8A8_UNORM, 12 );
    addAttribute( 5, VAF_TEXCOORD_R32G32_SFLOAT, 16 );
    mesh.pIndexBuffer = pIndices;
    mesh.IndexCount = indexCount;
    mesh.computeLayoutKey();
}
} // namespace

EdenRain::~EdenRain() { destroy(); }

float EdenRain::random( uint32_t &seed ) const {
    // seed = seed * 0x7ff8a3ed + 0x2aa01d31; (float)(uint)seed * 2^-32
    // (DAT_00f49a78; + 2^32 DAT_00f496e8 when the signed value is negative).
    seed = seed * 0x7ff8a3edu + 0x2aa01d31u;
    return static_cast<float>( static_cast<double>( seed ) * 2.3283064365386963e-10 );
}

bool EdenRain::setupMaterial( EdenRenderer &renderer, EdenMaterial &material, const char *pName, uint64_t guid,
                              const uint8_t *pSpec, const char *pTexture, uint8_t address, uint8_t alphaTest ) {
    // FUN_008b8450(material, name, guid, specs, states...) then the state
    // bytes written by the WFX objects (MAT. +0x30..+0x3a).
    MaterialDesc desc;
    desc.Name = pName;
    desc.Hashcode = GetIdentifier64bit( pName );
    desc.ShaderSetGUID = guid;
    memcpy( desc.Spec, pSpec, sizeof( desc.Spec ) );
    uint8_t *pStates = desc.States;
    pStates[0x0] = 1;         // +0x30
    pStates[0x1] = 2;         // +0x31: no depth write
    pStates[0x2] = 0;         // +0x32
    pStates[0x3] = 3;         // +0x33: two sided
    pStates[0x5] = alphaTest; // +0x35 (0: alpha test on)
    pStates[0x7] = 0;         // +0x37: alpha reference (WFX_Rain +0x59bc = 0)
    pStates[0x8] = 5;         // +0x38 SRCALPHA
    pStates[0x9] = 6;         // +0x39 INVSRCALPHA
    pStates[0xa] = 1;         // +0x3a ADD
    desc.DepthTest = 1;
    desc.DepthWrite = 0;
    desc.CullMode = 3;
    desc.SrcBlend = 5;
    desc.DstBlend = 6;
    MaterialTextureSlot slot;
    slot.SlotType = GetIdentifier64bit( "COLOR" );
    slot.TextureHashcode = GetIdentifier64bit( pTexture );
    slot.AddressU = address; // slot +0x18 / +0x19 (FUN_00de2be0: 1, 1; drops / splashes: 2, 2)
    slot.AddressV = address;
    desc.Textures.push_back( slot );
    desc.AllTextures.push_back( slot );
    renderer.setupMaterial( desc, material, kWeatherFxOwner );
    if ( material.pProgram == nullptr || material.pTextures[0] == nullptr ) {
        OTDU_LOG_WARN( "EdenRain: material '%s' not ready (program %p, texture %s %p)\n", pName,
                       static_cast<const void *>( material.pProgram ), pTexture,
                       static_cast<void *>( material.pTextures[0] ) );
        return false;
    }
    return true;
}

bool EdenRain::initialize( EdenRenderer &renderer, const std::string &islandFolder ) {
    destroy();
    uint8_t rainSpec[EdenShaderSystem::kNumSpecs] = {};
    rainSpec[0] = 2;  // S_NBTEXTURES (RAIN_FAR, RAIN_ALP)
    rainSpec[1] = 2;  // S_COORDTEXTURE
    rainSpec[33] = 1; // S_USERTYPE2
    uint8_t gouraudSpec[EdenShaderSystem::kNumSpecs] = {};
    gouraudSpec[0] = 1;  // S_NBTEXTURES
    gouraudSpec[1] = 1;  // S_COORDTEXTURE
    gouraudSpec[10] = 1; // S_NBPRELIGHT (vertex colour)
    bool bOk = setupMaterial( renderer, rainMaterial, "RainGrp", kTD_Rain, rainSpec, "RAIN_FAR", 1, 0 );
    bOk &= setupMaterial( renderer, dropMaterial, "RainDrop", kTD_Gouraud, gouraudSpec, "DROP", 2, 1 );
    bOk &= setupMaterial( renderer, splashMaterial, "RainSplash", kTD_Gouraud, gouraudSpec, "SPLASH", 2, 1 );
    bMaterials = bOk;

    // Index lists: per quad (v0, v1, v2) (v2, v1, v3) = the strips of the
    // original (FUN_00aa4eb0 / FUN_00aa3d80: n, n+1, n+2, n+3 + degenerate
    // joints; the rain blocks: FUN_0042b5b0 per quad).
    std::vector<uint32_t> indices( 500 * 6 );
    for ( uint32_t i = 0; i < 500; i++ ) {
        const uint32_t base = i * 4;
        const uint32_t quad[6] = { base, base + 1, base + 2, base + 2, base + 1, base + 3 };
        memcpy( &indices[i * 6], quad, sizeof( quad ) );
    }
    RenderDevice &device = gpRender->getDevice();
    pQuadIndices = device.createBuffer( indices.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indices.data() );
    pBlockIndices = device.createBuffer( 24 * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );

    // Forced rain: the island time keys with all their intemperies.
    gameWeathers.load( ( islandFolder + "fx/game_weathers.xmb" ).c_str() );
    rainSystem.setLibrary( &gameWeathers );
    rainSystem.setIntemperies( 2, 2 ); // Rainny (exe table 0x00f7e08c)

    // Splash zones (FUN_00aa3960): 3 columns x 5 rows of 8.33 m ahead of
    // the car, x = (col + 0.5) * 8.33 - 25 * 0.5, z = (row + 0.5) * -8.33.
    for ( uint32_t i = 0; i < 15; i++ ) {
        const uint32_t column = i % 3;
        const uint32_t row = i / 3;
        zones[i].Local = Vector3f( ( static_cast<float>( column ) + 0.5f ) * kZoneSize - kSplashWidth * 0.5f, 0.0f,
                                   ( static_cast<float>( row ) + 0.5f ) * -kZoneSize );
    }
    loadSound( islandFolder );
    bInitialized = true;
    OTDU_LOG_INFO( "EdenRain: materials %s, %zu time keys for the forced rain, sound %s\n", bOk ? "ok" : "missing",
                   gameWeathers.getPresets().size(), rainClip != nullptr ? rainClip->Name.c_str() : "(none)" );
    return bOk;
}

bool EdenRain::loadSound( const std::string &islandFolder ) {
    // ambiences.xmb (ASTExport { Environment[]; ReverbPreset[];
    // LightningExport[] Lightning; RainResource[] Rain }), RainResource {
    // Curve { PointF[] Points; Color; Visible }; Color CurveColor; Int32
    // CurveColorARGB; String Name; String Path; Single Volume, Pitch }.
    // The file of a resource is "%sIslands/%s/Sound/Ambience/%s.wav" with
    // its Name (FUN_00cbf3e0) [CODE].
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( ( islandFolder + "sound/ambience/ambiences.xmb" ).c_str(), buffer ) ) {
        return false;
    }
    XmbDocument document;
    if ( !document.load( buffer ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    if ( root.getArraySize( "Rain" ) == 0 ) {
        return false;
    }
    XmbValue rain = root.getArrayElement( "Rain", 0 );
    // The packed layout (Curve 9 bytes: Points[] + Visible, CurveColor at
    // +9, CurveColorARGB +13, Name +17, Path +21, Volume +25, Pitch +29) is
    // read 4 bytes too far by XmbValue (Name -> Path...). Ibiza [DATA, raw
    // bytes at data +0x561]: Name "Rain_Mid", Volume 1.0, Pitch 1.0, curve
    // (0, 0) (1, 1). Name = the file name of whichever string is read.
    std::string name = rain["Name"].asString();
    const size_t slash = name.find_last_of( "\\/" );
    if ( slash != std::string::npos ) {
        name = name.substr( slash + 1 );
    }
    const size_t dot = name.rfind( '.' );
    if ( dot != std::string::npos ) {
        name = name.substr( 0, dot );
    }
    const float volume = rain["Volume"].asFloat( 1.0f );
    const float pitch = rain["Pitch"].asFloat( 1.0f );
    soundVolume = volume >= 0.0f && volume <= 4.0f ? volume : 1.0f;
    soundPitch = pitch >= 0.25f && pitch <= 4.0f ? pitch : 1.0f;
    XmbValue curve = rain["Curve"];
    soundCurve.clear();
    for ( uint32_t i = 0; i < curve.getArraySize( "Points" ); i++ ) {
        XmbValue point = curve.getArrayElement( "Points", i );
        soundCurve.emplace_back( point["X"].asFloat(), point["Y"].asFloat() );
    }
    std::string curveText;
    for ( const auto &point : soundCurve ) {
        char text[48];
        snprintf( text, sizeof( text ), " (%g, %g)", point.first, point.second );
        curveText += text;
    }
    OTDU_LOG_INFO( "EdenRain: rain sound '%s' volume %g pitch %g curve%s\n", name.c_str(), soundVolume, soundPitch,
                   curveText.c_str() );
    if ( gpAudio == nullptr || name.empty() ) {
        return false;
    }
    std::vector<uint8_t> wav;
    if ( !gpFile->loadFile( ( islandFolder + "sound/ambience/" + ToLower( name ) + ".wav" ).c_str(), wav ) ) {
        OTDU_LOG_WARN( "EdenRain: rain sound '%s.wav' not found\n", name.c_str() );
        return false;
    }
    rainClip = gpAudio->loadClip( wav.data(), wav.size(), name.c_str() );
    return rainClip != nullptr;
}

void EdenRain::destroy() {
    stopSound();
    rainClip.reset();
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        if ( pQuadIndices != nullptr ) {
            device.destroyBuffer( pQuadIndices );
        }
        if ( pBlockIndices != nullptr ) {
            device.destroyBuffer( pBlockIndices );
        }
    }
    pQuadIndices = nullptr;
    pBlockIndices = nullptr;
    bMaterials = false;
    bGrid = false;
    bActive = false;
    bInitialized = false;
    for ( Drop &drop : drops ) {
        drop = Drop();
    }
    for ( Splash &splash : splashes ) {
        splash = Splash();
    }
    dropAlive = splashAlive = 0;
}

void EdenRain::stopSound() {
    if ( gpAudio != nullptr && voice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( voice );
    }
    voice = GSAudio::kInvalidVoice;
}

const WeatherPreset *EdenRain::resolveWeather( const WeatherPreset *pWeather, bool bEnabled ) {
    bActive = bEnabled && bInitialized;
    if ( !bActive || pWeather == nullptr ) {
        return pWeather;
    }
    if ( pWeather->getFloat( "rain_alpha", 0.0f ) > 0.0f || !rainSystem.isReady() ) {
        return pWeather;
    }
    // Dry weather: the rain values of the Rainny intempery at the same time.
    rainSystem.setTimeOfDay( static_cast<float>( pWeather->Hour * 3600 + pWeather->Minutes * 60 ) );
    rainSystem.evaluate();
    const WeatherPreset &rainny = rainSystem.getCurrent();
    resolved = *pWeather;
    for ( const auto &value : rainny.Values ) {
        if ( value.first.rfind( "rain_", 0 ) == 0 || value.first.rfind( "thunder_", 0 ) == 0 ) {
            resolved.Values[value.first] = value.second;
        }
    }
    return &resolved;
}

void EdenRain::initBlock( RainBlock &block, const Vector3f &center ) {
    // FUN_00dbe5d0: centre, 8 random uv offsets in [0, 1).
    block.Center = center;
    for ( uint32_t q = 0; q < 4; q++ ) {
        block.U[q] = random( block.Seed );
        block.V[q] = random( block.Seed );
    }
    block.bValid = true;
}

void EdenRain::layGrid( const Frame &frame, const EdenWorld &world ) {
    // FUN_00de1f40: grid point ahead of the camera, -camera matrix z row
    // (+0x70 / +0x78) * half diagonal = forward * 53 m [INF: -z = forward
    // as for the ocean mirror camera], snapped to 75 m from the origin of
    // the camera sector (FUN_00da6d40) with truncation (_ftol).
    const Vector3f lead = frame.Position + Vector3f( frame.Forward.x, 0.0f, frame.Forward.z ) * kBlockLead;
    const Vector3f origin = EdenWorld::getSectorOrigin( EdenWorld::getSectorAt( frame.Position ) );
    const float x0 = origin.x + static_cast<float>( static_cast<int32_t>( ( lead.x - origin.x ) / kBlockSize ) ) * kBlockSize;
    const float z0 = origin.z + static_cast<float>( static_cast<int32_t>( ( lead.z - origin.z ) / kBlockSize ) ) * kBlockSize;
    static const float kOffsets[9][2] = { { 0, 0 },  { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
                                          { 1, 0 }, { -1, 1 },  { 0, 1 },  { 1, 1 } };
    for ( uint32_t i = 0; i < 9; i++ ) {
        RainBlock &block = blocks[i];
        initBlock( block, Vector3f( x0 + kOffsets[i][0] * kBlockSize, 0.0f, z0 + kOffsets[i][1] * kBlockSize ) );
        // 4 crossing vertical quads (A top corner, B bottom corner), the
        // same for every block: (-37.5, 37.5) -> (0, -37.5), (0, 37.5) ->
        // (37.5, -37.5), (-37.5, -37.5) -> (37.5, 0), (-37.5, 0) -> (37.5,
        // 37.5), height 75.
        block.A[0] = Vector3f( -kBlockHalf, kBlockSize, kBlockHalf );
        block.B[0] = Vector3f( 0.0f, 0.0f, -kBlockHalf );
        block.A[1] = Vector3f( 0.0f, kBlockSize, kBlockHalf );
        block.B[1] = Vector3f( kBlockHalf, 0.0f, -kBlockHalf );
        block.A[2] = Vector3f( -kBlockHalf, kBlockSize, -kBlockHalf );
        block.B[2] = Vector3f( kBlockHalf, 0.0f, 0.0f );
        block.A[3] = Vector3f( -kBlockHalf, kBlockSize, 0.0f );
        block.B[3] = Vector3f( kBlockHalf, 0.0f, kBlockHalf );
    }
    (void)world;
    bGrid = true;
}

void EdenRain::recenterGrid( int32_t newCenter ) {
    // FUN_00de19f0: the block containing the camera becomes block 0; the
    // blocks still on the new 3 x 3 grid are kept (with their uv offsets),
    // the others are moved to the free slots (FUN_00dbe5d0: new offsets).
    const Vector3f center = blocks[newCenter].Center;
    static const float kOffsets[9][2] = { { 0, 0 },  { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
                                          { 1, 0 }, { -1, 1 },  { 0, 1 },  { 1, 1 } };
    RainBlock old[9];
    for ( uint32_t i = 0; i < 9; i++ ) {
        old[i] = blocks[i];
    }
    bool bUsed[9] = {};
    bool bFilled[9] = {};
    for ( uint32_t slot = 0; slot < 9; slot++ ) {
        const Vector3f target( center.x + kOffsets[slot][0] * kBlockSize, 0.0f,
                               center.z + kOffsets[slot][1] * kBlockSize );
        for ( uint32_t i = 0; i < 9; i++ ) {
            if ( !bUsed[i] && std::fabs( old[i].Center.x - target.x ) < 1.0f &&
                 std::fabs( old[i].Center.z - target.z ) < 1.0f ) {
                blocks[slot] = old[i];
                bUsed[i] = bFilled[slot] = true;
                break;
            }
        }
    }
    uint32_t next = 0;
    for ( uint32_t slot = 0; slot < 9; slot++ ) {
        if ( bFilled[slot] ) {
            continue;
        }
        while ( next < 9 && bUsed[next] ) {
            next++;
        }
        blocks[slot] = old[next < 9 ? next : 0];
        if ( next < 9 ) {
            bUsed[next] = true;
        }
        initBlock( blocks[slot], Vector3f( center.x + kOffsets[slot][0] * kBlockSize, 0.0f,
                                           center.z + kOffsets[slot][1] * kBlockSize ) );
    }
}

void EdenRain::updateBlocks( float deltaTime, const Frame &frame, const EdenWorld &world ) {
    if ( !bGrid ) {
        layGrid( frame, world );
    }
    // FUN_00de23c0 / FUN_00dbe3a0: camera inside block 0 (half size test on
    // x / z); else the block that contains it becomes the centre, none: new
    // grid.
    auto contains = [&]( const RainBlock &block ) {
        return block.Center.x - kBlockHalf <= frame.Position.x && frame.Position.x < block.Center.x + kBlockHalf &&
               block.Center.z - kBlockHalf <= frame.Position.z && frame.Position.z < block.Center.z + kBlockHalf;
    };
    if ( !contains( blocks[0] ) ) {
        int32_t found = -1;
        for ( int32_t i = 1; i < 9; i++ ) {
            if ( contains( blocks[i] ) ) {
                found = i;
                break;
            }
        }
        if ( found < 0 ) {
            layGrid( frame, world );
        } else {
            recenterGrid( found );
        }
    }
    // FUN_00de24f0: scroll speed (+0x59ac * DAT_0117ac84 + 1) * rain_scroll_speed.
    const float speed = ( 2.0f * kCarSpeedFactor + 1.0f ) * rainScroll;
    for ( RainBlock &block : blocks ) {
        // FUN_00dbe930: base = ground height under the centre
        // (FUN_00bd6c30), or camera y - 37.5 when the camera is higher than
        // 37.5 (+0x89c = 0).
        float ground = 0.0f;
        world.getHeight( block.Center.x, block.Center.z, ground );
        block.Base = frame.Position.y - 0.0f > kBlockHalf ? frame.Position.y - kBlockHalf : ground;
        block.Scroll += speed * deltaTime;
        if ( block.Scroll > rainTiling ) {
            block.Scroll -= std::floor( block.Scroll ); // CRT thunk 0x7c7f66 [INF: floor]
        }
    }
}

void EdenRain::updateDrops( float deltaTime, const Frame &frame, const EdenWorld &world ) {
    // FUN_00aa4eb0 (car frame -> camera frame [INF]): box centre C = car
    // position + car forward * 12 * 1.0.
    // The car z row is about horizontal: the camera forward is flattened so
    // that the box does not leave the view when the camera looks down [INF].
    const Vector3f flatForward = Vector3f( frame.Forward.x, 0.0f, frame.Forward.z ).normalized();
    const Vector3f center = frame.Position + flatForward * ( kDropLead * kDropLeadScale );
    dropRight = frame.Right;
    float ground = frame.Position.y;
    world.getSurfaceHeight( frame.Position.x, frame.Position.z, frame.Position.y + 1.0f, ground );
    // Kill height min(car y - DAT_011956c4, 20) [INF: DAT_011956c4 = the
    // ground height under the car].
    const float kill = Min( frame.Position.y - ground, kDropKillMax );
    const float fall = kDropFall * 9.81f * deltaTime * ( kDropBoost * kCarSpeedFactor + 1.0f );
    int32_t spawn = 0;
    dropTimer -= deltaTime;
    if ( dropTimer <= 0.0f && dropAlive <= kDropMax ) {
        spawn = Min( kDropSpawn, kDropMax - dropAlive );
        dropTimer = kDropPeriod;
    }
    const float half = static_cast<float>( kDropBox ) * 0.5f;
    for ( Drop &drop : drops ) {
        if ( drop.bAlive ) {
            // FUN_00aa4c90(fall, 40), edi = C.
            drop.Local.y -= fall;
            const float mx = BoxModulo( center.x + drop.Local.x, kDropBox );
            const float mz = BoxModulo( center.z + drop.Local.z, kDropBox );
            const int32_t cell[3] = { static_cast<int32_t>( ( center.x + drop.Local.x ) / kDropBox ),
                                      static_cast<int32_t>( ( drop.Local.y + center.y ) / kDropBox ),
                                      static_cast<int32_t>( ( center.z + drop.Local.z ) / kDropBox ) };
            const Vector3f relative( half - mx, drop.Local.y - fall, half - mz );
            if ( cell[0] == drop.Cell[0] && cell[1] == drop.Cell[1] && cell[2] == drop.Cell[2] ) {
                drop.Top = center + drop.Previous;
                drop.Bottom = center + relative;
                drop.bPlaced = true;
            }
            drop.Previous = relative;
            drop.Cell[0] = cell[0];
            drop.Cell[1] = cell[1];
            drop.Cell[2] = cell[2];
            if ( drop.Local.y < -kill ) {
                drop.bAlive = false;
                dropAlive--;
            }
        }
        if ( !drop.bAlive && spawn != 0 ) {
            drop = Drop();
            drop.Local.x = ( half - -half ) * random( dropSeed ) + -half;
            drop.Local.y = ( half - 0.0f ) * random( dropSeed ) + 0.0f;
            drop.Local.z = ( half - -half ) * random( dropSeed ) + -half;
            // +0x10..+0x3c cleared: the streak is placed by FUN_00aa4c90 once
            // two frames in the same box cell are seen (first frame: the
            // cells differ, the drop is not drawn).
            drop.Cell[0] = drop.Cell[1] = drop.Cell[2] = INT32_MIN;
            drop.bAlive = true;
            dropAlive++;
            spawn--;
        }
    }
}

void EdenRain::updateSplashes( float deltaTime, const Frame &frame, const EdenWorld &world ) {
    // FUN_00aa3b10: one zone per frame placed in the car frame, on the ground
    // (FUN_00bd6c30).
    splashRight = frame.Right;
    splashUp = frame.Up;
    const Vector3f flatForward = Vector3f( frame.Forward.x, 0.0f, frame.Forward.z ).normalized();
    const Vector3f flatRight = Vector3f( frame.Right.x, 0.0f, frame.Right.z ).normalized();
    for ( uint32_t n = 0; n < 15; n++ ) {
        // All zones are refreshed until each has been placed once, then one
        // per frame (round robin +0x4d30) [CODE].
        if ( n > 0 && zones[zoneIndex].bValid ) {
            break;
        }
        Zone &zone = zones[zoneIndex];
        // Zone local z < 0 = ahead (car z row = backward [INF]).
        zone.Position = frame.Position + flatRight * zone.Local.x - flatForward * zone.Local.z;
        float height = zone.Position.y;
        if ( world.getSurfaceHeight( zone.Position.x, zone.Position.z, frame.Position.y + 1.0f, height ) &&
             height > -99999.99f ) {
            zone.Position.y = height;
        }
        zone.bValid = true;
        zoneIndex = ( zoneIndex + 1 ) % 15;
    }
    // FUN_00aa3d80
    int32_t spawn = 0;
    splashTimer -= deltaTime;
    if ( splashTimer <= 0.0f && splashAlive <= splashMax ) {
        spawn = kSplashSpawn;
        splashTimer = kSplashPeriod;
    }
    const float step = kSplashLife / static_cast<float>( kSplashFrames );
    for ( Splash &splash : splashes ) {
        if ( splash.bAlive ) {
            splash.Life -= deltaTime;
            if ( splash.Life < 0.0f ) {
                splash.Life = 0.0f;
                splash.bAlive = false;
                splashAlive--;
            }
            splash.Frame = static_cast<int32_t>( ( kSplashLife - splash.Life ) / step );
        }
        if ( !splash.bAlive && spawn != 0 ) {
            // FUN_0095b3e0: zone index in [0, 14] (LCG, uniform [INF]).
            const uint32_t index = Min<uint32_t>( static_cast<uint32_t>( random( splashSeed ) * 15.0f ), 14 );
            const float dx = random( splashSeed ) * kZoneSize - kZoneHalf;
            const float dz = random( splashSeed ) * kZoneSize - kZoneHalf;
            splash.Life = ( kSplashLife - kSplashLife * 0.8f ) * random( splashSeed ) + kSplashLife * 0.8f;
            splash.Size = ( kSplashSize - kSplashSize * 0.5f ) * random( splashSeed ) + kSplashSize * 0.5f;
            // FUN_00aa3a80: zone position + offset on the zone plane, then
            // y += size. The ground under the splash is sampled [INF: the
            // zone plane of the original is a single height].
            splash.Position = zones[index].Position + Vector3f( dx, 0.0f, dz );
            float height = splash.Position.y;
            if ( world.getSurfaceHeight( splash.Position.x, splash.Position.z, frame.Position.y + 1.0f, height ) ) {
                splash.Position.y = height;
            }
            splash.Position.y += splash.Size;
            splash.Frame = 0;
            splash.bAlive = true;
            splashAlive++;
            spawn--;
        }
    }
}

void EdenRain::update( float deltaTime, const Frame &frame, const EdenWorld &world, const WeatherPreset *pWeather ) {
    if ( !bActive ) {
        stopSound();
        return;
    }
    setParameters( pWeather );
    updateBlocks( deltaTime, frame, world );
    if ( bDropsOn ) {
        updateDrops( deltaTime, frame, world );
    }
    if ( bSplashOn ) {
        updateSplashes( deltaTime, frame, world );
    }
    updateSound();
}

void EdenRain::updateSound() {
    if ( gpAudio == nullptr || rainClip == nullptr ) {
        return;
    }
    // Volume law not readable (ambience manager update [unknown]): Volume *
    // Curve(rain_alpha / 255) [INF], silent without rain.
    float x = Clamp( rainAlpha / 255.0f, 0.0f, 1.0f );
    float gain = 1.0f;
    if ( !soundCurve.empty() ) {
        gain = soundCurve.front().second;
        for ( size_t i = 1; i < soundCurve.size(); i++ ) {
            const auto &a = soundCurve[i - 1];
            const auto &b = soundCurve[i];
            if ( x >= a.first && x <= b.first && b.first > a.first ) {
                gain = a.second + ( b.second - a.second ) * ( x - a.first ) / ( b.first - a.first );
                break;
            }
            if ( x > b.first ) {
                gain = b.second;
            }
        }
    }
    const float volume = bRainOn ? soundVolume * gain : 0.0f;
    if ( volume <= 0.0f ) {
        stopSound();
        return;
    }
    if ( voice == GSAudio::kInvalidVoice || !gpAudio->isPlaying( voice ) ) {
        GSAudio::VoiceParams params;
        params.Client = 13; // AmbienceRain (bfx client table 0x00f90ac0)
        params.Volume = volume;
        params.Pitch = soundPitch;
        params.bLoop = true;
        voice = gpAudio->play( rainClip, params );
    } else {
        gpAudio->setVolume( voice, volume );
    }
}

void EdenRain::setParameters( const WeatherPreset *pWeather ) {
    // FUN_00aa6ce0 -> FUN_00aa61a0 (rain, drops) and the splash setter.
    const float alpha = pWeather != nullptr ? pWeather->getFloat( "rain_alpha", 0.0f ) : 0.0f;
    rainAlpha = static_cast<float>( static_cast<int32_t>( alpha ) ); // +0x59b8 = (int)alpha
    rainScroll = pWeather != nullptr ? pWeather->getFloat( "rain_scroll_speed", 0.0f ) : 0.0f; // +0x59a8
    rainTiling = pWeather != nullptr ? pWeather->getFloat( "rain_tiling", 0.0f ) : 0.0f;       // +0x59b4
    bRainOn = alpha > 0.001f;                                                                // DAT_00ecb0c4
    dropAlpha = static_cast<int32_t>( alpha );                                               // [+0xdb8]+0x7b6c
    bDropsOn = static_cast<double>( alpha ) > 9.999999747378752e-05;                          // DAT_00f49b18
    const float splashMaxValue = pWeather != nullptr ? pWeather->getFloat( "rain_splash_max", 0.0f ) : 0.0f;
    splashMax = static_cast<int32_t>( splashMaxValue );                                      // +0x4d4c
    splashAlpha = static_cast<int32_t>( pWeather != nullptr ? pWeather->getFloat( "rain_splash_alpha", 0.0f )
                                                            : 0.0f );                         // +0x4d54
    bSplashOn = splashMaxValue > 0.0f;                                                       // +0x201
}

void EdenRain::draw( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    if ( !bActive || !bMaterials ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    uint32_t streamIndex = 0;
    // WFX_Rain: FUN_00dbe930 vertices, per quad (A + top offset, uv (u, s +
    // v)), (A.xz at B.y, (u, s - T + v)), (B.xz at A.y + offset, (u + T, s +
    // v)), (B, (u + T, s - T + v)); colour (200, 200, 200, alpha). The top
    // offset (+0x8b0 / +0x8b8, slant) is 0 [kCarSpeedFactor].
    if ( bRainOn ) {
        const uint32_t color = MakeColor( static_cast<int32_t>( rainAlpha ) );
        for ( uint32_t b = 0; b < 9; b++ ) {
            const RainBlock &block = blocks[b];
            TransientAllocation allocation = device.allocateTransient( 16 * sizeof( Vertex ), 16 );
            if ( allocation.pData == nullptr ) {
                return;
            }
            uint8_t *pOut = static_cast<uint8_t *>( allocation.pData );
            const float s = block.Scroll;
            const float t = rainTiling;
            for ( uint32_t q = 0; q < 4; q++ ) {
                const Vector3f &a = block.A[q];
                const Vector3f &c = block.B[q];
                FillVertex( pOut + ( q * 4 + 0 ) * 24, Vector3f( a.x, a.y, a.z ), color, block.U[q], s + block.V[q] );
                FillVertex( pOut + ( q * 4 + 1 ) * 24, Vector3f( a.x, c.y, a.z ), color, block.U[q],
                            s - t + block.V[q] );
                FillVertex( pOut + ( q * 4 + 2 ) * 24, Vector3f( c.x, a.y, c.z ), color, block.U[q] + t,
                            s + block.V[q] );
                FillVertex( pOut + ( q * 4 + 3 ) * 24, Vector3f( c.x, c.y, c.z ), color, block.U[q] + t,
                            s - t + block.V[q] );
            }
            SetupMesh( blockMeshes[b], streams[streamIndex++], allocation, 16 * sizeof( Vertex ), pBlockIndices, 24 );
            renderer.draw( blockMeshes[b], rainMaterial,
                           Matrix4x4f::Translation( Vector3f( block.Center.x, block.Base, block.Center.z ) ) );
        }
    }
    // WFX_Drop: 4 vertices per drop (FUN_00aa4eb0): top + slant (0, 0), top
    // - (0, 1.5, 0) (1, 0), top + slant + 0.06 right (0, 1), top + 0.06
    // right - (0, 1.5, 0) (1, 1). Slant = car velocity * 1.5 *
    // DAT_0117ac84 = 0.
    if ( bDropsOn && dropAlive > 0 ) {
        uint32_t count = 0;
        for ( const Drop &drop : drops ) {
            count += drop.bAlive && drop.bPlaced ? 1 : 0;
        }
        TransientAllocation allocation = count != 0 ? device.allocateTransient( count * 4 * sizeof( Vertex ), 16 )
                                                    : TransientAllocation();
        if ( allocation.pData != nullptr ) {
            uint8_t *pOut = static_cast<uint8_t *>( allocation.pData );
            const uint32_t color = MakeColor( dropAlpha );
            const Vector3f width = dropRight * kDropWidth;
            const Vector3f length( 0.0f, kDropLength, 0.0f );
            uint32_t n = 0;
            for ( const Drop &drop : drops ) {
                if ( !drop.bAlive || !drop.bPlaced ) {
                    continue;
                }
                const Vector3f top = drop.Top - cameraPosition;
                FillVertex( pOut + ( n * 4 + 0 ) * 24, top, color, 0.0f, 0.0f );
                FillVertex( pOut + ( n * 4 + 1 ) * 24, top - length, color, 1.0f, 0.0f );
                FillVertex( pOut + ( n * 4 + 2 ) * 24, top + width, color, 0.0f, 1.0f );
                FillVertex( pOut + ( n * 4 + 3 ) * 24, top + width - length, color, 1.0f, 1.0f );
                n++;
            }
            SetupMesh( dropMesh, streams[streamIndex++], allocation, count * 4 * sizeof( Vertex ), pQuadIndices,
                       count * 6 );
            renderer.draw( dropMesh, dropMaterial, Matrix4x4f::Translation( cameraPosition ) );
        }
    }
    // WFX_Splash: billboard of the car right / up vectors, half size = size
    // * 0.5, cell (frame % 3, frame / 3) of the 3 x 3 atlas (1/3).
    if ( bSplashOn && splashAlive > 0 ) {
        uint32_t count = 0;
        for ( const Splash &splash : splashes ) {
            count += splash.bAlive ? 1 : 0;
        }
        TransientAllocation allocation = count != 0 ? device.allocateTransient( count * 4 * sizeof( Vertex ), 16 )
                                                    : TransientAllocation();
        if ( allocation.pData != nullptr ) {
            uint8_t *pOut = static_cast<uint8_t *>( allocation.pData );
            const uint32_t color = MakeColor( splashAlpha );
            uint32_t n = 0;
            for ( const Splash &splash : splashes ) {
                if ( !splash.bAlive ) {
                    continue;
                }
                const float half = splash.Size * 0.5f;
                const Vector3f r = splashRight * half;
                const Vector3f u = splashUp * half;
                const Vector3f c = splash.Position - cameraPosition;
                const float column = static_cast<float>( splash.Frame % 3 );
                const float row = static_cast<float>( splash.Frame / 3 );
                FillVertex( pOut + ( n * 4 + 0 ) * 24, c - r + u, color, column * kSplashCell, row * kSplashCell );
                FillVertex( pOut + ( n * 4 + 1 ) * 24, c - r - u, color, column * kSplashCell,
                            ( row + 1.0f ) * kSplashCell );
                FillVertex( pOut + ( n * 4 + 2 ) * 24, c + r + u, color, ( column + 1.0f ) * kSplashCell,
                            row * kSplashCell );
                FillVertex( pOut + ( n * 4 + 3 ) * 24, c + r - u, color, ( column + 1.0f ) * kSplashCell,
                            ( row + 1.0f ) * kSplashCell );
                n++;
            }
            SetupMesh( splashMesh, streams[streamIndex++], allocation, count * 4 * sizeof( Vertex ), pQuadIndices,
                       count * 6 );
            renderer.draw( splashMesh, splashMaterial, Matrix4x4f::Translation( cameraPosition ) );
        }
    }
}
