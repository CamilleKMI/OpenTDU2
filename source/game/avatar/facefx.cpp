#include "shared.h"
#include "facefx.h"
#include "filesystem/gs_file.h"

#include <cfloat>

// docs/re/facefx_spec.md. Function addresses are those of TestDrive2.exe.

const char *const kFaceFxCustomNodes[49] = {
    // FUN_00b770b0 (FxName members +0x1ee4 ... +0x1fa4, in this order).
    "CustPos Jaw thin",      "CustPos Nose pushed",    "C_Nose_Tall_or_Small",  "C_Brow_push",
    "C_Cheek_size",          "C_Chin_orient",          "C_Chin_size",           "C_Ear_orientation",
    "C_Ear_size",            "C_Eyes_height",          "C_Eyes_opening",        "C_Eyes_right_and_left",
    "C_Eyes_type",           "C_Eyes_up_and_down",     "C_Eyes_width",          "C_Fat_or_Thin",
    "C_Jaw_size",            "C_Jaw_in_and_out",       "C_Assymetry",           "C_Lips_down_in_and_out",
    "C_Lips_size",           "C_Lips_up_in_and_out",   "C_LowerFace",           "C_Mouth_position",
    "C_Mouth_size",          "C_Nose_shape",           "C_Nose_size",           "C_Nose_thick_and_thin",
    "C_Nose_up_and_down",    "C_Throat",               "C_Chin_up_and_down",    "C_Cheek_height",
    "C_Chin_Slim_or_Wide",   "C_Eyebrow_inside",       "C_Eyebrow_outside",     "C_Eyebrow_pushed",
    "C_Head_depth",          "C_Head_height",          "C_Head_width",          "C_EyeBrow_rotate",
    "C_Eyebrow_size",        "CustPos Asian",          "CustPos Afric",         "CustPos Older",
    "C_Lower_lip_thickness", "C_Upper_lip_thickness",  "C_UpperLipShrink",      "C_LowerLipShrink",
    "C_LowerEyeLid_Up",
};

namespace {
// The 6 normal map nodes (PTR_s_Anim_Nmap_Nose_00f7da3c, FUN_00b76180).
const char *const kNmapNodes[6] = { "Anim_Nmap_Nose", "Anim_Nmap_Mouth_R", "Anim_Nmap_Mouth_L",
                                    "Anim_Nmap_Brow", "Anim_Nmap_Eye_R",   "Anim_Nmap_Eye_L" };

constexpr float kEpsilon = 1.1920928955078125e-07f; // DAT_00f49b08 (and -: DAT_00f49b60)

// FxArchive (FUN_006f9630 header, FUN_006f9490 directory): little endian,
// "FACE" u32 version (1720) u32 0, FxString licensee, FxString project,
// u32 licensee version, u16 1, class table {u32 buckets {u32 bucket, u32 n
// {FxString class, u16 version}}}, name table {u32 n, FxString[n]}, u32.
// FxString = u32 length + characters. FxName = u32 name table index.
class FxReader {
public:
    FxReader( const uint8_t *pData_, size_t size_ ) : pData( pData_ ), size( size_ ) {}
    bool ok() const { return !bError; }
    size_t tell() const { return offset; }
    uint16_t u16() {
        uint16_t v = 0;
        read( &v, 2 );
        return v;
    }
    uint32_t u32() {
        uint32_t v = 0;
        read( &v, 4 );
        return v;
    }
    float f32() {
        float v = 0.0f;
        read( &v, 4 );
        return v;
    }
    std::string string() {
        const uint32_t length = u32();
        if ( bError || length > size - offset ) {
            bError = true;
            return std::string();
        }
        std::string s( reinterpret_cast<const char *>( pData + offset ), length );
        offset += length;
        while ( !s.empty() && s.back() == '\0' ) {
            s.pop_back();
        }
        return s;
    }
    const std::string &name() {
        static const std::string kEmpty;
        const uint32_t index = u32();
        if ( index >= names.size() ) {
            bError = true;
            return kEmpty;
        }
        return names[index];
    }
    bool header() {
        if ( size < 8 || memcmp( pData, "FACE", 4 ) != 0 ) {
            return false;
        }
        offset = 4;
        version = u32();
        if ( version != 1720 || u32() != 0 ) {
            return false;
        }
        string();
        string();
        u32();
        if ( u16() != 0 ) {
            const uint32_t buckets = u32();
            for ( uint32_t b = 0; b < buckets && ok(); b++ ) {
                u32();
                const uint32_t count = u32();
                for ( uint32_t c = 0; c < count && ok(); c++ ) {
                    const std::string className = string();
                    classVersions[className] = u16();
                }
            }
        }
        const uint32_t numNames = u32();
        if ( numNames > size ) {
            return false;
        }
        names.reserve( numNames );
        for ( uint32_t i = 0; i < numNames && ok(); i++ ) {
            names.push_back( string() );
        }
        u32();
        return ok();
    }
    uint16_t classVersion( const char *pClass ) const {
        auto it = classVersions.find( pClass );
        return it != classVersions.end() ? it->second : 0;
    }

private:
    const uint8_t *pData;
    size_t size;
    size_t offset = 0;
    bool bError = false;
    uint32_t version = 0;
    std::vector<std::string> names;
    std::unordered_map<std::string, uint16_t> classVersions;

    void read( void *pOut, size_t bytes ) {
        if ( bError || offset + bytes > size ) {
            bError = true;
            return;
        }
        memcpy( pOut, pData + offset, bytes );
        offset += bytes;
    }
};

// FxBone::Serialize (FUN_006feed0): FxName, pos vec3, rot quat (w x y z),
// scale vec3.
void ReadBone( FxReader &reader, std::string &outName, FaceFxActor::Transform &out ) {
    outName = reader.name();
    for ( float &v : out.Position ) {
        v = reader.f32();
    }
    for ( float &v : out.Rotation ) {
        v = reader.f32();
    }
    for ( float &v : out.Scale ) {
        v = reader.f32();
    }
}

// Hamilton product, (w, x, y, z) (inlined in FUN_00b77b20).
void QuatMul( const float a[4], const float b[4], float out[4] ) {
    const float w = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    const float x = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    const float y = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    const float z = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
    out[0] = w;
    out[1] = x;
    out[2] = y;
    out[3] = z;
}

// FUN_006e9b80: link function of a face graph input.
float LinkFunction( uint32_t function, float x, const std::vector<float> &p ) {
    switch ( function ) {
    case 0: // FxNullLinkFn
        return 0.0f;
    case 1: // FxLinearLinkFn: m x + b
        if ( p.size() == 2 ) {
            return p[1] + x * p[0];
        }
        return p.size() == 1 ? x * p[0] : x;
    case 2: { // FxQuadraticLinkFn (sign preserving)
        const float sign = x >= 0.0f ? 1.0f : -1.0f;
        return p.size() == 1 ? p[0] * x * x * sign : x * x * sign;
    }
    case 3: // FxCubicLinkFn
        return p.size() == 1 ? p[0] * x * x * x : x * x * x;
    case 4: { // FxSqrtLinkFn: sign * sqrt(|x|) (FUN_0040eb80 / FUN_00481570 [ENC], INF: fabs / sqrt)
        if ( x == 0.0f ) {
            return 0.0f;
        }
        const float sign = x >= 0.0f ? 1.0f : -1.0f;
        const float root = std::sqrt( std::fabs( x ) ) * sign;
        return p.size() == 1 ? p[0] * root : root;
    }
    case 5: // FxNegateLinkFn
        return -x;
    case 6: // FxInverseLinkFn (FUN_00481530 = equality with epsilon [ENC], INF)
        return std::fabs( x ) < kEpsilon ? 0.0f : 1.0f / x;
    case 7: // FxOneClampLinkFn
        return x > 1.0f ? 1.0f / x : 1.0f;
    case 8: // FxConstantLinkFn
        return p.empty() ? 1.0f : p[0];
    case 9: // FxCorrectiveLinkFn (handled by the node loop)
        return p.empty() ? 0.0f : p[0];
    case 10: // FxClampedLinearLinkFn
        if ( p.size() == 4 ) {
            if ( ( p[3] > 0.0f && p[1] < x ) || ( p[3] <= 0.0f && x < p[1] ) ) {
                return static_cast<float>( static_cast<double>( p[2] ) +
                                           static_cast<double>( p[1] ) * p[0] * -1.0 +
                                           static_cast<double>( x ) * p[0] );
            }
            return p[2];
        }
        if ( p.size() == 1 ) {
            return x <= 0.0f ? 0.0f : x * p[0];
        }
        return x;
    default:
        return 0.0f;
    }
}
} // namespace

// FxActor::Serialize (FUN_006f1950, version 3): FxNamedObject name,
// FxMasterBoneList (FUN_006f38d0 v2: u32 n {FxBone reference, quat inverse
// reference rotation (+0x30), u32 client index (+0x40, rewritten at load to
// the skeleton bone name hash), f32 weight (+0x44), u32 n {u32 node, FxBone
// pose}}), FxCompiledFaceGraph (FUN_006e9860: u32 n nodes (FUN_006e87a0),
// node name -> index hash map), FxAnimGroup "Default" (+0x6058), anim
// groups (+0x606c), FxPhonemeMap (+0x6088).
bool FaceFxActor::parse( const uint8_t *pData, size_t size ) {
    bones.clear();
    nodes.clear();
    nodeByName.clear();
    FxReader reader( pData, size );
    if ( !reader.header() || reader.classVersion( "FxActor" ) != 3 || reader.classVersion( "FxMasterBoneList" ) != 2 ) {
        return false;
    }
    name = reader.name();
    const uint32_t numBones = reader.u32();
    for ( uint32_t i = 0; i < numBones && reader.ok() && i < 0x400; i++ ) {
        Bone bone;
        ReadBone( reader, bone.Name, bone.Reference );
        for ( float &v : bone.InverseRotation ) {
            v = reader.f32();
        }
        reader.u32(); // client index
        reader.f32(); // weight
        const uint32_t numPoses = reader.u32();
        for ( uint32_t k = 0; k < numPoses && reader.ok() && k < 0x10000; k++ ) {
            BonePose pose;
            pose.Node = reader.u32();
            std::string poseName;
            ReadBone( reader, poseName, pose.Pose );
            bone.Poses.push_back( pose );
        }
        bone.NameHash4 = EdenNameHash4( bone.Name.c_str() );
        bones.push_back( std::move( bone ) );
    }
    const uint32_t numNodes = reader.u32();
    for ( uint32_t i = 0; i < numNodes && reader.ok() && i < 0x10000; i++ ) {
        Node node;
        node.Type = reader.u32();
        node.Name = reader.name();
        node.Min = reader.f32();
        node.InverseMin = reader.f32();
        node.Max = reader.f32();
        node.InverseMax = reader.f32();
        node.InputOperation = reader.u32();
        const uint32_t numInputs = reader.u32();
        for ( uint32_t k = 0; k < numInputs && reader.ok() && k < 0x10000; k++ ) {
            Link link;
            link.Input = reader.u32();
            link.Function = reader.u32();
            const uint32_t numParameters = reader.u32();
            for ( uint32_t p = 0; p < numParameters && reader.ok() && p < 64; p++ ) {
                link.Parameters.push_back( reader.f32() );
            }
            node.Inputs.push_back( std::move( link ) );
        }
        // FxFaceGraphNodeUserProperty[] (FUN_006ec130): name, u32, u32, f32,
        // FxString[] ("Morph Target Name").
        const uint32_t numProperties = reader.u32();
        for ( uint32_t k = 0; k < numProperties && reader.ok() && k < 0x1000; k++ ) {
            reader.name();
            reader.u32();
            reader.u32();
            reader.f32();
            const uint32_t numStrings = reader.u32();
            for ( uint32_t s = 0; s < numStrings && reader.ok() && s < 0x1000; s++ ) {
                reader.string();
            }
        }
        nodes.push_back( std::move( node ) );
    }
    // FUN_006eb3d0: hash map {u32 buckets {u32 bucket, u32 n {FxString, u32 index}}}.
    const uint32_t buckets = reader.u32();
    for ( uint32_t b = 0; b < buckets && reader.ok() && b < 0x10000; b++ ) {
        reader.u32();
        const uint32_t count = reader.u32();
        for ( uint32_t c = 0; c < count && reader.ok() && c < 0x10000; c++ ) {
            const std::string nodeName = reader.string();
            const uint32_t index = reader.u32();
            if ( index < nodes.size() ) {
                nodeByName[nodeName] = index;
            }
        }
    }
    if ( !reader.ok() || bones.empty() || nodes.empty() ) {
        bones.clear();
        nodes.clear();
        return false;
    }
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        for ( const Link &link : nodes[i].Inputs ) {
            if ( link.Input >= i ) { // the compiled graph is topologically sorted
                nodes.clear();
                bones.clear();
                return false;
            }
        }
    }
    for ( const Bone &bone : bones ) {
        for ( const BonePose &pose : bone.Poses ) {
            if ( pose.Node >= nodes.size() ) {
                nodes.clear();
                bones.clear();
                return false;
            }
        }
    }
    return true;
}

int32_t FaceFxActor::findNode( const std::string &nodeName ) const {
    auto it = nodeByName.find( nodeName );
    return it != nodeByName.end() ? static_cast<int32_t>( it->second ) : -1;
}

// FxAnimCurve::EvaluateAt (FUN_007033c0), key search FUN_00703a00,
// hermite FUN_007038e0 (x87, double constants 3 / -3 / 2), linear
// FUN_007039a0.
float FaceFxCurve::evaluate( float time, uint32_t *pCache ) const {
    if ( Keys.empty() ) {
        return 0.0f;
    }
    if ( !( time > Keys.front().Time ) ) {
        return Keys.front().Value;
    }
    if ( !( time < Keys.back().Time ) ) {
        return Keys.back().Value;
    }
    if ( Keys.size() == 1 ) {
        return Keys.front().Value;
    }
    const uint32_t last = static_cast<uint32_t>( Keys.size() ) - 1;
    uint32_t k = 0;
    if ( pCache != nullptr && *pCache < Keys.size() && Keys[*pCache].Time <= time ) {
        k = *pCache;
    }
    for ( ; k < last; k++ ) {
        if ( Keys[k].Time <= time && time < Keys[k + 1].Time ) {
            break;
        }
    }
    if ( pCache != nullptr ) {
        *pCache = k;
    }
    if ( k == last ) {
        k--;
    }
    const Key &k0 = Keys[k];
    const Key &k1 = Keys[k + 1];
    const float dt = k1.Time - k0.Time;
    const float u = ( time - k0.Time ) / dt;
    if ( Interpolation == 1 ) {
        return ( k1.Value - k0.Value ) * u + k0.Value;
    }
    const double v0 = k0.Value;
    const double v1 = k1.Value;
    const float slopeOut = k0.SlopeOut * dt;
    const float slopeIn = k1.SlopeIn * dt;
    const double cubic = v0 * 2.0 - v1 * 2.0 + slopeOut + slopeIn;
    const double square = v1 * 3.0 + v0 * -3.0 - slopeOut * 2.0 - slopeIn;
    return static_cast<float>( ( ( cubic * u + square ) * u + slopeOut ) * u + v0 );
}

// FxAnimSet::Serialize (FUN_006f5430): FxNamedObject name, FxName actor,
// FxAnimGroup (FUN_006eefa0: name, FxAnim[]). FxAnim v6 (FUN_006ee680):
// name, curves {FxName, u32 interpolation} (FxAnimCurve v1: keys in the
// anim), u32 total keys, {time, value, slope in, slope out}[total], u32 n
// key counts per curve, f32 blend in, f32 blend out, FxAnimBoneWeight[]
// {FxName, f32}, FxString, FxString, u32.
bool FaceFxAnimSet::parse( const uint8_t *pData, size_t size ) {
    anims.clear();
    FxReader reader( pData, size );
    if ( !reader.header() || reader.classVersion( "FxAnim" ) != 6 || reader.classVersion( "FxAnimCurve" ) != 1 ) {
        return false;
    }
    reader.name();
    actorName = reader.name();
    groupName = reader.name();
    const uint32_t numAnims = reader.u32();
    for ( uint32_t a = 0; a < numAnims && reader.ok() && a < 0x10000; a++ ) {
        FaceFxAnim anim;
        anim.Name = reader.name();
        const uint32_t numCurves = reader.u32();
        for ( uint32_t c = 0; c < numCurves && reader.ok() && c < 0x10000; c++ ) {
            FaceFxCurve curve;
            curve.Name = reader.name();
            curve.Interpolation = reader.u32();
            anim.Curves.push_back( std::move( curve ) );
        }
        const uint32_t numKeys = reader.u32();
        if ( numKeys != 0 ) {
            std::vector<FaceFxCurve::Key> keys( numKeys > size / 16 ? 0 : numKeys );
            if ( keys.size() != numKeys ) {
                return false;
            }
            for ( FaceFxCurve::Key &key : keys ) {
                key.Time = reader.f32();
                key.Value = reader.f32();
                key.SlopeIn = reader.f32();
                key.SlopeOut = reader.f32();
            }
            const uint32_t numCounts = reader.u32();
            size_t first = 0;
            for ( uint32_t c = 0; c < numCounts && reader.ok(); c++ ) {
                const uint32_t count = reader.u32();
                if ( c < anim.Curves.size() && first + count <= keys.size() ) {
                    anim.Curves[c].Keys.assign( keys.begin() + first, keys.begin() + first + count );
                }
                first += count;
            }
        }
        anim.BlendInTime = reader.f32();
        anim.BlendOutTime = reader.f32();
        const uint32_t numWeights = reader.u32();
        for ( uint32_t w = 0; w < numWeights && reader.ok() && w < 0x10000; w++ ) {
            reader.name();
            reader.f32();
        }
        reader.string();
        reader.string();
        reader.u32();
        // FUN_006eeb90 / FUN_00703550: bounds of the keys.
        float start = FLT_MAX, end = FLT_MIN;
        for ( const FaceFxCurve &curve : anim.Curves ) {
            float curveStart = FLT_MAX, curveEnd = FLT_MIN;
            if ( !curve.Keys.empty() ) {
                curveStart = curve.Keys.front().Time;
                curveEnd = curve.Keys.back().Time;
            }
            start = Min( start, curveStart );
            end = Max( end, curveEnd );
        }
        if ( start == FLT_MAX && end == FLT_MIN ) {
            start = 0.0f;
            end = 1.0f;
        }
        anim.StartTime = start;
        anim.EndTime = end;
        anims.push_back( std::move( anim ) );
    }
    return reader.ok() && reader.tell() == size;
}

const FaceFxAnim *FaceFxAnimSet::findAnim( const std::string &animName ) const {
    for ( const FaceFxAnim &anim : anims ) {
        if ( anim.Name == animName ) {
            return &anim;
        }
    }
    return nullptr;
}

bool FaceFxLibrary::load() {
    unload();
    const std::string path = gpFile->getBankFolder() + "avatar/avatar_face_fx.bnk";
    if ( !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "FaceFX: %s not found\n", path.c_str() );
        return false;
    }
    const BankEntry *pFemale = bank.findEntry( "female", "fxa" );
    const BankEntry *pMale = bank.findEntry( "male", "fxa" );
    const bool bFemale = pFemale != nullptr && female.parse( pFemale->pData, pFemale->Size );
    const bool bMale = pMale != nullptr && male.parse( pMale->pData, pMale->Size );
    bLoaded = bFemale || bMale;
    OTDU_LOG_INFO( "FaceFX: actors %s (%zu bones, %zu nodes) / %s (%zu bones, %zu nodes)\n",
                   bFemale ? female.getName().c_str() : "-", female.getBones().size(), female.getNodes().size(),
                   bMale ? male.getName().c_str() : "-", male.getBones().size(), male.getNodes().size() );
    return bLoaded;
}

void FaceFxLibrary::unload() {
    bank.unload();
    female = FaceFxActor();
    male = FaceFxActor();
    animSets.clear();
    bLoaded = false;
}

void FaceFxLibrary::addAnimSets( const Bank &source ) {
    for ( const BankEntry *pEntry : source.findEntriesByExtension( "fxe" ) ) {
        auto pSet = std::make_unique<FaceFxAnimSet>();
        if ( pSet->parse( pEntry->pData, pEntry->Size ) ) {
            OTDU_LOG_DEBUG( "FaceFX: %s group %s, %zu anims (%s)\n", pEntry->Path.c_str(), pSet->getGroupName().c_str(),
                            pSet->getAnims().size(), pSet->getActorName().c_str() );
            animSets.push_back( std::move( pSet ) );
        } else {
            OTDU_LOG_WARN( "FaceFX: %s not parsed\n", pEntry->Path.c_str() );
        }
    }
}

const FaceFxActor *FaceFxLibrary::getActor( bool bWoman ) const {
    const FaceFxActor &actor = bWoman ? female : male;
    return actor.getBones().empty() ? nullptr : &actor;
}

const FaceFxAnim *FaceFxLibrary::findAnim( const FaceFxActor &actor, const std::string &reference ) const {
    // FUN_00b77a50: anim = after the last ':', group = between the last two.
    const size_t last = reference.rfind( ':' );
    if ( last == std::string::npos || last == 0 ) {
        return nullptr;
    }
    const size_t previous = reference.rfind( ':', last - 1 );
    const std::string group = reference.substr( previous == std::string::npos ? 0 : previous + 1,
                                                last - ( previous == std::string::npos ? 0 : previous + 1 ) );
    const std::string animName = reference.substr( last + 1 );
    // FUN_00b77600: the groups mounted on the actor (FxActor +0x6058 /
    // +0x606c / +0x6078), i.e. the anim sets made for this actor.
    for ( const auto &pSet : animSets ) {
        if ( pSet->getActorName() != actor.getName() || pSet->getGroupName() != group ) {
            continue;
        }
        if ( const FaceFxAnim *pAnim = pSet->findAnim( animName ) ) {
            return pAnim;
        }
    }
    return nullptr;
}

void FaceFxLibrary::Evaluate( const FaceFxActor &actor, const FaceFxAnim *pAnim, float time, const int8_t *pFaceParams,
                              const float *pLookAt, FaceFxPose &outPose ) {
    const std::vector<FaceFxActor::Node> &nodes = actor.getNodes();
    const size_t numNodes = nodes.size();

    // Track values (compiled node +0x18): FUN_006ece00 evaluates every curve
    // whose name is a node of the graph at the anim time (audio time).
    std::vector<float> track( numNodes, 0.0f );
    if ( pAnim != nullptr ) {
        for ( const FaceFxCurve &curve : pAnim->Curves ) {
            const int32_t node = actor.findNode( curve.Name );
            if ( node >= 0 ) {
                track[node] = curve.evaluate( time );
            }
        }
    }
    // Registers (FUN_00b76550 -> FUN_00b76210 -> FUN_006ed2d0(node, 2,
    // (char)FaceParam[i] / 100.0, 0, -1, 0, 0)): operation 2 = replace,
    // no interpolation.
    std::vector<int8_t> hasRegister( numNodes, 0 );
    std::vector<float> registers( numNodes, 0.0f );
    if ( pFaceParams != nullptr ) {
        for ( uint32_t i = 0; i < 49; i++ ) {
            const int32_t node = actor.findNode( kFaceFxCustomNodes[i] );
            if ( node >= 0 ) {
                hasRegister[node] = 1;
                registers[node] = static_cast<float>( static_cast<double>( pFaceParams[i] ) / 100.0 );
            }
        }
    }

    if ( pLookAt != nullptr ) {
        // FUN_00b75f50 -> FUN_00b75e10: FUN_006ed2d0(node, 2, value clamped
        // to [node +0x08 min, node +0x10 max], 0, -1, 0, 0).
        static const char *const kLookNodes[2] = { "LookAt_T2_side", "LookAt_T2_vertical" };
        for ( uint32_t i = 0; i < 2; i++ ) {
            const int32_t node = actor.findNode( kLookNodes[i] );
            if ( node >= 0 ) {
                const FaceFxActor::Node &n = actor.getNodes()[node];
                float v = pLookAt[i];
                if ( v > n.Max ) {
                    v = n.Max;
                }
                if ( n.Min > v ) {
                    v = n.Min;
                }
                hasRegister[node] = 1;
                registers[node] = v;
            }
        }
    }

    // FUN_006e8a70: nodes in order (topologically sorted).
    static const float kInitial[4] = { 0.0f, 1.0f, -FLT_MAX, FLT_MAX }; // DAT_00e52b84
    std::vector<float> value( numNodes, 0.0f );
    for ( size_t i = 0; i < numNodes; i++ ) {
        const FaceFxActor::Node &node = nodes[i];
        float result = 0.0f;
        if ( node.Type == 1 || node.Type == 2 ) {
            // FxDeltaNode / FxCurrentTimeNode: not present in the TDU2
            // actors (stateless evaluation: delta 0, time = anim time).
            result = node.Type == 2 ? time : 0.0f;
            value[i] = result;
            continue;
        }
        const uint32_t operation = node.InputOperation & 3;
        if ( !node.Inputs.empty() ) {
            result = kInitial[operation];
        }
        float corrective = 0.0f;
        for ( const FaceFxActor::Link &link : node.Inputs ) {
            const FaceFxActor::Node &input = nodes[link.Input];
            const float x = value[link.Input];
            if ( link.Function == 9 ) {
                // FxCorrectiveLinkFn: max(x / max, -x / min) * p0, sum
                // clamped to 1; the node value is scaled by (1 - sum).
                const float a = x * input.InverseMax;
                const float b = -x * input.InverseMin;
                const float m = b > a ? b : a;
                const float c = m * ( link.Parameters.empty() ? 0.0f : link.Parameters[0] ) + corrective;
                corrective = c < 1.0f ? c : 1.0f;
                continue;
            }
            const float y = LinkFunction( link.Function, x, link.Parameters );
            switch ( operation ) {
            case 0:
                result = result + y;
                break;
            case 1:
                result = result * y;
                break;
            case 2:
                // FUN_006eacc0(&y, &acc): acc if NaN or acc > y, else y.
                result = ( std::isnan( result ) || std::isnan( y ) || result > y ) ? result : y;
                break;
            default:
                result = result < y ? result : y; // FUN_006eacf0 (min)
                break;
            }
        }
        result = result + track[i];
        if ( hasRegister[i] ) {
            result = registers[i]; // register operation 2 (replace)
        }
        result = ( 1.0f - corrective ) * result;
        // FUN_006ea320: max(min(v, max), min).
        const float clamped = result < node.Max ? result : node.Max;
        value[i] = node.Min > clamped ? node.Min : clamped;
    }

    // FUN_00b77b20: blended bone of every master bone.
    outPose.Bones.clear();
    outPose.Bones.reserve( actor.getBones().size() );
    for ( const FaceFxActor::Bone &bone : actor.getBones() ) {
        float position[3] = { bone.Reference.Position[0], bone.Reference.Position[1], bone.Reference.Position[2] };
        float scale[3] = { bone.Reference.Scale[0], bone.Reference.Scale[1], bone.Reference.Scale[2] };
        float reference[4] = { bone.Reference.Rotation[0], bone.Reference.Rotation[1], bone.Reference.Rotation[2],
                               bone.Reference.Rotation[3] };
        float accumulated[4] = { reference[0], reference[1], reference[2], reference[3] };
        for ( const FaceFxActor::BonePose &pose : bone.Poses ) {
            const float weight = value[pose.Node];
            const float delta = weight - 0.0f;
            if ( !( kEpsilon <= delta || delta <= -kEpsilon ) ) {
                continue;
            }
            for ( int c = 0; c < 3; c++ ) {
                position[c] = pose.Pose.Position[c] * weight + position[c];
            }
            const float *q = pose.Pose.Rotation;
            // Shortest arc: the reference copy is negated (and stays so).
            float sumSq = 0.0f, diffSq = 0.0f;
            for ( int c = 0; c < 4; c++ ) {
                sumSq += ( reference[c] + q[c] ) * ( reference[c] + q[c] );
                diffSq += ( reference[c] - q[c] ) * ( reference[c] - q[c] );
            }
            if ( sumSq < diffSq ) {
                for ( float &c : reference ) {
                    c = -c;
                }
            }
            const float dot = reference[0] * q[0] + reference[3] * q[3] + reference[1] * q[1] + q[2] * reference[2];
            float slerp[4];
            if ( dot <= 0.55f ) { // DAT_00f90738
                // acos / sin: FUN_00429330 / FUN_00541260 [ENC] (INF: CRT acosf / sinf).
                const float theta = std::acos( dot );
                const float inverseSin = static_cast<float>( 1.0 / std::sin( theta ) );
                const float a = static_cast<float>( std::sin( static_cast<float>( ( 1.0 - weight ) * theta ) ) ) * inverseSin;
                const float b = static_cast<float>( std::sin( theta * weight ) ) * inverseSin;
                for ( int c = 0; c < 4; c++ ) {
                    slerp[c] = a * reference[c] + b * q[c];
                }
            } else {
                const float a = static_cast<float>( 1.0 - weight );
                for ( int c = 0; c < 4; c++ ) {
                    slerp[c] = weight * q[c] + a * reference[c];
                }
            }
            // accumulated = accumulated * (inverse reference * slerp).
            float relative[4];
            QuatMul( bone.InverseRotation, slerp, relative );
            float product[4];
            QuatMul( accumulated, relative, product );
            for ( int c = 0; c < 4; c++ ) {
                accumulated[c] = product[c];
            }
            for ( int c = 0; c < 3; c++ ) {
                scale[c] = pose.Pose.Scale[c] * weight + scale[c];
            }
        }
        const float lengthSq = accumulated[0] * accumulated[0] + accumulated[1] * accumulated[1] +
                               accumulated[2] * accumulated[2] + accumulated[3] * accumulated[3];
        const float deviation = lengthSq - 1.0f;
        if ( kEpsilon <= deviation || deviation <= -kEpsilon ) {
            const float length = std::sqrt( lengthSq ); // FUN_00448620 [ENC] (INF: sqrt)
            if ( length < kEpsilon ) {
                accumulated[0] = 1.0f;
                accumulated[1] = accumulated[2] = accumulated[3] = 0.0f;
            } else {
                const float inverse = static_cast<float>( 1.0 / length );
                for ( float &c : accumulated ) {
                    c *= inverse;
                }
            }
        }
        // Max -> Eden: rotation (-x, y, z, w), position (-x, y, z) / 100
        // (double 100.0 @0xf49a00), scale unchanged.
        BoneTransform transform;
        transform.Rotation = Quaternionf( -accumulated[1], accumulated[2], accumulated[3], accumulated[0] );
        transform.Translation = Vector3f( static_cast<float>( -position[0] / 100.0 ),
                                          static_cast<float>( position[1] / 100.0 ),
                                          static_cast<float>( position[2] / 100.0 ) );
        transform.Scale = Vector3f( scale[0], scale[1], scale[2] );
        outPose.Bones.emplace_back( bone.NameHash4, transform );
    }

    // FUN_00caf700 (TD_Skinface d5013ed0dbcddbca): facialAnimCoef1 / 2.
    float nmap[6] = {};
    for ( uint32_t i = 0; i < 6; i++ ) {
        const int32_t node = actor.findNode( kNmapNodes[i] );
        if ( node >= 0 ) {
            nmap[i] = value[node];
        }
    }
    outPose.FacialCoef1 = Vector4f( nmap[0], nmap[1], nmap[2], 0.0f );
    outPose.FacialCoef2 = Vector4f( nmap[3], nmap[4], nmap[5], 0.0f );
}
