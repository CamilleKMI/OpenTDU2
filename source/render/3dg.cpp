#include "shared.h"
#include "3dg.h"

namespace {
constexpr uint32_t k3DGMagic = MakeFourCC( '.', '3', 'D', 'G' );
constexpr uint32_t kGeometryArrayMagic = MakeFourCC( 'G', 'E', 'O', 'A' );
constexpr uint32_t kHashMagic = MakeFourCC( 'H', 'A', 'S', 'H' );
constexpr uint32_t kGeometryMagic = MakeFourCC( 'G', 'E', 'O', 'M' );
constexpr uint32_t kPrimitiveMagic = MakeFourCC( 'P', 'R', 'I', 'M' );
constexpr uint32_t kBoneMagic = MakeFourCC( 'B', 'O', 'N', 'E' );
constexpr uint32_t kVertexBufferMagic = MakeFourCC( 'D', 'X', 'V', 'B' );
constexpr uint32_t kIndexBufferMagic = MakeFourCC( 'D', 'X', 'I', 'B' );

constexpr uint32_t kNumVertexSemantics = 12;
enum eVertexSemantic : uint32_t {
    VS_Position = 0,
    VS_Normal = 1,
    VS_Diffuse = 2,
    VS_Specular = 3,
    VS_Texcoord = 4,
    VS_Tangent = 5,
    VS_Binormal = 6,
    VS_BoneIndices = 7,
    VS_BoneWeights = 8,
};

template <typename T> T ReadAt( const uint8_t *pFile, uint32_t offset ) {
    T value;
    memcpy( &value, pFile + offset, sizeof( T ) );
    return value;
}

float HalfToFloat( uint16_t h ) {
    const uint32_t sign = ( h >> 15 ) & 1;
    int32_t exponent = ( h >> 10 ) & 0x1f;
    uint32_t mantissa = h & 0x3ff;
    uint32_t bits;
    if ( exponent == 0 ) {
        if ( mantissa == 0 ) {
            bits = sign << 31;
        } else {
            exponent = 1;
            while ( !( mantissa & 0x400 ) ) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3ff;
            bits = ( sign << 31 ) | ( static_cast<uint32_t>( exponent + 112 ) << 23 ) | ( mantissa << 13 );
        }
    } else if ( exponent == 31 ) {
        bits = ( sign << 31 ) | 0x7f800000 | ( mantissa << 13 );
    } else {
        bits = ( sign << 31 ) | ( static_cast<uint32_t>( exponent + 112 ) << 23 ) | ( mantissa << 13 );
    }
    float result;
    memcpy( &result, &bits, sizeof( float ) );
    return result;
}

void DecodeVector( const uint8_t *p, eVertexAttributeFormat format, float out[4] ) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch ( format ) {
    case VAF_POSITION_R32G32B32_SFLOAT:
    case VAF_NORMAL_R32G32B32_SFLOAT:
    case VAF_TANGENT_R32G32B32_SFLOAT:
        memcpy( out, p, 12 );
        break;
    case VAF_POSITION_R32G32B32A32_SFLOAT:
        memcpy( out, p, 16 );
        break;
    case VAF_POSITION_R16G16B16A16_SFLOAT:
    case VAF_NORMAL_R16G16B16A16_SFLOAT:
    case VAF_TANGENT_R16G16B16A16_SFLOAT:
        for ( int i = 0; i < 4; i++ ) {
            out[i] = HalfToFloat( ReadAt<uint16_t>( p, i * 2 ) );
        }
        break;
    case VAF_NORMAL_R16G16B16A16_SNORM:
    case VAF_TANGENT_R16G16B16A16_SNORM:
        for ( int i = 0; i < 4; i++ ) {
            out[i] = Max( ReadAt<int16_t>( p, i * 2 ) / 32767.0f, -1.0f );
        }
        break;
    case VAF_NORMAL_R16G16B16A16_UNORM:
        for ( int i = 0; i < 4; i++ ) {
            out[i] = ReadAt<uint16_t>( p, i * 2 ) / 65535.0f * 2.0f - 1.0f;
        }
        break;
    case VAF_NORMAL_A2B10G10R10_SNORM_PACK32:
    case VAF_TANGENT_A2B10G10R10_SNORM_PACK32: {
        uint32_t packed = ReadAt<uint32_t>( p, 0 );
        for ( int i = 0; i < 3; i++ ) {
            int32_t value = static_cast<int32_t>( ( packed >> ( i * 10 ) ) & 0x3ff );
            if ( value & 0x200 ) {
                value -= 0x400;
            }
            out[i] = Max( value / 511.0f, -1.0f );
        }
        break;
    }
    case VAF_TEXCOORD_R32G32_SFLOAT:
        memcpy( out, p, 8 );
        break;
    case VAF_TEXCOORD_R16G16_SFLOAT:
        out[0] = HalfToFloat( ReadAt<uint16_t>( p, 0 ) );
        out[1] = HalfToFloat( ReadAt<uint16_t>( p, 2 ) );
        break;
    case VAF_TEXCOORD_R16G16_SNORM:
        out[0] = ReadAt<int16_t>( p, 0 ) / 32767.0f;
        out[1] = ReadAt<int16_t>( p, 2 ) / 32767.0f;
        break;
    default:
        break;
    }
}
} // namespace

bool Render3DG::parse( const uint8_t *pFile, uint32_t fileSize, bool bMirrorX ) {
    geometries.clear();
    if ( !RenderFile::IsValid( pFile, fileSize, k3DGMagic ) ) {
        return false;
    }

    std::unordered_map<uint32_t, uint64_t> geometryOffsetToHash;
    RenderFile::VisitSections( pFile, fileSize, [&]( const RenderFile::Section &section,
                                                     uint32_t offset, uint32_t depth ) {
        if ( section.Type == kHashMagic && depth == 1 ) {
            const uint32_t numEntries = ( section.Size - sizeof( RenderFile::Section ) ) / 16;
            for ( uint32_t i = 0; i < numEntries; i++ ) {
                const uint32_t entryOffset = offset + sizeof( RenderFile::Section ) + i * 16;
                geometryOffsetToHash[ReadAt<uint32_t>( pFile, entryOffset + 8 )] =
                    ReadAt<uint64_t>( pFile, entryOffset );
            }
        }
        return true;
    } );

    Geometry *pCurrentGeometry = nullptr;
    GeometryPrimitive *pLastPrimitive = nullptr;
    uint32_t lastPrimitiveEnd = 0;
    uint32_t lastPaletteSize = 0;
    RenderFile::VisitSections( pFile, fileSize, [&]( const RenderFile::Section &section,
                                                     uint32_t offset, uint32_t depth ) {
        if ( section.Type == kGeometryMagic ) {
            auto it = geometryOffsetToHash.find( offset );
            const uint64_t hashcode = it != geometryOffsetToHash.end() ? it->second
                                                                       : static_cast<uint64_t>( offset );
            Geometry &geometry = geometries[hashcode];
            geometry.Hashcode = hashcode;
            geometry.BoundsMin = Vector3f( 1e9f, 1e9f, 1e9f );
            geometry.BoundsMax = Vector3f( -1e9f, -1e9f, -1e9f );
            pCurrentGeometry = &geometry;
        } else if ( section.Type == kPrimitiveMagic && pCurrentGeometry != nullptr ) {
            GeometryPrimitive primitive;
            if ( parsePrimitive( pFile, fileSize, offset, bMirrorX, primitive ) ) {
                for ( const MeshVertex &vertex : primitive.Vertices ) {
                    pCurrentGeometry->BoundsMin.x = Min( pCurrentGeometry->BoundsMin.x, vertex.Position[0] );
                    pCurrentGeometry->BoundsMin.y = Min( pCurrentGeometry->BoundsMin.y, vertex.Position[1] );
                    pCurrentGeometry->BoundsMin.z = Min( pCurrentGeometry->BoundsMin.z, vertex.Position[2] );
                    pCurrentGeometry->BoundsMax.x = Max( pCurrentGeometry->BoundsMax.x, vertex.Position[0] );
                    pCurrentGeometry->BoundsMax.y = Max( pCurrentGeometry->BoundsMax.y, vertex.Position[1] );
                    pCurrentGeometry->BoundsMax.z = Max( pCurrentGeometry->BoundsMax.z, vertex.Position[2] );
                }
                pCurrentGeometry->Primitives.push_back( std::move( primitive ) );
                pLastPrimitive = &pCurrentGeometry->Primitives.back();
                lastPrimitiveEnd = offset + section.Size;
                lastPaletteSize = ReadAt<uint16_t>( pFile, offset + sizeof( RenderFile::Section ) + 0x12 ) >> 8;
            } else {
                pLastPrimitive = nullptr;
            }
        } else if ( section.Type == kBoneMagic && pLastPrimitive != nullptr && offset < lastPrimitiveEnd ) {
            const uint32_t count = ( section.Size - sizeof( RenderFile::Section ) ) / 4;
            for ( uint32_t i = 0; i < count && i < lastPaletteSize; i++ ) {
                pLastPrimitive->BonePalette.push_back(
                    ReadAt<uint32_t>( pFile, offset + sizeof( RenderFile::Section ) + i * 4 ) );
            }
        }
        return true;
    } );
    return !geometries.empty();
}

bool Render3DG::parsePrimitive( const uint8_t *pFile, uint32_t fileSize, uint32_t primitiveOffset,
                                bool bMirrorX, GeometryPrimitive &outPrimitive ) {
    const uint32_t payload = primitiveOffset + sizeof( RenderFile::Section );
    if ( payload + 0x40 > fileSize ) {
        return false;
    }

    const uint32_t indexBufferOffset = ReadAt<uint32_t>( pFile, payload + 0x00 );
    const uint32_t vertexBufferOffset = ReadAt<uint32_t>( pFile, payload + 0x04 );
    outPrimitive.MaterialIndex = ReadAt<uint16_t>( pFile, payload + 0x10 );
    const uint16_t primitiveType = ReadAt<uint16_t>( pFile, payload + 0x12 );

    if ( vertexBufferOffset + 0x30 > fileSize || ReadAt<uint32_t>( pFile, vertexBufferOffset ) != kVertexBufferMagic ) {
        return false;
    }
    // Heightfields (terrain) have no index buffer: the renderer provides a
    // shared grid (texcoords) and index buffer.
    const bool bHasIndices = indexBufferOffset != 0 && indexBufferOffset + 0x20 <= fileSize &&
                             ReadAt<uint32_t>( pFile, indexBufferOffset ) == kIndexBufferMagic;

    // Vertex buffer
    const uint32_t numVertex = ReadAt<uint32_t>( pFile, vertexBufferOffset + 0x10 );
    uint8_t semanticCount[kNumVertexSemantics];
    eVertexAttributeFormat semanticFormat[kNumVertexSemantics];
    for ( uint32_t i = 0; i < kNumVertexSemantics; i++ ) {
        semanticCount[i] = pFile[vertexBufferOffset + 0x14 + i * 2];
        semanticFormat[i] = static_cast<eVertexAttributeFormat>( pFile[vertexBufferOffset + 0x15 + i * 2] );
    }

    // DXVB flags (+0x2c): two vertex streams stored one after the other.
    //   0x0100: stream 0 = positions
    //   0x0200: stream 0 = positions + bone indices/weights (skinned)
    //   0x1000: stream 0 = positions + texcoords
    //   0x2000: stream 0 = positions + texcoords + bone indices/weights
    // Stream 1 holds every other attribute; attributes are interleaved inside
    // a stream, in semantic order. Skinned vertices are stored in bind pose.
    const uint32_t layoutFlags = ReadAt<uint32_t>( pFile, vertexBufferOffset + 0x2c );
    //   0x0800: stream 0 = positions + normals + texcoords (billboards)
    const bool bTexcoordsInStream0 = ( layoutFlags & 0x3800 ) != 0;
    const bool bSkinInStream0 = ( layoutFlags & 0x2200 ) != 0;
    const bool bNormalsInStream0 = ( layoutFlags & 0x0800 ) != 0;
    uint32_t streamOf[kNumVertexSemantics] = {};
    uint32_t attributeOffset[kNumVertexSemantics] = {};
    uint32_t streamStride[2] = { 0, 0 };
    for ( uint32_t i = 0; i < kNumVertexSemantics; i++ ) {
        const bool bStream0 = i == VS_Position || ( bTexcoordsInStream0 && i == VS_Texcoord ) ||
                              ( bNormalsInStream0 && i == VS_Normal ) ||
                              ( bSkinInStream0 && ( i == VS_BoneIndices || i == VS_BoneWeights ) );
        streamOf[i] = bStream0 ? 0 : 1;
        attributeOffset[i] = streamStride[streamOf[i]];
        streamStride[streamOf[i]] += semanticCount[i] * GetVertexAttributeSize( semanticFormat[i] );
    }
    const uint32_t dataOffset = vertexBufferOffset + 0x30;
    const uint32_t streamBase[2] = { dataOffset, dataOffset + numVertex * streamStride[0] };
    if ( semanticCount[VS_Position] == 0 ||
         dataOffset + numVertex * ( streamStride[0] + streamStride[1] ) > fileSize ) {
        OTDU_LOG_WARN( "Unsupported vertex layout (3DG primitive at 0x%x)\n", primitiveOffset );
        return false;
    }
    auto attributePointer = [&]( uint32_t semantic, uint32_t vertex ) {
        const uint32_t stream = streamOf[semantic];
        return pFile + streamBase[stream] + vertex * streamStride[stream] + attributeOffset[semantic];
    };

    // Keep the original streams.
    {
        static constexpr uint8_t kUsage[kNumVertexSemantics] = { 0, 3, 10, 10, 5, 6, 7, 2, 1, 0xff, 0xff, 0xff };
        static constexpr uint8_t kUsageIndex[kNumVertexSemantics] = { 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
        outPrimitive.NumRawVertex = numVertex;
        for ( uint32_t stream = 0; stream < 2; stream++ ) {
            outPrimitive.RawStrides[stream] = streamStride[stream];
            outPrimitive.RawStreams[stream].assign( pFile + streamBase[stream],
                                                    pFile + streamBase[stream] + numVertex * streamStride[stream] );
        }
        for ( uint32_t i = 0; i < kNumVertexSemantics; i++ ) {
            if ( semanticCount[i] == 0 ) {
                continue;
            }
            if ( kUsage[i] == 0xff ) {
                OTDU_LOG_DEBUG( "3DG: unknown vertex semantic %u (format %u)\n", i, semanticFormat[i] );
                continue;
            }
            const uint32_t attributeSize = GetVertexAttributeSize( semanticFormat[i] );
            for ( uint32_t k = 0; k < semanticCount[i]; k++ ) {
                RawVertexAttribute attribute;
                attribute.Usage = kUsage[i];
                attribute.UsageIndex = static_cast<uint8_t>( kUsageIndex[i] + k );
                attribute.Stream = static_cast<uint8_t>( streamOf[i] );
                attribute.Format = semanticFormat[i];
                attribute.Offset = static_cast<uint16_t>( attributeOffset[i] + k * attributeSize );
                outPrimitive.RawAttributes.push_back( attribute );
            }
        }
    }

    outPrimitive.Vertices.resize( numVertex );
    const float mirror = bMirrorX ? -1.0f : 1.0f;
    for ( uint32_t v = 0; v < numVertex; v++ ) {
        MeshVertex &vertex = outPrimitive.Vertices[v];
        float values[4];
        DecodeVector( attributePointer( VS_Position, v ), semanticFormat[VS_Position], values );
        vertex.Position[0] = values[0] * mirror;
        vertex.Position[1] = values[1];
        vertex.Position[2] = values[2];

        if ( semanticCount[VS_Normal] > 0 ) {
            DecodeVector( attributePointer( VS_Normal, v ), semanticFormat[VS_Normal], values );
            vertex.Normal[0] = values[0] * mirror;
            vertex.Normal[1] = values[1];
            vertex.Normal[2] = values[2];
        } else {
            vertex.Normal[0] = 0.0f;
            vertex.Normal[1] = 1.0f;
            vertex.Normal[2] = 0.0f;
        }

        vertex.Color = 0xffffffff;
        if ( semanticCount[VS_Diffuse] > 0 ) {
            // D3DCOLOR (BGRA in memory) -> RGBA8
            const uint8_t *pColor = attributePointer( VS_Diffuse, v );
            vertex.Color = static_cast<uint32_t>( pColor[2] ) | ( static_cast<uint32_t>( pColor[1] ) << 8 ) |
                           ( static_cast<uint32_t>( pColor[0] ) << 16 ) |
                           ( static_cast<uint32_t>( pColor[3] ) << 24 );
        }

        vertex.Texcoord[0] = vertex.Texcoord[1] = 0.0f;
        if ( semanticCount[VS_Texcoord] > 0 ) {
            DecodeVector( attributePointer( VS_Texcoord, v ), semanticFormat[VS_Texcoord], values );
            vertex.Texcoord[0] = std::isfinite( values[0] ) ? values[0] : 0.0f;
            vertex.Texcoord[1] = std::isfinite( values[1] ) ? values[1] : 0.0f;
        }
    }

    if ( semanticCount[VS_BoneIndices] > 0 ) {
        outPrimitive.BoneIndices.resize( numVertex );
        for ( uint32_t v = 0; v < numVertex; v++ ) {
            outPrimitive.BoneIndices[v] = *attributePointer( VS_BoneIndices, v );
        }
    }

    if ( !bHasIndices ) {
        outPrimitive.PrimitiveType = primitiveType;
        return true;
    }

    // Index buffer
    const uint32_t numIndex = ReadAt<uint32_t>( pFile, indexBufferOffset + 0x10 );
    const uint32_t indexDataOffset = indexBufferOffset + 0x20;
    if ( indexDataOffset + numIndex * 2 > fileSize ) {
        return false;
    }
    std::vector<uint32_t> rawIndices( numIndex );
    for ( uint32_t i = 0; i < numIndex; i++ ) {
        rawIndices[i] = ReadAt<uint16_t>( pFile, indexDataOffset + i * 2 );
    }

    // Convert to a triangle list (mirroring flips the winding).
    // Low byte: primitive type, high byte: skinning palette size.
    if ( ( primitiveType & 0xff ) == static_cast<uint16_t>( ePrimitiveType::PT_TriangleStrip ) ) {
        outPrimitive.Indices.reserve( numIndex * 3 );
        for ( uint32_t i = 2; i < numIndex; i++ ) {
            uint32_t a = rawIndices[i - 2];
            uint32_t b = rawIndices[i - 1];
            uint32_t c = rawIndices[i];
            if ( a == b || b == c || a == c ) {
                continue;
            }
            if ( i & 1 ) {
                std::swap( a, b );
            }
            if ( a >= numVertex || b >= numVertex || c >= numVertex ) {
                continue;
            }
            outPrimitive.Indices.push_back( a );
            outPrimitive.Indices.push_back( b );
            outPrimitive.Indices.push_back( c );
        }
    } else {
        for ( uint32_t i = 0; i + 2 < numIndex; i += 3 ) {
            if ( rawIndices[i] < numVertex && rawIndices[i + 1] < numVertex && rawIndices[i + 2] < numVertex ) {
                outPrimitive.Indices.push_back( rawIndices[i] );
                outPrimitive.Indices.push_back( rawIndices[i + 1] );
                outPrimitive.Indices.push_back( rawIndices[i + 2] );
            }
        }
    }
    outPrimitive.PrimitiveType = primitiveType;
    if ( bMirrorX ) {
        for ( size_t i = 0; i + 2 < outPrimitive.Indices.size(); i += 3 ) {
            std::swap( outPrimitive.Indices[i + 1], outPrimitive.Indices[i + 2] );
        }
    }
    return !outPrimitive.Indices.empty();
}

const Geometry *Render3DG::find( uint64_t hashcode ) const {
    auto it = geometries.find( hashcode );
    return it != geometries.end() ? &it->second : nullptr;
}
