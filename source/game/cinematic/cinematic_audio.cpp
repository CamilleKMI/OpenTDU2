#include "shared.h"
#include "cinematic_audio.h"

#include "config/gs_config.h"
#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_scene.h"

namespace {
template <typename T> T ReadAt( const uint8_t *p, size_t offset ) {
    T value;
    memcpy( &value, p + offset, sizeof( T ) );
    return value;
}

constexpr uint32_t kSndClassHash = 0x8540076c; // edFxSound node (+0x58)
constexpr size_t kSndNodeSize = 0xe0;           // tag " DNS" at +0xe0
constexpr uint16_t kRefFx = 0x010d;             // REFS FX entry (u64 Fx__Root)

uint32_t FloatBits( float value ) {
    uint32_t bits;
    memcpy( &bits, &value, sizeof( bits ) );
    return bits;
}

// gpConfig language (db suffix) -> .bfx LSGL node name prefix.
std::string LanguagePrefix( const std::string &language ) {
    static constexpr const char *kPrefixes[][2] = { { "us", "US" }, { "fr", "FR" }, { "ge", "GE" },
                                                    { "it", "IT" }, { "sp", "SP" }, { "ru", "RU" },
                                                    { "po", "PL" }, { "pl", "PL" }, { "jp", "JP" } };
    for ( const auto &prefix : kPrefixes ) {
        if ( StringCmpI( language.c_str(), prefix[0] ) == 0 ) {
            return std::string( prefix[1] ) + "_";
        }
    }
    std::string upper = language;
    for ( char &c : upper ) {
        c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
    }
    return upper + "_";
}

bool EndsWith( const std::string &str, const char *pSuffix ) {
    const size_t length = strlen( pSuffix );
    return str.size() >= length && str.compare( str.size() - length, length, pSuffix ) == 0;
}
} // namespace

bool BankHashcodes::load( const Bank &bank, const char *pFilename ) {
    pBank = &bank;
    files.clear();
    const BankEntry *pEntry = bank.findEntry( pFilename != nullptr ? pFilename : "hashcodes.xmb" );
    if ( pEntry == nullptr && pFilename == nullptr ) {
        for ( const BankEntry &entry : bank.getEntries() ) {
            if ( entry.Extension == "xmb" && ToLower( entry.Name ).rfind( "hashcodes", 0 ) == 0 ) {
                pEntry = &entry;
                break;
            }
        }
    }
    XmbDocument document;
    if ( pEntry == nullptr || !document.load( pEntry->pData, pEntry->Size ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "___CmdSaveFileInfoArray" ); i++ ) {
        XmbValue info = root.getArrayElement( "___CmdSaveFileInfoArray", i );
        const int32_t type = static_cast<int32_t>( info["Type"].asInt() );
        const int32_t subType = static_cast<int32_t>( info["SubType"].asInt() );
        const uint32_t count = Min( info.getArraySize( "FileIndices" ), info.getArraySize( "FileReferences" ) );
        for ( uint32_t j = 0; j < count; j++ ) {
            files.push_back( { { info.getArrayElement( "FileReferences", j ).asUInt64(), type, subType },
                               static_cast<uint32_t>( info.getArrayElement( "FileIndices", j ).asInt() ) } );
        }
    }
    return !files.empty();
}

const BankEntry *BankHashcodes::find( uint64_t rsc, int32_t type, int32_t subType ) const {
    if ( pBank == nullptr ) {
        return nullptr;
    }
    for ( const auto &file : files ) {
        if ( file.first.Rsc == rsc && ( type < 0 || file.first.Type == type ) &&
             ( subType < 0 || file.first.SubType == subType ) && file.second < pBank->getEntries().size() ) {
            return &pBank->getEntries()[file.second];
        }
    }
    return nullptr;
}

CinematicSounds::CinematicSounds() {}

CinematicSounds::~CinematicSounds() { unload(); }

bool CinematicSounds::load( const Bank &bank ) {
    unload();
    pBank = &bank;
    hashcodes.load( bank );

    // Language bank (voices): "<bank>__<lang>.bnk", fallback "__us".
    std::string language = gpConfig != nullptr ? ToLower( gpConfig->getLanguage() ) : std::string( "us" );
    const std::string &path = bank.getPath();
    if ( path.size() > 4 && StringCmpI( path.c_str() + path.size() - 4, ".bnk" ) == 0 && gpFile != nullptr ) {
        const std::string base = path.substr( 0, path.size() - 4 );
        for ( const std::string &candidate : { language, std::string( "us" ) } ) {
            const std::string languagePath = base + "__" + candidate + ".bnk";
            if ( !gpFile->fileExists( languagePath.c_str() ) ) {
                continue;
            }
            auto pLanguage = std::make_unique<Bank>();
            if ( pLanguage->load( languagePath.c_str() ) ) {
                pLanguageBank = std::move( pLanguage );
                languageHashcodes.load( *pLanguageBank, nullptr );
                language = candidate;
                OTDU_LOG_DEBUG( "CinematicSounds: language bank '%s'\n", languagePath.c_str() );
                break;
            }
        }
    }
    languagePrefix = LanguagePrefix( language );

    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument document;
    if ( pEntities == nullptr || !document.load( pEntities->pData, pEntities->Size ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    XmbValue fx = root["ExportType_Fx"];
    for ( uint32_t i = 0; i < fx.getArraySize( "ArrayOf_Fx__Root" ); i++ ) {
        XmbValue entry = fx.getArrayElement( "ArrayOf_Fx__Root", i );
        rootSources[entry["__Hc"].asUInt64()] = entry["SourceData"].asUInt64();
    }
    XmbValue sourceData = root["ExportType_FxSourceData"];
    for ( uint32_t i = 0; i < sourceData.getArraySize( "ArrayOf_Fx__SourceData" ); i++ ) {
        XmbValue entry = sourceData.getArrayElement( "ArrayOf_Fx__SourceData", i );
        auto &source = sources[entry["__Hc"].asUInt64()];
        if ( entry.getArraySize( "Fx" ) > 0 ) {
            source.first = entry.getArrayElement( "Fx", 0 )["rsc"].asUInt64();
        }
        for ( uint32_t j = 0; j < entry.getArraySize( "SoundList" ); j++ ) {
            source.second.push_back( entry.getArrayElement( "SoundList", j )["SoundNode"].asUInt64() );
        }
    }
    // ArrayOf_Fx__Sound { SoundFile[0] } and ArrayOf_Fx__Sound__EN / __FR /
    // ... { LocalizedSoundFile[0] }.
    XmbValue soundExport = root["ExportType_FxSound"];
    if ( const XmbType *pType = soundExport.isValid() ? soundExport.getType() : nullptr ) {
        for ( const XmbType::Field &field : pType->Fields ) {
            if ( !field.bArray || field.Name.rfind( "ArrayOf_Fx__Sound", 0 ) != 0 ) {
                continue;
            }
            const char *pArray = field.Name.c_str();
            for ( uint32_t i = 0; i < soundExport.getArraySize( pArray ); i++ ) {
                XmbValue entry = soundExport.getArrayElement( pArray, i );
                for ( const char *pFile : { "SoundFile", "LocalizedSoundFile" } ) {
                    if ( entry.getArraySize( pFile ) > 0 ) {
                        soundFiles[entry["__Hc"].asUInt64()] = entry.getArrayElement( pFile, 0 )["rsc"].asUInt64();
                        break;
                    }
                }
            }
        }
    }
    XmbValue entityExport = root["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Fx" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Fx", i );
        XmbValue position = entry["orient_pos_scale"]["pos"];
        const Vector3f pos( position["X"].asFloat(), position["Y"].asFloat(), position["Z"].asFloat() );
        emitters[entry["__Name"].asString()] = { entry["Fx"].asUInt64(), pos };
    }
    OTDU_LOG_DEBUG( "CinematicSounds: %zu roots, %zu sources, %zu sounds, %zu emitters (voices '%s'%s)\n",
                    rootSources.size(), sources.size(), soundFiles.size(), emitters.size(), languagePrefix.c_str(),
                    pLanguageBank != nullptr ? "" : ", no language bank" );
    return true;
}

void CinematicSounds::unload() {
    stopAll();
    rootSources.clear();
    sources.clear();
    soundFiles.clear();
    emitters.clear();
    resolved.clear();
    resolvedEmitters.clear();
    pLanguageBank.reset();
    languageHashcodes = BankHashcodes();
    pBank = nullptr;
}

bool CinematicSounds::ParseBfx( const uint8_t *pData, size_t size, Bfx &outBfx ) {
    outBfx = Bfx();
    if ( size < kSndNodeSize + 4 ) {
        return false;
    }
    outBfx.bLanguageSwitch = size >= 0xb8 && memcmp( pData + 0xb4, "LSGL", 4 ) == 0;
    for ( size_t tag = kSndNodeSize; tag + 4 <= size; tag += 4 ) {
        if ( memcmp( pData + tag, " DNS", 4 ) != 0 ) {
            continue;
        }
        const uint8_t *pNode = pData + tag - kSndNodeSize;
        if ( ReadAt<uint32_t>( pNode, 0x58 ) != kSndClassHash ) {
            continue;
        }
        SndNode node;
        const char *pName = reinterpret_cast<const char *>( pNode + 0x78 );
        node.Name.assign( pName, strnlen( pName, 32 ) );
        node.Rsc = ReadAt<uint64_t>( pNode, 0xa0 );
        node.LoopCount = ReadAt<uint32_t>( pNode, 0x5c );
        Sound &params = node.Params;
        params.Volume = ReadAt<float>( pNode, 0xac );
        params.Pitch = ReadAt<float>( pNode, 0xb0 );
        params.VolumeRandom = ReadAt<float>( pNode, 0xb4 );
        params.PitchRandom = ReadAt<float>( pNode, 0xb8 );
        params.ConeInner = ReadAt<float>( pNode, 0xbc );
        params.ConeOuter = ReadAt<float>( pNode, 0xc0 );
        params.ConeOuterVolume = ReadAt<float>( pNode, 0xc4 );
        params.ClipNear = ReadAt<float>( pNode, 0xc8 );
        params.ClipMid = ReadAt<float>( pNode, 0xcc );
        params.ClipFar = ReadAt<float>( pNode, 0xd0 );
        params.ClipMidVolume = ReadAt<float>( pNode, 0xd4 );
        params.ClipAttenuation = ReadAt<float>( pNode, 0xd8 );
        params.Client = static_cast<int32_t>( ReadAt<uint32_t>( pNode, 0xdc ) );
        params.bLoop = node.LoopCount == 0xffffffffu;
        if ( !std::isfinite( params.Pitch ) || params.Pitch <= 0.0f ) {
            params.Pitch = 1.0f;
        }
        outBfx.Nodes.push_back( std::move( node ) );
        tag += kSndNodeSize - 4;
    }
    return !outBfx.Nodes.empty();
}

const BankEntry *CinematicSounds::findWav( uint64_t rsc ) const {
    if ( const BankEntry *pEntry = hashcodes.find( rsc, 10, 6 ) ) {
        return pEntry;
    }
    if ( const BankEntry *pEntry = languageHashcodes.find( rsc ) ) {
        return pEntry->Extension == "wav" ? pEntry : nullptr;
    }
    const BankEntry *pEntry = hashcodes.find( rsc );
    return pEntry != nullptr && pEntry->Extension == "wav" ? pEntry : nullptr;
}

const std::vector<CinematicSounds::Sound> *CinematicSounds::findRoot( uint64_t rootHashcode ) {
    auto it = resolved.find( rootHashcode );
    if ( it != resolved.end() ) {
        return &it->second;
    }
    std::vector<Sound> &list = resolved[rootHashcode];
    auto sourceIt = rootSources.find( rootHashcode );
    if ( sourceIt == rootSources.end() || gpAudio == nullptr ) {
        return &list;
    }
    auto source = sources.find( sourceIt->second );
    if ( source == sources.end() ) {
        return &list;
    }
    Bfx bfx;
    if ( const BankEntry *pBfx = hashcodes.find( source->second.first, 0, 31 ) ) {
        ParseBfx( pBfx->pData, pBfx->Size, bfx );
    }
    // Language switch: the node of the current language whose wav exists
    // (US_ then any as fallbacks). [INF] "_W" nodes (second take of some
    // lines, e.g. IntroNew-E_Meet_Tess_11_W) only when no plain one exists.
    uint64_t languageRsc = 0;
    if ( bfx.bLanguageSwitch ) {
        const SndNode *pChosen = nullptr;
        for ( const std::string &prefix : { languagePrefix, std::string( "US_" ), std::string() } ) {
            for ( int pass = 0; pass < 2 && pChosen == nullptr; pass++ ) {
                for ( const SndNode &node : bfx.Nodes ) {
                    if ( node.Name.rfind( prefix, 0 ) == 0 && EndsWith( node.Name, "_W" ) == ( pass == 1 ) &&
                         findWav( node.Rsc ) != nullptr ) {
                        pChosen = &node;
                        break;
                    }
                }
            }
            if ( pChosen != nullptr ) {
                break;
            }
        }
        if ( pChosen == nullptr ) {
            return &list;
        }
        languageRsc = pChosen->Rsc;
    }
    for ( uint64_t soundNode : source->second.second ) {
        auto fileIt = soundFiles.find( soundNode );
        if ( fileIt == soundFiles.end() ) {
            continue;
        }
        const uint64_t rsc = fileIt->second;
        if ( bfx.bLanguageSwitch && rsc != languageRsc ) {
            continue;
        }
        const BankEntry *pWav = findWav( rsc );
        if ( pWav == nullptr ) {
            continue;
        }
        Sound sound;
        for ( const SndNode &node : bfx.Nodes ) {
            if ( node.Rsc == rsc ) {
                sound = node.Params;
                break;
            }
        }
        sound.Name = pWav->Name;
        sound.Clip = gpAudio->loadClip( pWav->pData, pWav->Size, pWav->Name.c_str() );
        if ( sound.Clip != nullptr ) {
            list.push_back( std::move( sound ) );
        }
    }
    return &list;
}

const std::vector<CinematicSounds::Sound> *CinematicSounds::findEmitter( const char *pName ) {
    auto cached = resolvedEmitters.find( pName );
    if ( cached != resolvedEmitters.end() ) {
        return &cached->second;
    }
    auto it = emitters.find( pName );
    if ( it == emitters.end() ) {
        return nullptr;
    }
    const std::vector<Sound> *pSounds = findRoot( it->second.first );
    std::vector<Sound> &list = resolvedEmitters[pName];
    if ( pSounds != nullptr ) {
        list = *pSounds;
        for ( Sound &sound : list ) {
            sound.bHasPosition = true;
            sound.Position = it->second.second;
        }
    }
    return &list;
}

float CinematicSounds::randomUnit() {
    // SC_SoundEmiter LCG (emitter +0x114): s = s * 0x7ff8a3ed + 0x2aa01d31,
    // u = s / 2^32.
    randomSeed = randomSeed * 0x7ff8a3edu + 0x2aa01d31u;
    return static_cast<float>( randomSeed / 4294967296.0 );
}

float CinematicSounds::randomVolume( float randomness, float volume ) {
    // RandVol 0xb3a180 (SC emitters; [INF] same rule for the .bfx VolRnd).
    if ( randomness == 0.0f ) {
        return volume;
    }
    return Clamp( volume + randomness * randomUnit() - 0.5f * randomness, 0.0f, 1.0f );
}

float CinematicSounds::randomPitch( float randomness, float pitch ) {
    // RandPitch 0xb3a230 (SC emitters; [INF] same rule for the .bfx PtchRnd):
    // r = 2u - 1; r >= 0: F (1 + r FR). r < 0: [INF] interpolation from F
    // to F / (1 + FR) (the traced x87 form "r F + (1 - r) F / (1 + FR)" is
    // discontinuous at r = 0, spec 4.3).
    if ( randomness == 0.0f ) {
        return pitch;
    }
    const float r = 2.0f * randomUnit() - 1.0f;
    if ( r >= 0.0f ) {
        return pitch * ( 1.0f + r * randomness );
    }
    return ( 1.0f + r ) * pitch - r * pitch / ( 1.0f + randomness );
}

void CinematicSounds::play( const std::vector<Sound> &sounds, float offset, bool bLoop,
                            std::vector<GSAudio::VoiceHandle> *pOutVoices, const Vector3f *pWorldPosition,
                            uint32_t fxFlags ) {
    if ( gpAudio == nullptr ) {
        return;
    }
    if ( voices.size() > 64 ) {
        voices.erase( std::remove_if( voices.begin(), voices.end(),
                                      []( GSAudio::VoiceHandle voice ) { return !gpAudio->isPlaying( voice ); } ),
                      voices.end() );
    }
    for ( const Sound &sound : sounds ) {
        if ( sound.Clip == nullptr ) {
            continue;
        }
        GSAudio::VoiceParams params;
        params.Client = sound.Client;
        params.Volume = randomVolume( sound.VolumeRandom, sound.Volume );
        params.Pitch = randomPitch( sound.PitchRandom, sound.Pitch );
        params.bLoop = bLoop || sound.bLoop;
        // Late start: elapsed time at the playback rate.
        float start = offset * params.Pitch;
        if ( params.bLoop && sound.Clip->Duration > 0.0f ) {
            start = std::fmod( start, sound.Clip->Duration );
        }
        params.StartTime = start;
        params.ClipNear = sound.ClipNear;
        params.ClipMid = sound.ClipMid;
        params.ClipFar = sound.ClipFar;
        params.ClipMidVolume = sound.ClipMidVolume;
        params.ClipAttenuation = sound.ClipAttenuation;
        params.ConeInner = sound.ConeInner;
        params.ConeOuter = sound.ConeOuter;
        params.ConeOuterVolume = sound.ConeOuterVolume;
        // [INF] No Doppler for the .bfx sounds (no DopplerFactor field; the
        // cinematic camera cuts would give huge listener velocities).
        params.Doppler = 0.0f;
        bool bPositioned = false;
        if ( pWorldPosition != nullptr ) {
            params.Position = *pWorldPosition;
            bPositioned = true;
        } else if ( sound.bHasPosition ) {
            params.Position = sceneOrigin.transformPoint( sound.Position );
            bPositioned = true;
        }
        // Spatialized when positioned, not flagged 0x10 [INF], and on a 5
        // input pipeline (decided by GSAudio from the client).
        params.bSpatial = bPositioned && ( fxFlags & kFxFlag2D ) == 0;
        const GSAudio::VoiceHandle voice = gpAudio->play( sound.Clip, params );
        if ( voice != GSAudio::kInvalidVoice ) {
            voices.push_back( voice );
            if ( pOutVoices != nullptr ) {
                pOutVoices->push_back( voice );
            }
        }
    }
}

void CinematicSounds::stop( std::vector<GSAudio::VoiceHandle> &handles ) {
    if ( gpAudio != nullptr ) {
        for ( GSAudio::VoiceHandle voice : handles ) {
            gpAudio->stop( voice );
        }
    }
    handles.clear();
}

void CinematicSounds::update( const Cinematic &cinematic, float previousTime, float time,
                              std::vector<GSAudio::VoiceHandle> *pOutVoices ) {
    const auto &references = cinematic.getReferences();
    for ( const Cinematic::Event &event : cinematic.getFxEvents() ) {
        const bool bCrossed = event.Time > previousTime && event.Time <= time;
        const bool bStart = previousTime < 0.0f && event.Time <= 0.0f && time >= 0.0f;
        if ( !bCrossed && !bStart ) {
            continue;
        }
        if ( event.Reference < 0 || event.Reference >= static_cast<int32_t>( references.size() ) ) {
            continue;
        }
        if ( const std::vector<Sound> *pSounds = findRoot( references[event.Reference].Hashcode ) ) {
            // .cin Fx record: D3D row major matrix, translation = row 3
            // (Event::World m[3][0..2]), cinematic frame.
            const Vector3f position = sceneOrigin.transformPoint( event.World.getTranslation() );
            play( *pSounds, Max( time - event.Time, 0.0f ), false, pOutVoices, &position, event.Flags );
        }
    }
}

void CinematicSounds::updateSceneFx( const CinematicScene &scene, float previousSceneTime, float sceneTime,
                                     const Matrix4x4f &sceneToWorld, std::vector<GSAudio::VoiceHandle> *pOutVoices ) {
    const auto &references = scene.getReferences();
    for ( const CinematicScene::Object &object : scene.getObjects() ) {
        for ( const CinematicScene::Track &track : object.Tracks ) {
            // 0x54 byte records = 21 dwords.
            if ( track.Id != CinematicScene::kTrackFx || track.NumComponents < 21 ) {
                continue;
            }
            for ( size_t k = 0; k < track.Times.size(); k++ ) {
                const float eventTime = track.Times[k];
                const bool bCrossed = eventTime > previousSceneTime && eventTime <= sceneTime;
                const bool bStart = previousSceneTime < 0.0f && eventTime <= 0.0f && sceneTime >= 0.0f;
                if ( !bCrossed && !bStart ) {
                    continue;
                }
                const float *pRecord = &track.Values[k * track.NumComponents];
                const uint32_t reference = FloatBits( pRecord[0] );
                const uint32_t flags = FloatBits( pRecord[1] );
                if ( reference >= references.size() || references[reference].Type != kRefFx ) {
                    continue;
                }
                const std::vector<Sound> *pSounds = findRoot( references[reference].Hashcode );
                if ( pSounds == nullptr || pSounds->empty() ) {
                    continue;
                }
                // Record matrix at +0x14 (D3D row major, translation row 3).
                const Vector3f offset( pRecord[5 + 12], pRecord[5 + 13], pRecord[5 + 14] );
                const Vector3f position = sceneToWorld.transformPoint( object.samplePosition( eventTime ) + offset );
                play( *pSounds, Max( sceneTime - eventTime, 0.0f ), false, pOutVoices, &position, flags );
            }
        }
    }
}

void CinematicSounds::stopAll() {
    if ( gpAudio != nullptr ) {
        for ( GSAudio::VoiceHandle voice : voices ) {
            gpAudio->stop( voice );
        }
    }
    voices.clear();
}
