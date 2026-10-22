#include "shared.h"
#include "eden_uv_animation.h"

namespace {
template <typename T> bool ReadValue( const uint8_t *pData, size_t size, size_t &offset, T &outValue ) {
    if ( offset + sizeof( T ) > size ) {
        return false;
    }
    memcpy( &outValue, pData + offset, sizeof( T ) );
    offset += sizeof( T );
    return true;
}
} // namespace

float EdenUVAnimation::Channel::evaluate( float time, float defaultValue ) const {
    if ( Values.empty() ) {
        return defaultValue;
    }
    if ( Values.size() == 1 || time <= Times.front() ) {
        return Values.front();
    }
    for ( size_t i = 1; i < Times.size(); i++ ) {
        if ( time <= Times[i] ) {
            const float span = Times[i] - Times[i - 1];
            const float t = span > 0.0f ? ( time - Times[i - 1] ) / span : 1.0f;
            return Values[i - 1] + ( Values[i] - Values[i - 1] ) * t;
        }
    }
    return Values.back();
}

bool EdenUVAnimation::parse( const uint8_t *pData, size_t size ) {
    if ( size < 0x20 || memcmp( pData, "ANM!", 4 ) != 0 ) {
        return false;
    }
    memcpy( &Id, pData + 0x10, sizeof( Id ) );
    if ( memcmp( pData + 0x18, "DATA", 4 ) != 0 ) {
        return false;
    }
    size_t offset = 0x20;
    uint32_t numTracks = 0;
    float duration = 0.0f;
    float duration2 = 0.0f;
    if ( !ReadValue( pData, size, offset, numTracks ) || !ReadValue( pData, size, offset, duration ) ||
         !ReadValue( pData, size, offset, duration2 ) || numTracks == 0 ) {
        return false;
    }
    Duration = duration;
    offset += 3 * 4 + 2 * 4; // u32[3] 0, u32 0x1f, u32 0x64
    if ( offset + 4 + 16 > size || memcmp( pData + offset, "MAT!", 4 ) != 0 ) {
        return false;
    }
    offset += 4 + 16;
    for ( Channel &channel : Channels ) {
        uint32_t numKeys = 0;
        float length = 0.0f;
        if ( !ReadValue( pData, size, offset, numKeys ) || !ReadValue( pData, size, offset, length ) ||
             offset + static_cast<size_t>( numKeys ) * 8 > size ) {
            return false;
        }
        channel.Times.resize( numKeys );
        channel.Values.resize( numKeys );
        memcpy( channel.Times.data(), pData + offset, numKeys * 4 );
        offset += numKeys * 4;
        memcpy( channel.Values.data(), pData + offset, numKeys * 4 );
        offset += numKeys * 4;
    }
    return true;
}

Vector4f EdenUVAnimation::evaluate( float time ) const {
    float t = time;
    if ( Duration > 0.0f ) {
        t = std::fmod( time, Duration );
        if ( t < 0.0f ) {
            t += Duration;
        }
    }
    return Vector4f( Channels[0].evaluate( t, 0.0f ), Channels[1].evaluate( t, 0.0f ), Channels[2].evaluate( t, 1.0f ),
                     Channels[3].evaluate( t, 1.0f ) );
}

void EdenUVAnimation::evaluateRegisters( float time, Vector4f outRegisters[4] ) const {
    const Vector4f value = evaluate( time );
    outRegisters[0] = Vector4f( value.z, 0.0f, 0.0f, value.x );
    outRegisters[1] = Vector4f( 0.0f, value.w, 0.0f, value.y );
    outRegisters[2] = Vector4f( 0.0f, 0.0f, 1.0f, 0.0f );
    outRegisters[3] = Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
}

bool EdenUVAnimation::ParseName( const std::string &name, uint32_t &outSlot, std::vector<std::string> &outMaterials ) {
    outMaterials.clear();
    const size_t separator = name.rfind( '_' );
    if ( separator == std::string::npos || separator == 0 || separator + 1 >= name.size() ) {
        return false;
    }
    for ( size_t i = separator + 1; i < name.size(); i++ ) {
        if ( name[i] < '0' || name[i] > '9' ) {
            return false;
        }
    }
    outSlot = static_cast<uint32_t>( atoi( name.c_str() + separator + 1 ) );
    const std::string prefix = name.substr( 0, separator );
    outMaterials.push_back( prefix );
    // Optional slot name ("caustic_Normal1_1").
    const size_t slotName = prefix.rfind( '_' );
    if ( slotName != std::string::npos && slotName > 0 ) {
        outMaterials.push_back( prefix.substr( 0, slotName ) );
    }
    return true;
}
