#include "shared.h"
#include "2dm.h"

namespace {
constexpr uint32_t k2DMMagic = MakeFourCC( '.', '2', 'D', 'M' );
constexpr uint32_t kMaterialMagic = MakeFourCC( 'M', 'A', 'T', '.' );
constexpr uint32_t kUVAnimationMagic = MakeFourCC( 'U', 'V', 'A', '.' );

constexpr uint32_t kRenderStatesOffset = 0x30;
constexpr uint32_t kTextureSlotsOffset = 0x100;
constexpr uint32_t kTextureSlotSize = 0x20;
constexpr uint32_t kMaxTextureSlots = 8;
constexpr uint32_t kParametersOffset = 0x340;

template <typename T> T ReadAt( const uint8_t *p, uint32_t offset ) {
    T value;
    memcpy( &value, p + offset, sizeof( T ) );
    return value;
}

bool IsParameterDesc( const uint8_t *p ) {
    const uint16_t type = ReadAt<uint16_t>( p, 8 );
    const uint32_t size = ReadAt<uint32_t>( p, 12 );
    const uint64_t name = ReadAt<uint64_t>( p, 0 );
    return name != 0 && type >= 1 && type <= 4 && size == 0x10;
}
} // namespace

uint64_t MaterialDesc::findTexture( const char *pSlotType ) const {
    const uint64_t slotHash = GetIdentifier64bit( pSlotType );
    for ( const MaterialTextureSlot &slot : Textures ) {
        if ( slot.SlotType == slotHash ) {
            return slot.TextureHashcode;
        }
    }
    return 0;
}

bool MaterialDesc::getParameter( const char *pName, Vector4f &outValue ) const {
    auto it = Parameters.find( GetIdentifier64bit( pName ) );
    if ( it == Parameters.end() ) {
        return false;
    }
    outValue = it->second;
    return true;
}

void Render2DM::parseMaterial( const uint8_t *pPayload, uint32_t payloadSize ) {
    if ( payloadSize < kParametersOffset ) {
        return;
    }
    MaterialDesc material;
    material.Hashcode = ReadAt<uint64_t>( pPayload, 0 );
    const char *pName = reinterpret_cast<const char *>( pPayload + 0x10 );
    material.Name.assign( pName, strnlen( pName, 32 ) );

    const uint8_t *pStates = pPayload + kRenderStatesOffset;
    memcpy( material.States, pStates, sizeof( material.States ) );
    material.DepthTest = pStates[0];
    material.DepthWrite = pStates[1];
    material.CullMode = pStates[3];
    material.AlphaTest = pStates[5];
    material.AlphaRef = pStates[7];
    material.SrcBlend = pStates[8];
    material.DstBlend = pStates[9];

    auto readColor = [&]( uint32_t offset ) {
        return Vector4f( ReadAt<float>( pPayload, offset ), ReadAt<float>( pPayload, offset + 4 ),
                         ReadAt<float>( pPayload, offset + 8 ), ReadAt<float>( pPayload, offset + 12 ) );
    };
    material.DiffuseColor = readColor( 0xb0 );
    material.AmbientColor = readColor( 0xc0 );
    material.SpecularColor = readColor( 0xd0 );
    material.EmissiveColor = readColor( 0xe0 );
    material.ShaderSetGUID = ReadAt<uint64_t>( pPayload, 0x300 );
    material.ShaderVersion[0] = pPayload[0x30a];
    material.ShaderVersion[1] = pPayload[0x30b];
    memcpy( material.Spec, pPayload + 0x310, sizeof( material.Spec ) );

    const uint32_t numSlots = Min<uint32_t>( pPayload[0xfc], 16u );
    for ( uint32_t i = 0; i < Max( numSlots, kMaxTextureSlots ) && i < 16; i++ ) {
        const uint8_t *pSlot = pPayload + kTextureSlotsOffset + i * kTextureSlotSize;
        MaterialTextureSlot slot;
        slot.SlotType = ReadAt<uint64_t>( pSlot, 0 );
        slot.TextureHashcode = ReadAt<uint64_t>( pSlot, 8 );
        if ( i < numSlots ) {
            material.AllTextures.push_back( slot );
        }
        if ( slot.SlotType != 0 && i < kMaxTextureSlots ) {
            material.Textures.push_back( slot );
        }
    }

    // Parameter descriptors, then their values.
    uint32_t offset = kParametersOffset;
    while ( offset >= 0x330 && !IsParameterDesc( pPayload + offset ) && offset + 16 <= payloadSize &&
            offset < kParametersOffset + 0x80 ) {
        offset += 16;
    }
    std::vector<uint64_t> parameterNames;
    while ( offset + 16 <= payloadSize && IsParameterDesc( pPayload + offset ) ) {
        parameterNames.push_back( ReadAt<uint64_t>( pPayload, offset ) );
        offset += 16;
    }
    for ( size_t i = 0; i < parameterNames.size() && offset + 16 <= payloadSize; i++ ) {
        Vector4f value( ReadAt<float>( pPayload, offset ), ReadAt<float>( pPayload, offset + 4 ),
                        ReadAt<float>( pPayload, offset + 8 ), ReadAt<float>( pPayload, offset + 12 ) );
        material.Parameters[parameterNames[i]] = value;
        offset += 16;
    }

    orderedMaterials.push_back( material );
    materials[material.Hashcode] = std::move( material );
}

bool Render2DM::parse( const uint8_t *pFile, uint32_t fileSize ) {
    materials.clear();
    orderedMaterials.clear();
    uvAnimationIds.clear();
    if ( !RenderFile::IsValid( pFile, fileSize, k2DMMagic ) ) {
        return false;
    }
    RenderFile::VisitSections( pFile, fileSize, [&]( const RenderFile::Section &section,
                                                     uint32_t offset, uint32_t ) {
        if ( section.Type == kMaterialMagic ) {
            parseMaterial( pFile + offset + sizeof( RenderFile::Section ),
                           section.Size - sizeof( RenderFile::Section ) );
        } else if ( section.Type == kUVAnimationMagic && section.Size >= sizeof( RenderFile::Section ) + 8 ) {
            // UVAA { UVA. { u64 id; u64 0 } ... }
            uint64_t id = 0;
            memcpy( &id, pFile + offset + sizeof( RenderFile::Section ), sizeof( id ) );
            uvAnimationIds.push_back( id );
        }
        return true;
    } );
    return true;
}

const MaterialDesc *Render2DM::find( uint64_t hashcode ) const {
    auto it = materials.find( hashcode );
    return it != materials.end() ? &it->second : nullptr;
}
