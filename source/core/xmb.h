#pragma once

// XMB ("XMBF") is the Eden binary serialization format used by TDU2 for most
// structured data: shader libraries (.sob), game database (db_*.cpr once
// decrypted), key mappings (*.xmb), camera setups, car torque curves...
//
// Layout (all values in the file endianness; .sob files are big endian, the
// other files little endian):
//   0x00 char[4]  "XMBF"
//   0x04 u8       VersionMajor (1)
//   0x05 u8       VersionMinor (1)
//   0x06 u16      Padding
//   0x08 u32      String table offset (sorted, NUL terminated names)
//   0x0C u32      Type table offset
//   0x10 u32      Data offset
//   0x14 u32      Unknown (string table related)
//   0x18 u32      Root type reference (type index * 2)
//
// Type table: array of 8 bytes records { u32 TypeWord; u32 NameOffset }.
//   TypeWord = (Hi << 16) | Lo
//   - Primitive: Lo & 0x7fff = primitive code (see eXmbPrimitive).
//   - Composite: Lo & 0xf == 0, (Lo >> 4) & 0x7ff = number of fields,
//                byte size = Hi * 2 + ((Lo & 0x8000) ? 1 : 0).
//   A composite record is followed by one record per field where TypeWord is
//   the field type reference (type index * 2), with 0x80000000 set for
//   arrays. NOTE: in little endian files the two words of field records are
//   swapped ({ NameOffset; TypeWord }).
//
// Data: packed (unaligned) memory image of the root object. Strings are u32
// offsets (relative to the data section); arrays are { u32 Count; u32 Offset }.
enum class eXmbPrimitive : uint16_t {
    XP_Object = 0x0,
    XP_Bool = 0x1,
    XP_Int8 = 0x2,
    XP_Int16 = 0x3,
    XP_Int32 = 0x4,
    XP_Int64 = 0x5,
    XP_UInt8 = 0x6,
    XP_UInt16 = 0x7,
    XP_UInt32 = 0x8,
    XP_UInt64 = 0x9,
    XP_Float = 0xa,
    XP_Double = 0xb,
    XP_String = 0xc,
    XP_Virtual = 0xd,
};

class XmbDocument;

struct XmbType {
    struct Field {
        std::string Name;
        uint32_t TypeIndex = 0;
        bool bArray = false;
        uint32_t Offset = 0; // Offset in the parent object
    };

    std::string Name;
    bool bComposite = false;
    eXmbPrimitive Primitive = eXmbPrimitive::XP_Object;
    uint32_t Size = 0;
    std::vector<Field> Fields;

    const Field *findField( const char *pName ) const;
};

// Lightweight view on a value stored in a XMB document.
class XmbValue {
public:
    XmbValue() = default;
    XmbValue( const XmbDocument *pDocument, uint32_t typeIndex, uint32_t offset )
        : pDoc( pDocument ), typeIndex( typeIndex ), dataOffset( offset ) {}

    inline bool isValid() const { return pDoc != nullptr; }
    inline uint32_t getTypeIndex() const { return typeIndex; }
    inline uint32_t getOffset() const { return dataOffset; }
    const XmbType *getType() const;

    // Composite access
    XmbValue operator[]( const char *pFieldName ) const;
    uint32_t getArraySize( const char *pFieldName ) const;
    XmbValue getArrayElement( const char *pFieldName, uint32_t index ) const;
    const uint8_t *getArrayData( const char *pFieldName,
                                 uint32_t *pOutCount ) const;

    // Primitive access (with implicit conversions between numeric types)
    int64_t asInt( int64_t defaultValue = 0 ) const;
    uint64_t asUInt64( uint64_t defaultValue = 0 ) const;
    double asDouble( double defaultValue = 0.0 ) const;
    float asFloat( float defaultValue = 0.0f ) const {
        return static_cast<float>( asDouble( defaultValue ) );
    }
    bool asBool( bool defaultValue = false ) const {
        return asInt( defaultValue ? 1 : 0 ) != 0;
    }
    std::string asString( const char *pDefault = "" ) const;

private:
    const XmbDocument *pDoc = nullptr;
    uint32_t typeIndex = 0;
    uint32_t dataOffset = 0;
};

class XmbDocument {
public:
    inline bool isBigEndian() const { return bBigEndian; }
    inline const std::vector<XmbType> &getTypes() const { return types; }
    inline const XmbType &getType( uint32_t index ) const {
        return types[index];
    }
    inline XmbValue getRoot() const {
        return XmbValue( this, rootType, 0 );
    }

public:
    // The buffer is copied (documents are small, and this allows decrypted
    // or temporary buffers to be released right away).
    bool load( const uint8_t *pBuffer, size_t size );
    bool load( const std::vector<uint8_t> &buffer ) {
        return load( buffer.data(), buffer.size() );
    }

    int32_t findType( const char *pName ) const;

    uint8_t readU8( uint32_t offset ) const;
    uint16_t readU16( uint32_t offset ) const;
    uint32_t readU32( uint32_t offset ) const;
    uint64_t readU64( uint32_t offset ) const;
    const char *readString( uint32_t offset ) const;
    const uint8_t *getDataPointer( uint32_t offset ) const;
    inline uint32_t getDataSize() const {
        return static_cast<uint32_t>( data.size() - dataStart );
    }

private:
    std::vector<uint8_t> data;
    std::vector<XmbType> types;
    uint32_t dataStart = 0;
    uint32_t rootType = 0;
    bool bBigEndian = false;

private:
    uint32_t readFileU32( uint32_t absoluteOffset ) const;
};

// Helper: decrypts (if needed) and loads a .cpr/.xmb file.
bool LoadXmbFromFile( const char *pPath, XmbDocument &outDocument );
