#include "shared.h"
#include "ige_file.h"

#include "filesystem/gs_file.h"

namespace {
uint32_t ReadU32BE( const uint8_t *p ) {
    return static_cast<uint32_t>( p[0] ) << 24 | static_cast<uint32_t>( p[1] ) << 16 |
           static_cast<uint32_t>( p[2] ) << 8 | p[3];
}

float ReadF32BE( const uint8_t *p ) {
    const uint32_t bits = ReadU32BE( p );
    float value;
    memcpy( &value, &bits, sizeof( value ) );
    return value;
}
} // namespace

int64_t IgeFile::Param::asInt() const {
    int64_t value = 0;
    for ( uint8_t byte : Value ) {
        value = ( value << 8 ) | byte;
    }
    if ( Value.size() == 4 ) {
        value = static_cast<int32_t>( static_cast<uint32_t>( value ) );
    }
    return value;
}

std::string IgeFile::Param::asString() const {
    std::string text( Value.begin(), Value.end() );
    const size_t end = text.find( '\0' );
    return end != std::string::npos ? text.substr( 0, end ) : text;
}

bool IgeFile::load( const char *pPath ) {
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( pPath, data ) ) {
        return false;
    }
    return parse( data.data(), data.size() );
}

bool IgeFile::parse( const uint8_t *pData, size_t size ) {
    params.clear();
    checkpoints.clear();
    numEntities = 0;
    // FUN_00b25fc0: the first chunk must be 'IGE2' (0x49474532) [CODE].
    if ( size < 0x1c || ReadU32BE( pData ) != MakeTag( "IGE2" ) ) {
        return false;
    }
    version = ReadU32BE( pData + 8 );
    size_t offset = ReadU32BE( pData + 4 );
    while ( offset + 8 <= size ) {
        const uint32_t tag = ReadU32BE( pData + offset );
        const uint32_t chunkSize = ReadU32BE( pData + offset + 4 );
        if ( chunkSize < 8 || offset + chunkSize > size ) {
            break;
        }
        const uint8_t *pChunk = pData + offset;
        if ( tag == MakeTag( "MDSC" ) && chunkSize >= 0x18 ) {
            size_t p = 0x18;
            while ( p + 8 <= chunkSize ) {
                Param param;
                param.Tag = ReadU32BE( pChunk + p );
                param.Group = pChunk[p + 5];
                const uint16_t valueSize = static_cast<uint16_t>( pChunk[p + 6] << 8 | pChunk[p + 7] );
                if ( p + 8 + valueSize > chunkSize ) {
                    break;
                }
                param.Value.assign( pChunk + p + 8, pChunk + p + 8 + valueSize );
                params.push_back( std::move( param ) );
                p += 8 + valueSize;
            }
        } else if ( tag == MakeTag( "ENTI" ) && chunkSize >= 12 ) {
            numEntities = ReadU32BE( pChunk + 8 );
            size_t p = 12;
            for ( uint32_t i = 0; i < numEntities && p + 8 <= chunkSize; i++ ) {
                const uint32_t entityTag = ReadU32BE( pChunk + p );
                const uint32_t entitySize = ReadU32BE( pChunk + p + 4 );
                if ( entitySize < 8 || p + entitySize > chunkSize ) {
                    break;
                }
                if ( entityTag == MakeTag( "CHK2" ) && entitySize >= 0x7c ) {
                    const uint8_t *e = pChunk + p + 8;
                    Checkpoint cp;
                    cp.Id = ReadU32BE( e + 4 );
                    for ( int32_t k = 0; k < 3; k++ ) {
                        cp.Fields[k] = ReadU32BE( e + 8 + 4 * k );
                    }
                    cp.K = ReadF32BE( e + 0x14 );
                    // Rows of a 3x4 [R | T] in the column-vector convention:
                    // the axes are the columns of R (the start gate's third
                    // column runs along the sea-front road; the third row is
                    // across it) [DATA + render check].
                    float m[3][4];
                    for ( int32_t r = 0; r < 3; r++ ) {
                        for ( int32_t c = 0; c < 4; c++ ) {
                            m[r][c] = ReadF32BE( e + 0x18 + 16 * r + 4 * c );
                        }
                    }
                    cp.Right = Vector3f( m[0][0], m[1][0], m[2][0] );
                    cp.Up = Vector3f( m[0][1], m[1][1], m[2][1] );
                    cp.Forward = Vector3f( m[0][2], m[1][2], m[2][2] );
                    cp.Position = Vector3f( m[0][3], m[1][3], m[2][3] );
                    cp.Width = ReadF32BE( e + 0x60 );
                    checkpoints.push_back( cp );
                }
                p += entitySize;
            }
        }
        offset += chunkSize;
    }
    return true;
}

const IgeFile::Param *IgeFile::find( uint32_t tag ) const {
    for ( const Param &param : params ) {
        if ( param.Tag == tag ) {
            return &param;
        }
    }
    return nullptr;
}

int64_t IgeFile::getInt( uint32_t tag, int64_t defaultValue ) const {
    const Param *pParam = find( tag );
    return pParam != nullptr ? pParam->asInt() : defaultValue;
}
