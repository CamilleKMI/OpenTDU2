#include "shared.h"
#include "3dd.h"

namespace {
constexpr uint32_t k3DDMagic = MakeFourCC( '.', '3', 'D', 'D' );
constexpr uint32_t kMaterialBankArrayMagic = MakeFourCC( 'M', 'B', 'N', 'A' );
constexpr uint32_t kObjectArrayMagic = MakeFourCC( 'O', 'B', 'J', 'A' );
constexpr uint32_t kInstanceArrayMagic = MakeFourCC( 'I', 'N', 'S', 'A' );
constexpr uint32_t kHierarchyArrayMagic = MakeFourCC( 'H', 'I', 'E', 'A' );
constexpr uint32_t kHashMagic = MakeFourCC( 'H', 'A', 'S', 'H' );
constexpr uint32_t kInstanceMagic = MakeFourCC( 'I', 'N', 'S', '.' );
constexpr uint32_t kHierarchyMagic = MakeFourCC( 'H', 'I', 'E', '.' );
constexpr uint32_t kMaterialBankMagic = MakeFourCC( 'M', 'B', 'N', 'K' );

template <typename T> T ReadAt( const uint8_t *pFile, uint32_t offset ) {
    T value;
    memcpy( &value, pFile + offset, sizeof( T ) );
    return value;
}

std::string HashToName( uint64_t hashcode ) {
    std::string name;
    for ( uint32_t i = 0; i < 8; i++ ) {
        char c = static_cast<char>( ( hashcode >> ( i * 8 ) ) & 0xff );
        if ( c == '\0' ) {
            break;
        }
        name += ( c >= 32 && c < 127 ) ? c : '?';
    }
    return name;
}
} // namespace

bool Render3DD::parse( const uint8_t *pFile, uint32_t fileSize, bool bMirrorX ) {
    nodes.clear();
    if ( !RenderFile::IsValid( pFile, fileSize, k3DDMagic ) ) {
        return false;
    }

    // Collect hash tables per container and the hierarchy nodes.
    std::unordered_map<uint32_t, uint64_t> instanceHashes; // INS offset -> hash
    std::unordered_map<uint32_t, uint64_t> hashEntries;    // OBJA HASH entry offset -> hash
    std::vector<uint32_t> hierarchyOffsets;

    uint32_t currentContainer = 0;
    RenderFile::VisitSections( pFile, fileSize, [&]( const RenderFile::Section &section,
                                                     uint32_t offset, uint32_t depth ) {
        if ( depth == 0 ) {
            currentContainer = section.Type;
            return true;
        }
        if ( section.Type == kHashMagic ) {
            const uint32_t numEntries = ( section.Size - sizeof( RenderFile::Section ) ) / 16;
            for ( uint32_t i = 0; i < numEntries; i++ ) {
                const uint32_t entryOffset = offset + sizeof( RenderFile::Section ) + i * 16;
                const uint64_t hashcode = ReadAt<uint64_t>( pFile, entryOffset );
                const uint32_t pointer = ReadAt<uint32_t>( pFile, entryOffset + 8 );
                if ( currentContainer == kInstanceArrayMagic ) {
                    instanceHashes[pointer] = hashcode;
                } else if ( currentContainer == kObjectArrayMagic ) {
                    hashEntries[entryOffset] = hashcode;
                }
            }
        } else if ( section.Type == kHierarchyMagic ) {
            hierarchyOffsets.push_back( offset );
        }
        return true;
    } );

    struct NodeSource {
        uint32_t HierarchyOffset;
        uint32_t InstanceOffset;
        uint32_t ParentOffset;
    };
    std::vector<NodeSource> sources;
    for ( uint32_t hierarchyOffset : hierarchyOffsets ) {
        const uint32_t payload = hierarchyOffset + sizeof( RenderFile::Section );
        sources.push_back( { hierarchyOffset, ReadAt<uint32_t>( pFile, payload + 4 ),
                             ReadAt<uint32_t>( pFile, payload + 12 ) } );
    }
    if ( sources.empty() ) {
        // Simple scenes (rims...) have no HIEA: every instance is a root node.
        for ( const auto &instance : instanceHashes ) {
            sources.push_back( { instance.first, instance.first, 0u } );
        }
        std::sort( sources.begin(), sources.end(),
                   []( const NodeSource &a, const NodeSource &b ) { return a.InstanceOffset < b.InstanceOffset; } );
    }

    std::unordered_map<uint32_t, int32_t> hierarchyIndex;
    for ( const NodeSource &source : sources ) {
        const uint32_t hierarchyOffset = source.HierarchyOffset;
        const uint32_t instanceOffset = source.InstanceOffset;
        const uint32_t parentOffset = source.ParentOffset;
        if ( instanceOffset + sizeof( RenderFile::Section ) + 0x60 > fileSize ) {
            continue;
        }

        SceneNode node;
        auto hashIt = instanceHashes.find( instanceOffset );
        node.Hashcode = hashIt != instanceHashes.end() ? hashIt->second : 0;
        node.Name = HashToName( node.Hashcode );

        const uint32_t instancePayload = instanceOffset + sizeof( RenderFile::Section );
        float matrix[16];
        memcpy( matrix, pFile + instancePayload, sizeof( matrix ) );
        for ( int c = 0; c < 4; c++ ) {
            for ( int r = 0; r < 4; r++ ) {
                node.Local.m[c][r] = matrix[c * 4 + r];
            }
        }
        if ( bMirrorX ) {
            // M' = S M S, S = diag(-1, 1, 1, 1)
            for ( int c = 0; c < 4; c++ ) {
                for ( int r = 0; r < 4; r++ ) {
                    if ( ( c == 0 ) != ( r == 0 ) ) {
                        node.Local.m[c][r] = -node.Local.m[c][r];
                    }
                }
            }
        }

        const uint32_t materialBankOffset = ReadAt<uint32_t>( pFile, instancePayload + 0x44 );
        const uint32_t objectEntryOffset = ReadAt<uint32_t>( pFile, instancePayload + 0x50 );
        const uint16_t numLods = ReadAt<uint16_t>( pFile, instancePayload + 0x4e );
        for ( uint32_t lod = 0; lod < numLods && lod < 3; lod++ ) {
            if ( instancePayload + 0x54 + lod * 0x14 + 4 <= fileSize ) {
                node.LodDistances.push_back( ReadAt<float>( pFile, instancePayload + 0x54 + lod * 0x14 ) );
                auto lodObjectIt = hashEntries.find( ReadAt<uint32_t>( pFile, instancePayload + 0x50 + lod * 0x14 ) );
                node.LodObjects.push_back( lodObjectIt != hashEntries.end() ? lodObjectIt->second : 0 );
            }
        }
        auto objectIt = hashEntries.find( objectEntryOffset );
        if ( objectIt != hashEntries.end() ) {
            node.ObjectHashcode = objectIt->second;
        }
        if ( materialBankOffset != 0 && materialBankOffset + sizeof( RenderFile::Section ) <= fileSize &&
             ReadAt<uint32_t>( pFile, materialBankOffset ) == kMaterialBankMagic ) {
            const uint32_t bankSize = ReadAt<uint32_t>( pFile, materialBankOffset + 8 );
            const uint32_t numMaterials = ( bankSize - sizeof( RenderFile::Section ) ) / 16;
            for ( uint32_t i = 0; i < numMaterials; i++ ) {
                node.Materials.push_back( ReadAt<uint64_t>(
                    pFile, materialBankOffset + sizeof( RenderFile::Section ) + i * 16 ) );
            }
        }

        auto parentIt = hierarchyIndex.find( parentOffset );
        node.Parent = ( parentOffset != 0 && parentIt != hierarchyIndex.end() ) ? parentIt->second : -1;
        node.World = node.Parent >= 0 ? nodes[node.Parent].World * node.Local : node.Local;

        hierarchyIndex[hierarchyOffset] = static_cast<int32_t>( nodes.size() );
        nodes.push_back( std::move( node ) );
    }
    return !nodes.empty();
}

int32_t Render3DD::findNode( const char *pName ) const {
    const uint64_t hashcode = GetIdentifier64bit( pName );
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        if ( nodes[i].Hashcode == hashcode ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

bool Render3DD::isDescendantOf( int32_t nodeIndex, int32_t ancestorIndex ) const {
    while ( nodeIndex >= 0 ) {
        if ( nodeIndex == ancestorIndex ) {
            return true;
        }
        nodeIndex = nodes[nodeIndex].Parent;
    }
    return false;
}
