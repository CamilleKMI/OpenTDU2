#include "shared.h"
#include "event_reactions.h"

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "game/avatar/eden_avatar.h"
#include "game/cinematic/cinematic_audio.h"
#include "system/cmd_line.h"

namespace {
// FUN_00a151c0: clip start parameters (DAT_0144b618 = DAT_00ede42c) [CODE];
// used as the blend time [INF].
constexpr float kBlendTime = 0.3f;
// FaceFX fade DAT_00ede428 (FUN_00b77600 / FUN_00b77b20) [CODE].
constexpr float kFaceFxFade = 0.2f;

uint32_t ReadU32( const uint8_t *p ) {
    uint32_t v;
    memcpy( &v, p, 4 );
    return v;
}
float ReadF32( const uint8_t *p ) {
    float v;
    memcpy( &v, p, 4 );
    return v;
}

std::string FileStem( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    return dot != std::string::npos ? file.substr( 0, dot ) : file;
}

// .bas REFS: { u32 count; { u16 type (0x0301); u16 size; u32 1; u32 n;
// u64 hash; char name[] } } -> the .xan name.
std::string BasClipName( const uint8_t *pData, size_t size ) {
    for ( size_t p = 0x10; p + 8 <= size; ) {
        const uint32_t chunkSize = ReadU32( pData + p + 4 );
        if ( memcmp( pData + p, "REFS", 4 ) == 0 && chunkSize >= 0x18 && p + 8 + chunkSize <= size ) {
            const char *pName = reinterpret_cast<const char *>( pData + p + 8 + 0x18 );
            const size_t maxLength = chunkSize - 0x18;
            return FileStem( std::string( pName, strnlen( pName, maxLength ) ) );
        }
        if ( memcmp( pData + p, "EOF!", 4 ) == 0 ) {
            break;
        }
        p += 8 + chunkSize;
    }
    return std::string();
}
} // namespace

bool EventReactions::ParseTrack( const uint8_t *pData, size_t size, Reaction &out ) {
    if ( size < 0x34 || memcmp( pData, "EVT!", 4 ) != 0 || memcmp( pData + 0x10, "DATA", 4 ) != 0 ) {
        return false;
    }
    const uint32_t dataSize = ReadU32( pData + 0x14 );
    const size_t dataEnd = Min<size_t>( 0x18 + dataSize, size );
    out.Duration = ReadF32( pData + 0x1c );
    // REFS (after DATA).
    struct Ref {
        uint16_t Type = 0;
        std::string Name;
        uint64_t Hash = 0;
    };
    std::vector<Ref> refs;
    if ( dataEnd + 12 <= size && memcmp( pData + dataEnd, "REFS", 4 ) == 0 ) {
        const uint32_t count = ReadU32( pData + dataEnd + 8 );
        size_t p = dataEnd + 12;
        for ( uint32_t i = 0; i < count && p + 4 <= size; i++ ) {
            Ref ref;
            memcpy( &ref.Type, pData + p, 2 );
            uint16_t entrySize = 0;
            memcpy( &entrySize, pData + p + 2, 2 );
            if ( entrySize < 12 || p + entrySize > size ) {
                break;
            }
            if ( ref.Type == 0x0004 && entrySize >= 20 ) {
                memcpy( &ref.Hash, pData + p + 12, 8 );
            } else {
                const char *pName = reinterpret_cast<const char *>( pData + p + 12 );
                ref.Name.assign( pName, strnlen( pName, entrySize - 12 ) );
            }
            refs.push_back( ref );
            p += entrySize;
        }
    }
    for ( size_t p = 0x34; p + 8 <= dataEnd; ) {
        const uint32_t itemSize = ReadU32( pData + p );
        if ( itemSize < 8 || p + itemSize > dataEnd ) {
            break;
        }
        const uint8_t *pBody = pData + p + 8;
        if ( memcmp( pData + p + 4, "MORF", 4 ) == 0 && itemSize >= 16 ) {
            const uint32_t n = ReadU32( pBody );
            if ( 8 + n * 12 <= itemSize - 8 ) {
                for ( uint32_t k = 0; k < n; k++ ) {
                    Morph morph;
                    morph.Time = ReadF32( pBody + 8 + 4 * k );
                    const uint32_t ref = ReadU32( pBody + 8 + 4 * n + 8 * k );
                    morph.Length = ReadF32( pBody + 8 + 4 * n + 8 * k + 4 );
                    if ( ref < refs.size() ) {
                        morph.Anim = refs[ref].Name;
                    }
                    out.Morphs.push_back( morph );
                }
            }
        } else if ( memcmp( pData + p + 4, "FXS!", 4 ) == 0 && itemSize >= 0x28 ) {
            Sound sound;
            sound.Start = ReadF32( pBody + 4 );
            const uint32_t ref = ReadU32( pBody + 16 );
            sound.Flags = ReadU32( pBody + 20 );
            sound.Duration = ReadF32( pBody + 28 );
            if ( ref < refs.size() ) {
                sound.Root = refs[ref].Hash;
            }
            out.Sounds.push_back( sound );
        }
        p += itemSize;
    }
    return true;
}

bool EventReactions::load( const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities,
                           const char *pHandler ) {
    unload();
    XmbValue entityRoot = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__AnimationHandler" ); i++ ) {
        XmbValue handler = entityRoot.getArrayElement( "ArrayOf_Entity__AnimationHandler", i );
        if ( handler["__Name"].asString() != pHandler ) {
            continue;
        }
        for ( uint32_t a = 0; a < handler.getArraySize( "Animation" ); a++ ) {
            XmbValue anm = handler.getArrayElement( "Animation", a );
            Reaction reaction;
            const BankEntry *pBas = anm.getArraySize( "File" ) > 0
                                        ? hashcodes.find( anm.getArrayElement( "File", 0 )["rsc"].asUInt64() )
                                        : nullptr;
            const BankEntry *pTrk = anm.getArraySize( "File_trk" ) > 0
                                        ? hashcodes.find( anm.getArrayElement( "File_trk", 0 )["rsc"].asUInt64() )
                                        : nullptr;
            if ( pBas != nullptr ) {
                reaction.Name = BasClipName( pBas->pData, pBas->Size );
                const BankEntry *pAnm = bank.findEntry( reaction.Name.c_str(), "anm" );
                if ( pAnm == nullptr || !reaction.Clip.parse( pAnm->pData, pAnm->Size ) ) {
                    OTDU_LOG_WARN( "EventReactions: %s[%u] clip '%s' not found\n", pHandler, a, reaction.Name.c_str() );
                }
            }
            if ( pTrk == nullptr || !ParseTrack( pTrk->pData, pTrk->Size, reaction ) ) {
                reaction.Duration = reaction.Clip.getDuration();
            }
            OTDU_LOG_DEBUG( "EventReactions: %s[%u] %s %.2f s, %zu FaceFX keys, %zu sounds\n", pHandler, a,
                            reaction.Name.c_str(), reaction.Duration, reaction.Morphs.size(), reaction.Sounds.size() );
            reactions.push_back( std::move( reaction ) );
        }
        break;
    }
    played.assign( reactions.size(), 0 );
    OTDU_LOG_INFO( "EventReactions: %s, %zu reactions\n", pHandler, reactions.size() );
    return !reactions.empty();
}

void EventReactions::unload() {
    reactions.clear();
    played.clear();
    current = -1;
    voices.clear();
}

const std::string &EventReactions::getName( int32_t index ) const {
    static const std::string kNone;
    return index >= 0 && index < static_cast<int32_t>( reactions.size() ) ? reactions[index].Name : kNone;
}

int32_t EventReactions::play( int32_t first, int32_t count, CinematicSounds &sounds ) {
    if ( count <= 0 || first < 0 || first + count > static_cast<int32_t>( reactions.size() ) || isPlaying() ) {
        return -1;
    }
    auto random = [&]() {
        seed = seed * 0x7ff8a3edu + 0x2aa01d31u;
        return static_cast<int32_t>( ( seed >> 16 ) % static_cast<uint32_t>( count ) ); // [INF] FUN_006c8f00
    };
    int32_t index = random() + first;
    if ( played[index] != 0 ) {
        index = -1;
        for ( int32_t i = first; i < first + count; i++ ) {
            if ( played[i] == 0 ) {
                index = i;
                break;
            }
        }
        if ( index < 0 ) {
            std::fill( played.begin() + first, played.begin() + first + count, 0 );
            index = Clamp( random(), 0, count - 1 ) + first;
        }
    }
    sounds.stop( voices );
    current = index;
    time = 0.0f;
    played[index] = 1;
    OTDU_LOG_INFO( "EventReactions: family (%d, %d) -> %d %s (%.2f s)\n", first, count, index,
                   reactions[index].Name.c_str(), reactions[index].Duration );
    return index;
}

void EventReactions::stop( CinematicSounds &sounds ) {
    sounds.stop( voices );
    current = -1;
}

void EventReactions::tick( float deltaTime, CinematicSounds &sounds ) {
    if ( current < 0 ) {
        return;
    }
    const Reaction &reaction = reactions[current];
    const float previous = time;
    time += deltaTime;
    static const bool bNoAudio = CmdLine::HasArg( "-noaudio" );
    for ( const Sound &sound : reaction.Sounds ) {
        const bool bCrossed = previous <= 0.0f ? sound.Start <= time : sound.Start > previous && sound.Start <= time;
        if ( !bCrossed || bNoAudio || sound.Root == 0 ) {
            continue;
        }
        if ( const std::vector<CinematicSounds::Sound> *pSounds = sounds.findRoot( sound.Root ) ) {
            // Flags 0x37: bit 0x10 -> not positional [INF, CinematicSounds].
            sounds.play( *pSounds, time - sound.Start, false, &voices, nullptr, sound.Flags );
            OTDU_LOG_INFO( "EventReactions: %s voice %s at %.2f s\n", reaction.Name.c_str(),
                           pSounds->empty() ? "?" : pSounds->front().Name.c_str(), sound.Start );
        } else {
            OTDU_LOG_WARN( "EventReactions: %s sound 0x%016llx not found\n", reaction.Name.c_str(),
                           static_cast<unsigned long long>( sound.Root ) );
        }
    }
    const float duration = reaction.Duration > 0.0f ? reaction.Duration : reaction.Clip.getDuration();
    if ( time >= duration ) {
        current = -1;
    }
}

const EdenAnimation *EventReactions::getClip() const {
    return current >= 0 ? &reactions[current].Clip : nullptr;
}

float EventReactions::getWeight() const {
    if ( current < 0 ) {
        return 0.0f;
    }
    const Reaction &reaction = reactions[current];
    const float duration = reaction.Duration > 0.0f ? reaction.Duration : reaction.Clip.getDuration();
    return Clamp( Min( time / kBlendTime, ( duration - time ) / kBlendTime ), 0.0f, 1.0f );
}

void EventReactions::applyFaceFx( const FaceFxLibrary &faceFx, EdenAvatar &avatar,
                                  std::vector<BoneTransform> &locals ) const {
    if ( current < 0 || !faceFx.isLoaded() ) {
        return;
    }
    const FaceFxActor *pActor = faceFx.getActor( avatar.isWoman() );
    if ( pActor == nullptr ) {
        return;
    }
    const Reaction &reaction = reactions[current];
    // Last key <= time, faded in / out like the .sce Morph track
    // (IntroCinematic::applyFaceFx) [INF].
    const Morph *pMorph = nullptr;
    for ( const Morph &morph : reaction.Morphs ) {
        if ( morph.Time <= time ) {
            pMorph = &morph;
        }
    }
    const FaceFxAnim *pAnim = pMorph != nullptr ? faceFx.findAnim( *pActor, pMorph->Anim ) : nullptr;
    static const bool bLogFaceFx = CmdLine::HasArg( "-logfacefx" );
    if ( bLogFaceFx && pMorph != nullptr ) {
        OTDU_LOG_INFO( "EventReactions: FaceFX '%s' %s at %.2f s\n", pMorph->Anim.c_str(), pAnim ? "found" : "missing",
                       time );
    }
    if ( pAnim == nullptr ) {
        avatar.setFacialCoefficients( Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ), Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
        return;
    }
    const float animTime = time - pMorph->Time;
    float weight = Min( animTime / kFaceFxFade, 1.0f );
    if ( animTime > pAnim->EndTime ) {
        weight = Min( weight, 1.0f - ( animTime - pAnim->EndTime ) / kFaceFxFade );
    }
    weight = Max( weight, 0.0f );
    if ( weight <= 0.0f ) {
        avatar.setFacialCoefficients( Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ), Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
        return;
    }
    FaceFxPose pose;
    FaceFxLibrary::Evaluate( *pActor, pAnim, Min( animTime, pAnim->EndTime ), avatar.getFaceParams(), nullptr, pose );
    const EdenSkeleton &skeleton = avatar.getSkeleton();
    for ( const auto &entry : pose.Bones ) {
        const int32_t bone = skeleton.findBone( entry.first );
        if ( bone < 0 || static_cast<size_t>( bone ) >= locals.size() ) {
            continue;
        }
        if ( weight >= 1.0f ) {
            locals[bone] = entry.second;
        } else {
            std::vector<BoneTransform> a( 1, locals[bone] ), b( 1, entry.second ), out;
            BlendPoses( a, b, weight, out );
            locals[bone] = out[0];
        }
    }
    auto scale = []( const Vector4f &v, float w ) { return Vector4f( v.x * w, v.y * w, v.z * w, v.w * w ); };
    avatar.setFacialCoefficients( scale( pose.FacialCoef1, weight ), scale( pose.FacialCoef2, weight ) );
}
