#pragma once

#include "pch.h"

#if defined( _WIN32 )
#define OTDU_WIN32 1
#elif defined( __APPLE__ )
#define OTDU_MACOS 1
#define OTDU_UNIX 1
#else
#define OTDU_UNIX 1
#endif

#if defined( __x86_64__ ) || defined( _WIN64 ) || defined( __aarch64__ ) ||  \
    defined( __arm64__ )
#define OTDU_64_BITS 1
#define OTDU_32_BITS 0
#else
#define OTDU_64_BITS 0
#define OTDU_32_BITS 1
#endif

extern const char **gpCmdLineArgs;
extern int32_t gCmdLineArgCount;

#include "core/logger.h"
#include "core/assert.h"

// Function/struct reconstructed from the original binary but not implemented
// yet (TestDrive2.exe, MC_PC branch, build 2015-01-12).
#ifdef OTDU_DEVBUILD
#define OTDU_UNIMPLEMENTED OTDU_ASSERT_TRANSIENT( false && "UNIMPLEMENTED" );
#else
#define OTDU_UNIMPLEMENTED                                                     \
    OTDU_LOG_DEBUG( "UNIMPLEMENTED: %s\n", __FUNCTION__ );
#endif

// Static assert checking the size of a struct read straight from an asset
// (all TDU2 assets were authored for a 32-bit little-endian x86 client).
#define OTDU_SIZE_MUST_MATCH( obj, size )                                      \
    static_assert(                                                             \
        sizeof( obj ) == size,                                                 \
        #obj ": size does not match (asset deserialization will fail!)" )

static constexpr uint32_t OTDU_MAX_PATH = 260;

template <typename T> inline T Min( T left, T right ) {
    return left < right ? left : right;
}
template <typename T> inline T Max( T left, T right ) {
    return left < right ? right : left;
}
template <typename T> inline T Clamp( T value, T minValue, T maxValue ) {
    return Max( Min( value, maxValue ), minValue );
}
template <typename T> inline T Saturate( T value ) {
    return Max( Min( value, T( 1 ) ), T( 0 ) );
}
template <typename T, typename R> constexpr T Align( T value, R to ) {
    return ( value + ( to - 1 ) ) & ~( ( to - 1 ) );
}

inline int32_t StringCmpI( const char *x, const char *y ) {
#if defined( OTDU_WIN32 )
    return _stricmp( x, y );
#else
    return strcasecmp( x, y );
#endif
}

inline int32_t StringNCmpI( const char *x, const char *y, size_t n ) {
#if defined( OTDU_WIN32 )
    return _strnicmp( x, y, n );
#else
    return strncasecmp( x, y, n );
#endif
}

inline std::string ToLower( std::string str ) {
    for ( char &c : str ) {
        c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
    }
    return str;
}

inline std::string ToUpper( std::string str ) {
    for ( char &c : str ) {
        c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
    }
    return str;
}

// Eden "hashcode" identifiers (64-bit): uppercase characters accumulated in
// 8 byte slots (i.e. the first 8 characters packed in a little-endian integer
// for short names such as "PAGE_001"). TestDriveConcatToString8 in OpenTDU.
inline uint64_t GetIdentifier64bit( const char *pString ) {
    uint8_t slots[8] = { 0 };
    for ( size_t i = 0; pString[i] != '\0'; i++ ) {
        char c = pString[i];
        if ( c > '`' && c < '{' ) {
            c = static_cast<char>( c - 0x20 );
        }
        slots[i & 7] = static_cast<uint8_t>( slots[i & 7] + static_cast<uint8_t>( c ) );
    }
    uint64_t hashcode = 0ull;
    for ( uint32_t i = 0; i < 8; i++ ) {
        hashcode |= static_cast<uint64_t>( slots[i] ) << ( i * 8 );
    }
    return hashcode;
}

inline std::string IdentifierToString( uint64_t hashcode ) {
    std::string str;
    for ( uint32_t i = 0; i < 8; i++ ) {
        char c = static_cast<char>( ( hashcode >> ( i * 8 ) ) & 0xff );
        if ( c == '\0' ) {
            break;
        }
        str += c;
    }
    return str;
}

namespace TestDrive {
void *Alloc( size_t size );
void *AllocAligned( size_t size, size_t alignment );
void Free( void *pMemory );
void FreeAligned( void *pMemory );
} // namespace TestDrive
