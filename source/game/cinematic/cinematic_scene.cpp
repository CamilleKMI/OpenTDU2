#include "shared.h"
#include "cinematic_scene.h"

#include "render/render_file.h"

namespace {
template <typename T> T ReadAt( const uint8_t *p, size_t offset ) {
    T value;
    memcpy( &value, p + offset, sizeof( T ) );
    return value;
}

constexpr uint32_t kSceneMagic = MakeFourCC( 'S', 'C', 'E', '!' );
constexpr uint32_t kCinematicMagic = MakeFourCC( 'C', 'I', 'N', '!' );
constexpr uint32_t kDataMagic = MakeFourCC( 'D', 'A', 'T', 'A' );
constexpr uint32_t kRefsMagic = MakeFourCC( 'R', 'E', 'F', 'S' );
constexpr size_t kObjectHeaderSize = 0x3c;

uint32_t FindKey( const std::vector<float> &times, float time, float &outBlend ) {
    outBlend = 0.0f;
    if ( times.size() < 2 || time <= times.front() ) {
        return 0;
    }
    if ( time >= times.back() ) {
        return static_cast<uint32_t>( times.size() - 1 );
    }
    for ( uint32_t i = 0; i + 1 < times.size(); i++ ) {
        if ( time < times[i + 1] ) {
            const float span = times[i + 1] - times[i];
            outBlend = span > 0.0f ? ( time - times[i] ) / span : 0.0f;
            return i;
        }
    }
    return static_cast<uint32_t>( times.size() - 1 );
}
} // namespace

const CinematicScene::Track *CinematicScene::Object::findTrack( uint32_t id ) const {
    for ( const Track &track : Tracks ) {
        if ( track.Id == id ) {
            return &track;
        }
    }
    return nullptr;
}

const CinematicScene::Property *CinematicScene::Object::findProperty( const char *pName ) const {
    for ( const Property &property : Properties ) {
        if ( property.Name == pName ) {
            return &property;
        }
    }
    return nullptr;
}

const std::string &CinematicScene::Property::valueAt( float time ) const {
    static const std::string kEmpty;
    const std::string *pValue = &kEmpty;
    for ( size_t k = 0; k < Times.size() && k < Values.size(); k++ ) {
        if ( Times[k] <= time ) {
            pValue = &Values[k];
        }
    }
    return *pValue;
}

Vector3f CinematicScene::Object::samplePosition( float time ) const {
    const Track *pTrack = findTrack( kTrackPosition );
    if ( pTrack == nullptr || pTrack->NumComponents < 3 || pTrack->Times.empty() ) {
        return Vector3f( 0.0f, 0.0f, 0.0f );
    }
    float blend = 0.0f;
    const uint32_t key = FindKey( pTrack->Times, time, blend );
    const float *a = &pTrack->Values[key * pTrack->NumComponents];
    if ( blend <= 0.0f || key + 1 >= pTrack->Times.size() ) {
        return Vector3f( a[0], a[1], a[2] );
    }
    const float *b = &pTrack->Values[( key + 1 ) * pTrack->NumComponents];
    return Vector3f( a[0] + ( b[0] - a[0] ) * blend, a[1] + ( b[1] - a[1] ) * blend, a[2] + ( b[2] - a[2] ) * blend );
}

Quaternionf CinematicScene::Object::sampleRotation( float time ) const {
    const Track *pTrack = findTrack( kTrackRotation );
    if ( pTrack == nullptr || pTrack->NumComponents < 4 || pTrack->Times.empty() ) {
        return Quaternionf();
    }
    float blend = 0.0f;
    const uint32_t key = FindKey( pTrack->Times, time, blend );
    const float *a = &pTrack->Values[key * 4];
    Quaternionf qa( a[0], a[1], a[2], a[3] );
    if ( blend <= 0.0f || key + 1 >= pTrack->Times.size() ) {
        return qa;
    }
    const float *b = &pTrack->Values[( key + 1 ) * 4];
    Quaternionf qb( b[0], b[1], b[2], b[3] );
    float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    if ( dot < 0.0f ) {
        qb = Quaternionf( -qb.x, -qb.y, -qb.z, -qb.w );
    }
    Quaternionf r( qa.x + ( qb.x - qa.x ) * blend, qa.y + ( qb.y - qa.y ) * blend, qa.z + ( qb.z - qa.z ) * blend,
                   qa.w + ( qb.w - qa.w ) * blend );
    const float length = std::sqrt( r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w );
    return Quaternionf( r.x / length, r.y / length, r.z / length, r.w / length );
}

float CinematicScene::Object::sampleFloat( uint32_t trackId, float time, float defaultValue ) const {
    const Track *pTrack = findTrack( trackId );
    if ( pTrack == nullptr || pTrack->NumComponents < 1 || pTrack->Times.empty() ) {
        return defaultValue;
    }
    float blend = 0.0f;
    const uint32_t key = FindKey( pTrack->Times, time, blend );
    const float a = pTrack->Values[key * pTrack->NumComponents];
    if ( blend <= 0.0f || key + 1 >= pTrack->Times.size() ) {
        return a;
    }
    const float b = pTrack->Values[( key + 1 ) * pTrack->NumComponents];
    return a + ( b - a ) * blend;
}

bool CinematicScene::parseScene( const uint8_t *pData, size_t size ) {
    objects.clear();
    if ( size < 0x90 || ReadAt<uint32_t>( pData, 0 ) != kSceneMagic ) {
        return false;
    }
    size_t dataOffset = 0;
    for ( size_t offset = 0x10; offset + 8 <= size; offset += 4 ) {
        if ( ReadAt<uint32_t>( pData, offset ) == kDataMagic ) {
            dataOffset = offset + 8;
            break;
        }
    }
    if ( dataOffset == 0 || dataOffset + 0x68 > size ) {
        return false;
    }
    const uint8_t *pChunk = pData + dataOffset;
    const size_t chunkSize = Min<size_t>( ReadAt<uint32_t>( pData, dataOffset - 4 ), size - dataOffset );
    duration = ReadAt<float>( pChunk, 0 );
    const uint32_t numObjects = ReadAt<uint32_t>( pChunk, 4 );
    memcpy( &origin.m[0][0], pChunk + 0x28, sizeof( float ) * 16 );

    size_t offset = 0x68;
    for ( uint32_t i = 0; i < numObjects && offset + kObjectHeaderSize <= chunkSize; i++ ) {
        const uint32_t objectSize = ReadAt<uint32_t>( pChunk, offset );
        const uint32_t headerSize = ReadAt<uint32_t>( pChunk, offset + 4 );
        if ( objectSize < headerSize || offset + objectSize > chunkSize ) {
            break;
        }
        Object object;
        object.ClassHash = ReadAt<uint32_t>( pChunk, offset + 8 );
        object.NameHash = ReadAt<uint32_t>( pChunk, offset + 0x0c );
        object.EntityHashcode = ReadAt<uint64_t>( pChunk, offset + 0x34 );
        const char *pName = reinterpret_cast<const char *>( pChunk + offset + 0x14 );
        object.Name.assign( pName, strnlen( pName, 32 ) );

        size_t trackOffset = offset + headerSize;
        while ( trackOffset + 0x14 <= offset + objectSize ) {
            const uint32_t trackSize = ReadAt<uint32_t>( pChunk, trackOffset + 4 );
            if ( trackSize < 0x14 || trackOffset + trackSize > offset + objectSize ) {
                break;
            }
            if ( ReadAt<uint32_t>( pChunk, trackOffset + 8 ) == kTrackProperty && trackSize >= 0x10 + 0x80 ) {
                Property property;
                const char *pPropertyName = reinterpret_cast<const char *>( pChunk + trackOffset + 0x10 );
                property.Name.assign( pPropertyName, strnlen( pPropertyName, 0x80 ) );
                const uint32_t numSub = ReadAt<uint32_t>( pChunk, trackOffset + 12 );
                size_t sub = trackOffset + 0x90;
                for ( uint32_t s = 0; s < numSub && sub + 0x14 <= trackOffset + trackSize; s++ ) {
                    const uint32_t subSize = ReadAt<uint32_t>( pChunk, sub + 4 );
                    const uint32_t numKeys = ReadAt<uint32_t>( pChunk, sub + 12 );
                    if ( subSize < 0x14 || sub + subSize > trackOffset + trackSize ) {
                        break;
                    }
                    size_t key = sub + 0x14 + static_cast<size_t>( numKeys ) * 4;
                    for ( uint32_t k = 0; k < numKeys && key + 8 <= sub + subSize; k++ ) {
                        property.Times.push_back( ReadAt<float>( pChunk, sub + 0x14 + k * 4 ) );
                        const char *pValue = reinterpret_cast<const char *>( pChunk + key + 8 );
                        const size_t length = strnlen( pValue, sub + subSize - key - 8 );
                        property.Values.emplace_back( pValue, length );
                        key += 8 + length + 1; // [INF] keys packed one after the other
                    }
                    sub += subSize;
                }
                object.Properties.push_back( std::move( property ) );
                trackOffset += trackSize;
                continue;
            }
            Track track;
            track.Id = ReadAt<uint32_t>( pChunk, trackOffset + 8 );
            const uint32_t numKeys = ReadAt<uint32_t>( pChunk, trackOffset + 12 );
            if ( numKeys > 0 && numKeys < 100000 ) {
                const size_t valuesSize = trackSize - 0x14 - numKeys * 4;
                const uint32_t keySize = static_cast<uint32_t>( valuesSize / numKeys );
                track.NumComponents = keySize / 4;
                for ( uint32_t k = 0; k < numKeys; k++ ) {
                    track.Times.push_back( ReadAt<float>( pChunk, trackOffset + 0x14 + k * 4 ) );
                }
                const size_t valuesOffset = trackOffset + 0x14 + numKeys * 4;
                for ( uint32_t k = 0; k < numKeys * track.NumComponents; k++ ) {
                    track.Values.push_back( ReadAt<float>( pChunk, valuesOffset + k * 4 ) );
                }
                object.Tracks.push_back( std::move( track ) );
            }
            trackOffset += trackSize;
        }
        objects.push_back( std::move( object ) );
        offset += objectSize;
    }
    // REFS chunk (after DATA).
    references.clear();
    const size_t dataEnd = dataOffset + ReadAt<uint32_t>( pData, dataOffset - 4 );
    if ( dataEnd + 12 <= size && ReadAt<uint32_t>( pData, dataEnd ) == kRefsMagic ) {
        const uint32_t refsSize = ReadAt<uint32_t>( pData, dataEnd + 4 );
        const uint32_t numRefs = ReadAt<uint32_t>( pData, dataEnd + 8 );
        const size_t end = Min<size_t>( size, dataEnd + 8 + refsSize );
        size_t entry = dataEnd + 12;
        for ( uint32_t r = 0; r < numRefs && entry + 12 <= end; r++ ) {
            Reference reference;
            reference.Type = ReadAt<uint16_t>( pData, entry );
            const uint16_t entrySize = ReadAt<uint16_t>( pData, entry + 2 );
            if ( entrySize < ( reference.Type == kRefMessage ? 4 : 12 ) || entry + entrySize > end ) {
                break;
            }
            if ( reference.Type == kRefMessage ) {
                // Message string (lkon, lkof...): no hashcode, the text
                // follows the 4 byte header [DATA M_WALKMODE_walkmode.sce
                // "0b 02 0c 00 'lkon' 00 ..."].
                const char *pName = reinterpret_cast<const char *>( pData + entry + 4 );
                reference.Name.assign( pName, strnlen( pName, entrySize - 4 ) );
            } else {
                reference.Hashcode = ReadAt<uint64_t>( pData, entry + 4 );
                const char *pName = reinterpret_cast<const char *>( pData + entry + 12 );
                reference.Name.assign( pName, strnlen( pName, entrySize - 12 ) );
            }
            references.push_back( std::move( reference ) );
            entry += entrySize;
        }
    }
    return !objects.empty();
}

const CinematicScene::Object *CinematicScene::findObject( const char *pName ) const {
    for ( const Object &object : objects ) {
        if ( StringCmpI( object.Name.c_str(), pName ) == 0 ) {
            return &object;
        }
    }
    return nullptr;
}

const CinematicScene::Object *CinematicScene::findObjectByHash( uint32_t nameHash ) const {
    for ( const Object &object : objects ) {
        if ( object.NameHash == nameHash || CinematicNameHash( object.Name.c_str() ) == nameHash ) {
            return &object;
        }
    }
    return nullptr;
}

uint32_t CinematicNameHash( const char *pName ) {
    uint8_t bytes[4] = { 0, 0, 0, 0 };
    for ( size_t i = 0; pName[i] != '\0'; i++ ) {
        bytes[i & 3] = static_cast<uint8_t>( bytes[i & 3] + static_cast<uint8_t>( pName[i] ) );
    }
    return bytes[0] | ( bytes[1] << 8 ) | ( bytes[2] << 16 ) | ( static_cast<uint32_t>( bytes[3] ) << 24 );
}

bool Cinematic::parse( const uint8_t *pData, size_t size ) {
    references.clear();
    sceneReferences.clear();
    shots.clear();
    messages.clear();
    fxEvents.clear();
    if ( size < 0x20 || ReadAt<uint32_t>( pData, 0 ) != kCinematicMagic ) {
        return false;
    }
    size_t dataOffset = 0;
    size_t dataSize = 0;
    for ( size_t offset = 0x10; offset + 8 <= size; ) {
        const uint32_t magic = ReadAt<uint32_t>( pData, offset );
        const uint32_t chunkSize = ReadAt<uint32_t>( pData, offset + 4 );
        if ( magic == kRefsMagic ) {
            const uint32_t numRefs = ReadAt<uint32_t>( pData, offset + 8 );
            size_t entry = offset + 12;
            const size_t end = Min<size_t>( size, offset + 8 + chunkSize );
            for ( uint32_t i = 0; i < numRefs && entry + 4 <= end; i++ ) {
                Reference reference;
                reference.Type = ReadAt<uint16_t>( pData, entry );
                const uint16_t entrySize = ReadAt<uint16_t>( pData, entry + 2 );
                if ( entrySize < 4 || entry + entrySize > end ) {
                    break;
                }
                const char *pBody = reinterpret_cast<const char *>( pData + entry + 4 );
                if ( reference.Type == 0x020b ) {
                    reference.Text.assign( pBody, strnlen( pBody, entrySize - 4 ) );
                } else if ( entrySize >= 12 ) {
                    reference.Hashcode = ReadAt<uint64_t>( pData, entry + 4 );
                    if ( reference.Type == 0x0303 ) {
                        reference.Text.assign( pBody + 8, strnlen( pBody + 8, entrySize - 12 ) );
                    }
                }
                references.push_back( std::move( reference ) );
                entry += entrySize;
            }
        } else if ( magic == kDataMagic ) {
            dataOffset = offset + 8;
            dataSize = Min<size_t>( chunkSize, size - dataOffset );
        }
        if ( chunkSize == 0 ) {
            break;
        }
        offset += 8 + chunkSize;
    }
    if ( dataOffset == 0 || dataSize < 0x3c ) {
        return false;
    }
    const uint8_t *pChunk = pData + dataOffset;
    duration = ReadAt<float>( pChunk, 0 );
    const uint32_t numScenes = ReadAt<uint32_t>( pChunk, 0x0c );
    size_t offset = 0x3c;
    for ( uint32_t i = 0; i < numScenes && offset + 8 <= dataSize; i++, offset += 8 ) {
        sceneReferences.push_back( ReadAt<int32_t>( pChunk, offset + 4 ) );
    }
    uint32_t messageTrack = 0;
    while ( offset + 0x14 <= dataSize ) {
        const uint32_t trackSize = ReadAt<uint32_t>( pChunk, offset + 4 );
        if ( trackSize < 0x14 || offset + trackSize > dataSize ) {
            break;
        }
        const uint32_t id = ReadAt<uint32_t>( pChunk, offset + 8 );
        const uint32_t numKeys = ReadAt<uint32_t>( pChunk, offset + 12 );
        const size_t timesOffset = offset + 0x14;
        const size_t valuesOffset = timesOffset + numKeys * 4;
        const uint32_t valueSize = numKeys > 0 ? static_cast<uint32_t>( ( trackSize - 0x14 - numKeys * 4 ) / numKeys ) : 0;
        for ( uint32_t k = 0; k < numKeys && valueSize > 0; k++ ) {
            const float time = ReadAt<float>( pChunk, timesOffset + k * 4 );
            const size_t value = valuesOffset + k * valueSize;
            if ( id == 0 && valueSize >= 0x14 ) {
                Shot shot;
                shot.Start = time;
                shot.Duration = ReadAt<float>( pChunk, value );
                shot.Scene = ReadAt<uint32_t>( pChunk, value + 4 );
                shot.CameraHash = ReadAt<uint32_t>( pChunk, value + 8 );
                shot.CameraStart = ReadAt<float>( pChunk, value + 0x0c );
                shot.CameraEnd = ReadAt<float>( pChunk, value + 0x10 );
                if ( valueSize >= 0x28 ) {
                    shot.FadeType = ReadAt<uint32_t>( pChunk, value + 0x14 );
                    shot.FadeDuration = ReadAt<float>( pChunk, value + 0x18 );
                    shot.FadeColor = Vector3f( ReadAt<float>( pChunk, value + 0x1c ), ReadAt<float>( pChunk, value + 0x20 ),
                                               ReadAt<float>( pChunk, value + 0x24 ) );
                }
                shots.push_back( shot );
            } else if ( id == CinematicScene::kTrackMessage && valueSize >= 8 ) {
                Event event;
                event.Time = time;
                event.Reference = ReadAt<int32_t>( pChunk, value );
                event.Duration = ReadAt<float>( pChunk, value + 4 );
                event.Track = messageTrack;
                messages.push_back( event );
            } else if ( id == CinematicScene::kTrackFx && valueSize >= 0x54 ) {
                Event event;
                event.Time = time;
                event.Reference = ReadAt<int32_t>( pChunk, value );
                event.Flags = ReadAt<uint32_t>( pChunk, value + 4 );
                event.Duration = ReadAt<float>( pChunk, value + 0x0c );
                for ( int c = 0; c < 4; c++ ) {
                    for ( int r = 0; r < 4; r++ ) {
                        event.World.m[c][r] = ReadAt<float>( pChunk, value + 0x14 + ( c * 4 + r ) * 4 );
                    }
                }
                fxEvents.push_back( event );
            }
        }
        if ( id == CinematicScene::kTrackMessage ) {
            messageTrack++;
        }
        offset += trackSize;
    }
    std::stable_sort( messages.begin(), messages.end(),
                      []( const Event &a, const Event &b ) { return a.Time < b.Time; } );
    std::stable_sort( fxEvents.begin(), fxEvents.end(),
                      []( const Event &a, const Event &b ) { return a.Time < b.Time; } );
    // A cinematic may have no shot: C_LIFE_SOUND (garage) is a 0 s timeline
    // with one Fx event (the garage music).
    return !shots.empty() || !fxEvents.empty() || !messages.empty();
}

const Cinematic::Shot *Cinematic::getShot( float time ) const {
    const Shot *pShot = shots.empty() ? nullptr : &shots.front();
    for ( const Shot &shot : shots ) {
        if ( time >= shot.Start ) {
            pShot = &shot;
        }
    }
    return pShot;
}

const std::string *Cinematic::getLastMessage( const char *pPrefix, float time, float *pOutTime ) const {
    const size_t prefixLength = strlen( pPrefix );
    const std::string *pResult = nullptr;
    static thread_local std::string argument;
    for ( const Event &event : messages ) {
        if ( event.Time > time ) {
            break;
        }
        if ( event.Reference < 0 || event.Reference >= static_cast<int32_t>( references.size() ) ) {
            continue;
        }
        const std::string &text = references[event.Reference].Text;
        if ( text.compare( 0, prefixLength, pPrefix ) == 0 ) {
            pResult = &text;
            if ( pOutTime != nullptr ) {
                *pOutTime = event.Time;
            }
        }
    }
    if ( pResult == nullptr ) {
        return nullptr;
    }
    argument = pResult->substr( prefixLength );
    return &argument;
}

bool Cinematic::getCamera( const std::vector<CinematicScene> &scenes, float time, Vector3f &outPosition,
                           Quaternionf &outRotation, float &outFov ) const {
    const Shot *pShot = getShot( time );
    if ( pShot == nullptr || pShot->Scene >= scenes.size() ) {
        return false;
    }
    const CinematicScene &scene = scenes[pShot->Scene];
    const CinematicScene::Object *pCamera = scene.findObjectByHash( pShot->CameraHash );
    if ( pCamera == nullptr ) {
        return false;
    }
    // Camera local time = start + (t - shot start), within the shot range.
    const float localTime = Min( pShot->getSceneTime( time ), Max( pShot->CameraEnd, pShot->CameraStart ) );
    outPosition = scene.getOrigin().transformPoint( pCamera->samplePosition( localTime ) );
    outRotation = pCamera->sampleRotation( localTime );
    outFov = pCamera->sampleFloat( CinematicScene::kTrackFov, localTime, 60.0f ) * kDegToRad;
    return true;
}
