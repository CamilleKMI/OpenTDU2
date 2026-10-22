#include "shared.h"
#include "xmb.h"

#include "core/crypto/cpr.h"
#include "filesystem/gs_file.h"

namespace {
constexpr uint32_t kXmbMagic = 0x46424d58; // XMBF
constexpr uint32_t kXmbPaddingRecord = 0x41414141;

uint32_t GetPrimitiveSize( eXmbPrimitive primitive ) {
    switch ( primitive ) {
    case eXmbPrimitive::XP_Bool:
    case eXmbPrimitive::XP_Int8:
    case eXmbPrimitive::XP_UInt8:
        return 1;
    case eXmbPrimitive::XP_Int16:
    case eXmbPrimitive::XP_UInt16:
        return 2;
    case eXmbPrimitive::XP_Int64:
    case eXmbPrimitive::XP_UInt64:
    case eXmbPrimitive::XP_Double:
        return 8;
    default:
        return 4;
    }
}

inline uint16_t ByteSwap16( uint16_t v ) {
    return static_cast<uint16_t>( ( v >> 8 ) | ( v << 8 ) );
}
inline uint32_t ByteSwap32( uint32_t v ) {
    return ( v >> 24 ) | ( ( v >> 8 ) & 0xff00 ) | ( ( v << 8 ) & 0xff0000 ) |
           ( v << 24 );
}
inline uint64_t ByteSwap64( uint64_t v ) {
    return ( static_cast<uint64_t>( ByteSwap32( v & 0xffffffff ) ) << 32 ) |
           ByteSwap32( static_cast<uint32_t>( v >> 32 ) );
}
} // namespace

const XmbType::Field *XmbType::findField( const char *pName ) const {
    for ( const Field &field : Fields ) {
        if ( field.Name == pName ) {
            return &field;
        }
    }
    return nullptr;
}

uint32_t XmbDocument::readFileU32( uint32_t absoluteOffset ) const {
    uint32_t value = 0;
    memcpy( &value, data.data() + absoluteOffset, sizeof( uint32_t ) );
    return bBigEndian ? ByteSwap32( value ) : value;
}

bool XmbDocument::load( const uint8_t *pBuffer, size_t size ) {
    data.clear();
    types.clear();

    if ( pBuffer == nullptr || size < 0x1c ) {
        return false;
    }

    uint32_t magic = 0;
    memcpy( &magic, pBuffer, sizeof( uint32_t ) );
    if ( magic != kXmbMagic ) {
        OTDU_LOG_WARN( "Invalid XMB magic\n" );
        return false;
    }

    data.assign( pBuffer, pBuffer + size );

    // Header offsets are small, so the endianness can be guessed from the
    // string table offset.
    bBigEndian = ( data[8] == 0 && data[9] == 0 );

    const uint32_t stringTableOffset = readFileU32( 0x08 );
    const uint32_t typeTableOffset = readFileU32( 0x0c );
    dataStart = readFileU32( 0x10 );
    rootType = readFileU32( 0x18 ) / 2;

    if ( stringTableOffset >= size || typeTableOffset >= size ||
         dataStart > size ) {
        OTDU_LOG_WARN( "Malformed XMB header\n" );
        return false;
    }

    auto getName = [&]( uint32_t nameOffset ) -> std::string {
        uint32_t absolute = stringTableOffset + nameOffset;
        if ( absolute >= typeTableOffset ) {
            return std::string();
        }
        const char *pString =
            reinterpret_cast<const char *>( data.data() + absolute );
        size_t maxLength = typeTableOffset - absolute;
        return std::string( pString, strnlen( pString, maxLength ) );
    };

    uint32_t remainingFields = 0;
    size_t currentComposite = 0;
    for ( uint32_t offset = typeTableOffset; offset + 8 <= dataStart;
          offset += 8 ) {
        uint32_t typeWord = readFileU32( offset );
        uint32_t nameOffset = readFileU32( offset + 4 );
        if ( remainingFields > 0 && !bBigEndian ) {
            std::swap( typeWord, nameOffset );
        }

        if ( typeWord == kXmbPaddingRecord ||
             stringTableOffset + nameOffset >= typeTableOffset ) {
            break;
        }

        if ( remainingFields > 0 ) {
            XmbType::Field field;
            field.Name = getName( nameOffset );
            field.TypeIndex = ( typeWord & 0x7fffffff ) / 2;
            field.bArray = ( typeWord & 0x80000000 ) != 0;
            types[currentComposite].Fields.push_back( field );
            remainingFields--;

            // Field records occupy a slot in the type table too (type
            // references are record indices).
            XmbType fieldRecord;
            fieldRecord.Name = field.Name;
            types.push_back( fieldRecord );
            continue;
        }

        const uint16_t hi = static_cast<uint16_t>( typeWord >> 16 );
        const uint16_t lo = static_cast<uint16_t>( typeWord & 0xffff );

        XmbType type;
        type.Name = getName( nameOffset );
        const uint32_t numFields = ( lo >> 4 ) & 0x7ff;
        if ( ( lo & 0xf ) == 0 && numFields > 0 ) {
            type.bComposite = true;
            type.Size = hi * 2u + ( ( lo & 0x8000 ) ? 1u : 0u );
            remainingFields = numFields;
        } else {
            type.bComposite = false;
            type.Primitive = static_cast<eXmbPrimitive>( lo & 0x7fff );
            type.Size = GetPrimitiveSize( type.Primitive );
        }
        types.push_back( type );
        currentComposite = types.size() - 1;
    }

    // Resolve field offsets (now that every type size is known).
    for ( XmbType &type : types ) {
        if ( !type.bComposite ) {
            continue;
        }
        uint32_t fieldOffset = 0;
        for ( XmbType::Field &field : type.Fields ) {
            field.Offset = fieldOffset;
            if ( field.bArray ) {
                fieldOffset += 8;
            } else if ( field.TypeIndex < types.size() ) {
                fieldOffset += types[field.TypeIndex].Size;
            }
        }
    }

    if ( rootType >= types.size() ) {
        OTDU_LOG_WARN( "Invalid XMB root type\n" );
        return false;
    }

    return true;
}

int32_t XmbDocument::findType( const char *pName ) const {
    for ( size_t i = 0; i < types.size(); i++ ) {
        if ( types[i].Name == pName && types[i].bComposite ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

const uint8_t *XmbDocument::getDataPointer( uint32_t offset ) const {
    size_t absolute = static_cast<size_t>( dataStart ) + offset;
    if ( absolute >= data.size() ) {
        return nullptr;
    }
    return data.data() + absolute;
}

uint8_t XmbDocument::readU8( uint32_t offset ) const {
    const uint8_t *p = getDataPointer( offset );
    return p != nullptr ? *p : 0;
}

uint16_t XmbDocument::readU16( uint32_t offset ) const {
    const uint8_t *p = getDataPointer( offset );
    if ( p == nullptr || dataStart + offset + 2 > data.size() ) {
        return 0;
    }
    uint16_t v;
    memcpy( &v, p, sizeof( v ) );
    return bBigEndian ? ByteSwap16( v ) : v;
}

uint32_t XmbDocument::readU32( uint32_t offset ) const {
    const uint8_t *p = getDataPointer( offset );
    if ( p == nullptr || dataStart + offset + 4 > data.size() ) {
        return 0;
    }
    uint32_t v;
    memcpy( &v, p, sizeof( v ) );
    return bBigEndian ? ByteSwap32( v ) : v;
}

uint64_t XmbDocument::readU64( uint32_t offset ) const {
    const uint8_t *p = getDataPointer( offset );
    if ( p == nullptr || dataStart + offset + 8 > data.size() ) {
        return 0;
    }
    uint64_t v;
    memcpy( &v, p, sizeof( v ) );
    return bBigEndian ? ByteSwap64( v ) : v;
}

const char *XmbDocument::readString( uint32_t offset ) const {
    uint32_t stringOffset = readU32( offset );
    const uint8_t *p = getDataPointer( stringOffset );
    return p != nullptr ? reinterpret_cast<const char *>( p ) : "";
}

const XmbType *XmbValue::getType() const {
    if ( pDoc == nullptr || typeIndex >= pDoc->getTypes().size() ) {
        return nullptr;
    }
    return &pDoc->getType( typeIndex );
}

XmbValue XmbValue::operator[]( const char *pFieldName ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || !pType->bComposite ) {
        return XmbValue();
    }
    const XmbType::Field *pField = pType->findField( pFieldName );
    if ( pField == nullptr || pField->bArray ) {
        return XmbValue();
    }
    return XmbValue( pDoc, pField->TypeIndex, dataOffset + pField->Offset );
}

uint32_t XmbValue::getArraySize( const char *pFieldName ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || !pType->bComposite ) {
        return 0;
    }
    const XmbType::Field *pField = pType->findField( pFieldName );
    if ( pField == nullptr || !pField->bArray ) {
        return 0;
    }
    return pDoc->readU32( dataOffset + pField->Offset );
}

XmbValue XmbValue::getArrayElement( const char *pFieldName,
                                    uint32_t index ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || !pType->bComposite ) {
        return XmbValue();
    }
    const XmbType::Field *pField = pType->findField( pFieldName );
    if ( pField == nullptr || !pField->bArray ) {
        return XmbValue();
    }
    uint32_t count = pDoc->readU32( dataOffset + pField->Offset );
    uint32_t arrayOffset = pDoc->readU32( dataOffset + pField->Offset + 4 );
    if ( index >= count ) {
        return XmbValue();
    }
    uint32_t elementSize = pDoc->getType( pField->TypeIndex ).Size;
    return XmbValue( pDoc, pField->TypeIndex,
                     arrayOffset + index * elementSize );
}

const uint8_t *XmbValue::getArrayData( const char *pFieldName,
                                       uint32_t *pOutCount ) const {
    const XmbType *pType = getType();
    *pOutCount = 0;
    if ( pType == nullptr || !pType->bComposite ) {
        return nullptr;
    }
    const XmbType::Field *pField = pType->findField( pFieldName );
    if ( pField == nullptr || !pField->bArray ) {
        return nullptr;
    }
    *pOutCount = pDoc->readU32( dataOffset + pField->Offset );
    uint32_t arrayOffset = pDoc->readU32( dataOffset + pField->Offset + 4 );
    return pDoc->getDataPointer( arrayOffset );
}

int64_t XmbValue::asInt( int64_t defaultValue ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || pType->bComposite ) {
        return defaultValue;
    }
    switch ( pType->Primitive ) {
    case eXmbPrimitive::XP_Bool:
    case eXmbPrimitive::XP_UInt8:
        return pDoc->readU8( dataOffset );
    case eXmbPrimitive::XP_Int8:
        return static_cast<int8_t>( pDoc->readU8( dataOffset ) );
    case eXmbPrimitive::XP_UInt16:
        return pDoc->readU16( dataOffset );
    case eXmbPrimitive::XP_Int16:
        return static_cast<int16_t>( pDoc->readU16( dataOffset ) );
    case eXmbPrimitive::XP_UInt32:
        return pDoc->readU32( dataOffset );
    case eXmbPrimitive::XP_Int32:
        return static_cast<int32_t>( pDoc->readU32( dataOffset ) );
    case eXmbPrimitive::XP_UInt64:
    case eXmbPrimitive::XP_Int64:
        return static_cast<int64_t>( pDoc->readU64( dataOffset ) );
    case eXmbPrimitive::XP_Float:
    case eXmbPrimitive::XP_Double:
        return static_cast<int64_t>( asDouble() );
    case eXmbPrimitive::XP_String:
        return atoll( pDoc->readString( dataOffset ) );
    default:
        return defaultValue;
    }
}

uint64_t XmbValue::asUInt64( uint64_t defaultValue ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || pType->bComposite ) {
        return defaultValue;
    }
    if ( pType->Primitive == eXmbPrimitive::XP_UInt64 ||
         pType->Primitive == eXmbPrimitive::XP_Int64 ) {
        return pDoc->readU64( dataOffset );
    }
    return static_cast<uint64_t>( asInt( static_cast<int64_t>( defaultValue ) ) );
}

double XmbValue::asDouble( double defaultValue ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || pType->bComposite ) {
        return defaultValue;
    }
    if ( pType->Primitive == eXmbPrimitive::XP_Float ) {
        uint32_t raw = pDoc->readU32( dataOffset );
        float value;
        memcpy( &value, &raw, sizeof( float ) );
        return value;
    }
    if ( pType->Primitive == eXmbPrimitive::XP_Double ) {
        uint64_t raw = pDoc->readU64( dataOffset );
        double value;
        memcpy( &value, &raw, sizeof( double ) );
        return value;
    }
    if ( pType->Primitive == eXmbPrimitive::XP_String ) {
        return atof( pDoc->readString( dataOffset ) );
    }
    return static_cast<double>( asInt( static_cast<int64_t>( defaultValue ) ) );
}

std::string XmbValue::asString( const char *pDefault ) const {
    const XmbType *pType = getType();
    if ( pType == nullptr || pType->bComposite ) {
        return pDefault;
    }
    if ( pType->Primitive == eXmbPrimitive::XP_String ) {
        return pDoc->readString( dataOffset );
    }
    if ( pType->Primitive == eXmbPrimitive::XP_Float ||
         pType->Primitive == eXmbPrimitive::XP_Double ) {
        return std::to_string( asDouble() );
    }
    return std::to_string( asInt() );
}

bool LoadXmbFromFile( const char *pPath, XmbDocument &outDocument ) {
    std::vector<uint8_t> buffer;
    if ( gpFile == nullptr || !gpFile->loadFile( pPath, buffer ) ) {
        return false;
    }

    if ( buffer.size() >= 4 && memcmp( buffer.data(), "XMBF", 4 ) != 0 ) {
        std::vector<uint8_t> decrypted;
        if ( !DecryptCPR( buffer.data(), buffer.size(), decrypted ) ) {
            return false;
        }
        buffer.swap( decrypted );
    }

    return outDocument.load( buffer );
}
