#include "shared.h"
#include "bank.h"

#include "filesystem/gs_file.h"

namespace {
constexpr uint32_t kBankHeaderOffset = 8;

// Converts the internal path layout ("dir\.ext\file") to a regular path
// ("dir\file.ext"). See FUN_0062220a in TDU1 (same algorithm in TDU2).
std::string DecodeBankPath( const std::string &encoded ) {
    std::vector<std::string> components;
    size_t start = 0;
    while ( start <= encoded.size() ) {
        size_t end = encoded.find( '\\', start );
        if ( end == std::string::npos ) {
            end = encoded.size();
        }
        components.push_back( encoded.substr( start, end - start ) );
        start = end + 1;
    }

    std::string result;
    std::string pendingExtension;
    for ( size_t i = 0; i < components.size(); i++ ) {
        const std::string &component = components[i];
        if ( !component.empty() && component[0] == '.' ) {
            pendingExtension = component;
            continue;
        }
        if ( !result.empty() ) {
            result += '\\';
        }
        result += component;
    }
    result += pendingExtension;
    return result;
}
} // namespace

Bank::Bank() {}

Bank::~Bank() { unload(); }

void Bank::unload() {
    buffer.clear();
    buffer.shrink_to_fit();
    entries.clear();
}

bool Bank::load( const char *pPath ) {
    unload();
    std::vector<uint8_t> fileBuffer;
    if ( gpFile == nullptr || !gpFile->loadFile( pPath, fileBuffer ) ) {
        OTDU_LOG_WARN( "Failed to load bank '%s'\n", pPath );
        return false;
    }
    return loadFromMemory( std::move( fileBuffer ), pPath );
}

bool Bank::loadFromMemory( std::vector<uint8_t> &&bankBuffer,
                           const char *pName ) {
    unload();
    buffer = std::move( bankBuffer );
    bankPath = pName != nullptr ? pName : "";
    if ( !parse() ) {
        OTDU_LOG_WARN( "Failed to parse bank '%s'\n", bankPath.c_str() );
        unload();
        return false;
    }
    return true;
}

uint32_t Bank::resolveIndex( const uint8_t *pIndexes,
                             uint32_t numEntries,
                             uint32_t index ) const {
    // FUN_00621b6e (TDU1)
    if ( numEntries < 0x100 ) {
        return pIndexes[index];
    } else if ( numEntries < 0x10000 ) {
        uint16_t value;
        memcpy( &value, pIndexes + index * 2, sizeof( uint16_t ) );
        return value;
    }
    uint32_t value;
    memcpy( &value, pIndexes + index * 4, sizeof( uint32_t ) );
    return value;
}

void Bank::parseDirectory( const uint8_t *pDirectory,
                           const uint8_t *pDirectoryEnd,
                           std::vector<std::string> &outPaths ) {
    // The directory is a prefix tree. Each node starts with a signed length:
    //  - negative: inner node; |length| chars (0x80 => next byte + 0x80),
    //    followed by a 7-bit varint number of children, then the name.
    //  - positive: leaf (file); length chars (0x7f => next byte + 0x7f).
    std::function<const uint8_t *( const uint8_t *, const std::string & )>
        parseNode = [&]( const uint8_t *p,
                         const std::string &prefix ) -> const uint8_t * {
        if ( p >= pDirectoryEnd ) {
            return nullptr;
        }
        int8_t length = static_cast<int8_t>( *p++ );
        if ( length < 0 ) {
            int32_t nameLength = -static_cast<int32_t>( length );
            if ( length == -0x80 ) {
                nameLength = *p++ + 0x80;
            }
            uint32_t numChildren = 0;
            uint32_t shift = 0;
            int8_t byte;
            do {
                byte = static_cast<int8_t>( *p++ );
                numChildren |= ( static_cast<uint32_t>( byte ) & 0x7f ) << shift;
                shift += 7;
            } while ( byte < 0 );
            std::string name( reinterpret_cast<const char *>( p ), nameLength );
            p += nameLength;
            std::string childPrefix = prefix + name + '\\';
            for ( uint32_t i = 0; i < numChildren && p != nullptr; i++ ) {
                p = parseNode( p, childPrefix );
            }
            return p;
        }

        int32_t nameLength = length;
        if ( length == 0x7f ) {
            nameLength = *p++ + 0x7f;
        }
        std::string name( reinterpret_cast<const char *>( p ), nameLength );
        p += nameLength;
        outPaths.push_back( prefix + name );
        return p;
    };

    const uint8_t *p = pDirectory;
    while ( p != nullptr && p < pDirectoryEnd && *p != 0 ) {
        p = parseNode( p, std::string() );
    }
}

bool Bank::parse() {
    // FUN_00621c28 (TDU1)
    if ( buffer.size() < kBankHeaderOffset + sizeof( BankHeader ) ) {
        return false;
    }

    const uint8_t *pMemory = buffer.data();
    if ( memcmp( pMemory + kBankHeaderOffset, "KNAB", 4 ) != 0 ) {
        OTDU_LOG_WARN( "Invalid bank magic (big endian banks are not "
                       "supported)\n" );
        return false;
    }

    BankHeader header;
    memcpy( &header, pMemory + kBankHeaderOffset, sizeof( BankHeader ) );
    if ( header.Version != 0 || header.NumEntries < 0 ) {
        return false;
    }

    auto getSection = [&]( int32_t offset, uint32_t *pOutSize ) -> const uint8_t * {
        if ( offset < 8 ||
             static_cast<size_t>( offset ) + 8 > buffer.size() ) {
            *pOutSize = 0;
            return nullptr;
        }
        uint32_t sectionSize;
        memcpy( &sectionSize, pMemory + offset, sizeof( uint32_t ) );
        *pOutSize = sectionSize;
        return pMemory + offset + 8;
    };

    uint32_t tableSize = 0;
    uint32_t typesSize = 0;
    uint32_t directorySize = 0;
    uint32_t indexesSize = 0;
    const uint8_t *pTable = getSection( header.SectionOffsetTable, &tableSize );
    const uint8_t *pTypes = getSection( header.SectionOffsetTypes, &typesSize );
    const uint8_t *pDirectory =
        getSection( header.SectionOffsetDirectory, &directorySize );
    const uint8_t *pIndexes =
        getSection( header.SectionOffsetIndexes, &indexesSize );

    const uint32_t numEntries = static_cast<uint32_t>( header.NumEntries );
    if ( pTable == nullptr ) {
        return false;
    }

    // Some banks use the TDU1 0x10 bytes layout; detect it from the section
    // size (NumEntries + 1 records).
    uint32_t sectionStride = sizeof( BankSection );
    if ( numEntries > 0 && tableSize / ( numEntries + 1 ) == 0x10 ) {
        sectionStride = 0x10;
    }

    entries.resize( numEntries );
    for ( uint32_t i = 0; i < numEntries; i++ ) {
        BankSection section = {};
        memcpy( &section, pTable + i * sectionStride,
                Min<uint32_t>( sectionStride, sizeof( BankSection ) ) );

        BankEntry &entry = entries[i];
        entry.Index = static_cast<int32_t>( i );
        if ( section.Offset >= 0 &&
             static_cast<size_t>( section.Offset ) + section.Size <=
                 buffer.size() ) {
            entry.pData = pMemory + section.Offset;
            entry.Size = static_cast<uint32_t>( section.Size );
        }

        if ( pTypes != nullptr ) {
            BankType type;
            memcpy( &type, pTypes + i * sizeof( BankType ), sizeof( BankType ) );
            entry.SubType = type.SubType;
            entry.Group = type.Group;
        } else {
            entry.SubType = header.Type;
            entry.Group = header.TypeUnknown;
        }
    }

    if ( pDirectory != nullptr && pIndexes != nullptr ) {
        std::vector<std::string> paths;
        const uint8_t *pDirectoryEnd =
            pDirectory + Min<size_t>( directorySize, buffer.size() -
                                                         ( pDirectory - pMemory ) );
        parseDirectory( pDirectory, pDirectoryEnd, paths );

        for ( uint32_t i = 0; i < paths.size() && i < numEntries; i++ ) {
            uint32_t entryIndex = resolveIndex( pIndexes, numEntries, i );
            if ( entryIndex >= numEntries ) {
                continue;
            }
            BankEntry &entry = entries[entryIndex];
            entry.Path = DecodeBankPath( paths[i] );

            size_t separator = entry.Path.find_last_of( '\\' );
            entry.Filename = ( separator == std::string::npos )
                                 ? entry.Path
                                 : entry.Path.substr( separator + 1 );
            size_t dot = entry.Filename.find_last_of( '.' );
            if ( dot != std::string::npos ) {
                entry.Name = entry.Filename.substr( 0, dot );
                entry.Extension = ToLower( entry.Filename.substr( dot + 1 ) );
            } else {
                entry.Name = entry.Filename;
            }
        }
    }

    return true;
}

const BankEntry *Bank::findEntry( const char *pFilename ) const {
    for ( const BankEntry &entry : entries ) {
        if ( StringCmpI( entry.Filename.c_str(), pFilename ) == 0 ) {
            return &entry;
        }
    }
    return nullptr;
}

const BankEntry *Bank::findEntry( const char *pName,
                                  const char *pExtension ) const {
    for ( const BankEntry &entry : entries ) {
        if ( StringCmpI( entry.Name.c_str(), pName ) == 0 &&
             StringCmpI( entry.Extension.c_str(), pExtension ) == 0 ) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const BankEntry *>
Bank::findEntriesByExtension( const char *pExtension ) const {
    std::vector<const BankEntry *> result;
    for ( const BankEntry &entry : entries ) {
        if ( StringCmpI( entry.Extension.c_str(), pExtension ) == 0 ) {
            result.push_back( &entry );
        }
    }
    return result;
}
