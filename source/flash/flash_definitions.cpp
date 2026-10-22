#include "shared.h"
#include "flash_definitions.h"

#include "flash/flash_movie.h"
#include "render/gs_render.h"

#include <mapbox/earcut.hpp>

namespace {
template <typename T> T Read( const std::vector<uint8_t> &data, uint32_t offset ) {
    T value{};
    if ( offset + sizeof( T ) <= data.size() ) {
        memcpy( &value, data.data() + offset, sizeof( T ) );
    }
    return value;
}

std::string ReadString( const std::vector<uint8_t> &data, uint32_t &offset, uint32_t end ) {
    std::string result;
    while ( offset < end && data[offset] != 0 ) {
        result += static_cast<char>( data[offset++] );
    }
    offset++; // terminator
    return result;
}

using Point = std::array<float, 2>;
using Ring = std::vector<Point>;

bool PointInRing( const Ring &ring, float x, float y ) {
    bool bInside = false;
    for ( size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++ ) {
        const Point &a = ring[i];
        const Point &b = ring[j];
        if ( ( ( a[1] > y ) != ( b[1] > y ) ) &&
             ( x < ( b[0] - a[0] ) * ( y - a[1] ) / ( b[1] - a[1] + 1e-12f ) + a[0] ) ) {
            bInside = !bInside;
        }
    }
    return bInside;
}

float RingArea( const Ring &ring ) {
    float area = 0.0f;
    for ( size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++ ) {
        area += ( ring[j][0] - ring[i][0] ) * ( ring[j][1] + ring[i][1] );
    }
    return std::fabs( area ) * 0.5f;
}

// Joins the edge polylines of one fill style into closed rings.
std::vector<Ring> BuildRings( std::vector<Ring> polylines ) {
    std::vector<Ring> rings;
    auto close = []( const Point &a, const Point &b ) {
        return std::fabs( a[0] - b[0] ) < 1.5f && std::fabs( a[1] - b[1] ) < 1.5f;
    };
    while ( !polylines.empty() ) {
        Ring ring = std::move( polylines.back() );
        polylines.pop_back();
        bool bExtended = true;
        while ( !close( ring.front(), ring.back() ) && bExtended ) {
            bExtended = false;
            for ( size_t i = 0; i < polylines.size(); i++ ) {
                Ring &other = polylines[i];
                if ( close( ring.back(), other.front() ) ) {
                    ring.insert( ring.end(), other.begin() + 1, other.end() );
                } else if ( close( ring.back(), other.back() ) ) {
                    ring.insert( ring.end(), other.rbegin() + 1, other.rend() );
                } else {
                    continue;
                }
                polylines.erase( polylines.begin() + i );
                bExtended = true;
                break;
            }
        }
        if ( ring.size() > 1 && close( ring.front(), ring.back() ) ) {
            ring.pop_back();
        }
        if ( ring.size() >= 3 ) {
            rings.push_back( std::move( ring ) );
        }
    }
    return rings;
}

// Even-odd triangulation of a set of rings.
void Triangulate( const std::vector<Ring> &rings, FlashShapeMesh &mesh ) {
    // Nesting depth of each ring: even = outer, odd = hole.
    std::vector<int> depth( rings.size(), 0 );
    std::vector<int> parent( rings.size(), -1 );
    for ( size_t i = 0; i < rings.size(); i++ ) {
        float bestArea = 1e30f;
        for ( size_t j = 0; j < rings.size(); j++ ) {
            if ( i == j || !PointInRing( rings[j], rings[i][0][0], rings[i][0][1] ) ) {
                continue;
            }
            depth[i]++;
            const float area = RingArea( rings[j] );
            if ( area < bestArea ) {
                bestArea = area;
                parent[i] = static_cast<int>( j );
            }
        }
    }
    for ( size_t i = 0; i < rings.size(); i++ ) {
        if ( depth[i] % 2 != 0 ) {
            continue;
        }
        std::vector<Ring> polygon;
        polygon.push_back( rings[i] );
        for ( size_t j = 0; j < rings.size(); j++ ) {
            if ( depth[j] % 2 == 1 && parent[j] == static_cast<int>( i ) ) {
                polygon.push_back( rings[j] );
            }
        }
        const std::vector<uint32_t> indices = mapbox::earcut<uint32_t>( polygon );
        const uint32_t base = static_cast<uint32_t>( mesh.Positions.size() / 2 );
        for ( const Ring &ring : polygon ) {
            for ( const Point &p : ring ) {
                mesh.Positions.push_back( p[0] );
                mesh.Positions.push_back( p[1] );
            }
        }
        for ( uint32_t index : indices ) {
            mesh.Indices.push_back( base + index );
        }
    }
}
} // namespace

eFlashCharacterType FlashDefinitions::getType( uint16_t id ) const {
    auto it = types.find( id );
    return it != types.end() ? it->second : eFlashCharacterType::CT_None;
}

const FlashShape *FlashDefinitions::getShape( uint16_t id ) const {
    auto it = shapes.find( id );
    return it != shapes.end() ? &it->second : nullptr;
}

const FlashTimeline *FlashDefinitions::getSprite( uint16_t id ) const {
    auto it = sprites.find( id );
    return it != sprites.end() ? &it->second : nullptr;
}

const FlashEditText *FlashDefinitions::getEditText( uint16_t id ) const {
    auto it = editTexts.find( id );
    return it != editTexts.end() ? &it->second : nullptr;
}

const FlashButton *FlashDefinitions::getButton( uint16_t id ) const {
    auto it = buttons.find( id );
    return it != buttons.end() ? &it->second : nullptr;
}

bool FlashDefinitions::build( const VMFFile &vmf, FlashMovie *pMovie ) {
    destroy();
    root.FrameCount = vmf.getFrameCount();
    buildTimeline( vmf, vmf.getTags(), root, pMovie );
    for ( const FlashBitmapDesc &bitmap : vmf.getBitmaps() ) {
        types[bitmap.CharacterId] = eFlashCharacterType::CT_Bitmap;
    }
    for ( const FlashFontDesc &font : vmf.getFonts() ) {
        types[font.CharacterId] = eFlashCharacterType::CT_Font;
    }
    return true;
}

void FlashDefinitions::destroy() {
    if ( gpRender != nullptr ) {
        for ( GPUTexture *pTexture : gradientTextures ) {
            gpRender->getDevice().destroyTexture( pTexture );
        }
    }
    gradientTextures.clear();
    root = FlashTimeline();
    shapes.clear();
    sprites.clear();
    editTexts.clear();
    buttons.clear();
    types.clear();
}

void FlashDefinitions::buildTimeline( const VMFFile &vmf, const std::vector<FlashTag> &tags,
                                      FlashTimeline &outTimeline, FlashMovie *pMovie ) {
    const std::vector<uint8_t> &data = vmf.getData();
    outTimeline.Frames.clear();
    outTimeline.Frames.emplace_back();
    for ( const FlashTag &tag : tags ) {
        std::vector<FlashControlTag> &frame = outTimeline.Frames.back();
        switch ( tag.Code ) {
        case FT_ShowFrame:
            outTimeline.Frames.emplace_back();
            break;
        case FT_PlaceObject2: {
            FlashControlTag control{ eFlashControlType::CT_Place };
            control.Place = parsePlace( vmf, tag );
            frame.push_back( control );
            break;
        }
        case FT_RemoveObject2: {
            FlashControlTag control{ eFlashControlType::CT_Remove };
            control.Depth = Read<uint16_t>( data, tag.Offset );
            frame.push_back( control );
            break;
        }
        case FT_RemoveObject: {
            FlashControlTag control{ eFlashControlType::CT_Remove };
            control.Depth = Read<uint16_t>( data, tag.Offset + 2 );
            frame.push_back( control );
            break;
        }
        case FT_DoAction: {
            FlashControlTag control{ eFlashControlType::CT_Action };
            control.ActionOffset = tag.Offset;
            control.ActionLength = tag.Length;
            frame.push_back( control );
            break;
        }
        case FT_FrameLabel: {
            uint32_t offset = tag.Offset;
            FlashControlTag control{ eFlashControlType::CT_Label };
            control.Label = ReadString( data, offset, tag.Offset + tag.Length );
            outTimeline.Labels[ToLower( control.Label )] = static_cast<uint32_t>( outTimeline.Frames.size() - 1 );
            frame.push_back( control );
            break;
        }
        case FT_DefineShape:
        case FT_DefineShape2:
        case FT_DefineShape3:
            parseShape( vmf, tag, pMovie );
            break;
        case FT_DefineEditText:
            parseEditText( vmf, tag );
            break;
        case FT_DefineButton2:
            parseButton( vmf, tag );
            break;
        case FT_DefineSpriteEden:
        case FT_DefineSprite: {
            const uint16_t id = Read<uint16_t>( data, tag.Offset );
            FlashTimeline &sprite = sprites[id];
            sprite.FrameCount = Read<uint16_t>( data, tag.Offset + 2 );
            // Eden sprites carry an extra u32 (payload size) before the tags.
            const uint32_t start = tag.Offset + ( tag.Code == FT_DefineSpriteEden ? 8 : 4 );
            std::vector<FlashTag> spriteTags;
            vmf.parseTags( start, tag.Offset + tag.Length, spriteTags );
            buildTimeline( vmf, spriteTags, sprite, pMovie );
            types[id] = eFlashCharacterType::CT_Sprite;
            break;
        }
        default:
            break;
        }
    }
    // Trailing empty frame after the last ShowFrame.
    if ( outTimeline.Frames.size() > 1 && outTimeline.Frames.back().empty() ) {
        outTimeline.Frames.pop_back();
    }
    if ( outTimeline.FrameCount == 0 ) {
        outTimeline.FrameCount = static_cast<uint16_t>( outTimeline.Frames.size() );
    }
    outTimeline.Frames.resize( Max<size_t>( outTimeline.FrameCount, 1 ) );
}

FlashPlaceInfo FlashDefinitions::parsePlace( const VMFFile &vmf, const FlashTag &tag ) {
    const std::vector<uint8_t> &data = vmf.getData();
    FlashPlaceInfo place;
    const uint8_t flags = data[tag.Offset];
    place.Depth = Read<uint16_t>( data, tag.Offset + 1 );
    uint32_t offset = tag.Offset + 3;
    const uint32_t end = tag.Offset + tag.Length;
    place.bMove = ( flags & 0x01 ) != 0;
    if ( flags & 0x02 ) {
        place.bHasCharacter = true;
        place.CharacterId = Read<uint16_t>( data, offset );
        offset += 2;
    }
    if ( flags & 0x04 ) {
        FlashBitReader reader( data.data(), end, offset );
        place.Matrix = reader.readMatrix();
        place.bHasMatrix = true;
        offset = reader.getOffset();
    }
    if ( flags & 0x08 ) {
        FlashBitReader reader( data.data(), end, offset );
        place.ColorTransform = reader.readColorTransform( true );
        place.bHasColorTransform = true;
        offset = reader.getOffset();
    }
    if ( flags & 0x10 ) {
        place.bHasRatio = true;
        place.Ratio = Read<uint16_t>( data, offset );
        offset += 2;
    }
    if ( flags & 0x20 ) {
        place.bHasName = true;
        place.Name = ReadString( data, offset, end );
    }
    if ( flags & 0x40 ) {
        place.ClipDepth = Read<uint16_t>( data, offset );
        offset += 2;
    }
    return place;
}

void FlashDefinitions::parseShape( const VMFFile &vmf, const FlashTag &tag, FlashMovie *pMovie ) {
    const std::vector<uint8_t> &data = vmf.getData();
    const uint32_t end = tag.Offset + tag.Length;
    const uint16_t id = Read<uint16_t>( data, tag.Offset );
    const bool bAlpha = tag.Code == FT_DefineShape3;
    FlashShape &shape = shapes[id];
    types[id] = eFlashCharacterType::CT_Shape;

    FlashBitReader reader( data.data(), end, tag.Offset + 2 );
    reader.readRect( shape.XMin, shape.XMax, shape.YMin, shape.YMax );
    uint32_t offset = reader.getOffset();

    auto readColor = [&]() {
        uint32_t color = data[offset] | ( data[offset + 1] << 8 ) | ( data[offset + 2] << 16 );
        color |= ( bAlpha ? data[offset + 3] : 0xffu ) << 24;
        offset += bAlpha ? 4 : 3;
        return color;
    };

    // Fill styles
    uint32_t numFills = data[offset++];
    if ( numFills == 0xff && tag.Code != FT_DefineShape ) {
        numFills = Read<uint16_t>( data, offset );
        offset += 2;
    }
    for ( uint32_t i = 0; i < numFills && offset < end; i++ ) {
        FlashFill fill;
        const uint8_t type = data[offset++];
        if ( type == 0x00 ) {
            fill.Type = eFlashFillType::FF_Solid;
            fill.Color = readColor();
        } else if ( type == 0x10 || type == 0x12 || type == 0x13 ) {
            fill.Type = type == 0x10 ? eFlashFillType::FF_LinearGradient : eFlashFillType::FF_RadialGradient;
            FlashBitReader matrixReader( data.data(), end, offset );
            fill.InverseMatrix = matrixReader.readMatrix().inverse();
            offset = matrixReader.getOffset();
            const uint32_t numStops = data[offset++] & 0x0f;
            for ( uint32_t s = 0; s < numStops; s++ ) {
                const uint8_t ratio = data[offset++];
                fill.GradientStops.emplace_back( ratio, readColor() );
            }
            if ( type == 0x13 ) {
                offset += 2; // focal point
            }
            fill.pTexture = createGradientTexture( fill );
        } else if ( type >= 0x40 && type <= 0x43 ) {
            fill.Type = eFlashFillType::FF_Bitmap;
            fill.BitmapId = Read<uint16_t>( data, offset );
            offset += 2;
            fill.bRepeat = ( type == 0x40 || type == 0x42 );
            FlashBitReader matrixReader( data.data(), end, offset );
            fill.InverseMatrix = matrixReader.readMatrix().inverse();
            offset = matrixReader.getOffset();
            if ( pMovie != nullptr && fill.BitmapId != 0xffff ) {
                FlashMovie::BitmapRef bitmap = pMovie->getBitmap( fill.BitmapId );
                fill.pTexture = bitmap.pTexture;
                fill.U0 = bitmap.UV.X;
                fill.V0 = bitmap.UV.Y;
                fill.U1 = bitmap.UV.X + bitmap.UV.Width;
                fill.V1 = bitmap.UV.Y + bitmap.UV.Height;
                fill.BitmapWidth = Max( bitmap.Width, 1.0f );
                fill.BitmapHeight = Max( bitmap.Height, 1.0f );
            }
            if ( fill.pTexture == nullptr ) {
                fill.Type = eFlashFillType::FF_None; // clipping helper (id 0xffff)
            }
        } else {
            OTDU_LOG_WARN( "VMF shape %u: unknown fill type 0x%x\n", id, type );
            return;
        }
        shape.Fills.push_back( fill );
    }

    // Line styles (unused: Eden shapes are fills only).
    uint32_t numLines = data[offset++];
    if ( numLines == 0xff && tag.Code != FT_DefineShape ) {
        numLines = Read<uint16_t>( data, offset );
        offset += 2;
    }
    offset += numLines * ( 2 + ( bAlpha ? 4 : 3 ) );

    // Eden pre-decoded paths:
    //   u16 numPaths; { u8 line, u16 fill0, u16 fill1, i32 moveX, moveY, u16 numEdges,
    //                   { i32 controlX, controlY, anchorX, anchorY }[numEdges], u8 pad }
    //   (the pad byte ends each path record: LOG_IN2 ch30, GENERAL ch278).
    const uint16_t numPaths = Read<uint16_t>( data, offset );
    offset += 2;
    std::map<uint32_t, std::vector<Ring>> polylinesPerFill;
    for ( uint32_t p = 0; p < numPaths && offset + 15 <= end; p++ ) {
        const uint16_t fill0 = Read<uint16_t>( data, offset + 1 );
        const uint16_t fill1 = Read<uint16_t>( data, offset + 3 );
        float x = static_cast<float>( Read<int32_t>( data, offset + 5 ) );
        float y = static_cast<float>( Read<int32_t>( data, offset + 9 ) );
        const uint16_t numEdges = Read<uint16_t>( data, offset + 13 );
        offset += 15;
        Ring polyline;
        polyline.push_back( { x, y } );
        for ( uint32_t e = 0; e < numEdges && offset + 16 <= end; e++ ) {
            const float cx = static_cast<float>( Read<int32_t>( data, offset ) );
            const float cy = static_cast<float>( Read<int32_t>( data, offset + 4 ) );
            const float ax = static_cast<float>( Read<int32_t>( data, offset + 8 ) );
            const float ay = static_cast<float>( Read<int32_t>( data, offset + 12 ) );
            offset += 16;
            const float cross = ( cx - x ) * ( ay - y ) - ( cy - y ) * ( ax - x );
            if ( std::fabs( cross ) > 4.0f ) {
                // Quadratic curve: flatten.
                const int steps = 8;
                for ( int s = 1; s <= steps; s++ ) {
                    const float t = static_cast<float>( s ) / steps;
                    const float it = 1.0f - t;
                    polyline.push_back( { it * it * x + 2.0f * it * t * cx + t * t * ax,
                                          it * it * y + 2.0f * it * t * cy + t * t * ay } );
                }
            } else {
                polyline.push_back( { ax, ay } );
            }
            x = ax;
            y = ay;
        }
        offset += 1; // pad
        if ( fill0 != 0 ) {
            polylinesPerFill[fill0].push_back( polyline );
        }
        if ( fill1 != 0 && fill1 != fill0 ) {
            polylinesPerFill[fill1].push_back( polyline );
        }
    }

    if ( offset != end ) {
        OTDU_LOG_DEBUG( "VMF shape %u: %d bytes left after the paths\n", id, static_cast<int32_t>( end ) - static_cast<int32_t>( offset ) );
    }
    for ( auto &entry : polylinesPerFill ) {
        if ( entry.first == 0 || entry.first > shape.Fills.size() ||
             shape.Fills[entry.first - 1].Type == eFlashFillType::FF_None ) {
            continue;
        }
        FlashShapeMesh mesh;
        mesh.FillIndex = entry.first - 1;
        Triangulate( BuildRings( entry.second ), mesh );
        if ( !mesh.Indices.empty() ) {
            shape.Meshes.push_back( std::move( mesh ) );
        }
    }
}

GPUTexture *FlashDefinitions::createGradientTexture( const FlashFill &fill ) {
    if ( gpRender == nullptr || fill.GradientStops.empty() ) {
        return nullptr;
    }
    auto sample = [&]( float ratio ) {
        const auto &stops = fill.GradientStops;
        if ( ratio <= stops.front().first ) {
            return stops.front().second;
        }
        for ( size_t i = 1; i < stops.size(); i++ ) {
            if ( ratio <= stops[i].first ) {
                const float t = ( ratio - stops[i - 1].first ) /
                                Max( static_cast<float>( stops[i].first - stops[i - 1].first ), 1.0f );
                uint32_t out = 0;
                for ( int c = 0; c < 4; c++ ) {
                    const float a = static_cast<float>( ( stops[i - 1].second >> ( c * 8 ) ) & 0xff );
                    const float b = static_cast<float>( ( stops[i].second >> ( c * 8 ) ) & 0xff );
                    out |= static_cast<uint32_t>( a + ( b - a ) * t + 0.5f ) << ( c * 8 );
                }
                return out;
            }
        }
        return stops.back().second;
    };

    const bool bRadial = fill.Type == eFlashFillType::FF_RadialGradient;
    const uint32_t width = bRadial ? 128 : 256;
    const uint32_t height = bRadial ? 128 : 4;
    std::vector<uint32_t> pixels( width * height );
    for ( uint32_t y = 0; y < height; y++ ) {
        for ( uint32_t x = 0; x < width; x++ ) {
            float ratio;
            if ( bRadial ) {
                const float dx = ( x + 0.5f ) / width * 2.0f - 1.0f;
                const float dy = ( y + 0.5f ) / height * 2.0f - 1.0f;
                ratio = Min( std::sqrt( dx * dx + dy * dy ), 1.0f ) * 255.0f;
            } else {
                ratio = ( x + 0.5f ) / width * 255.0f;
            }
            pixels[y * width + x] = sample( ratio );
        }
    }
    GPUTexture *pTexture = gpRender->getTextureCache().createFromRGBA(
        width, height, reinterpret_cast<const uint8_t *>( pixels.data() ), "flash_gradient" );
    if ( pTexture != nullptr ) {
        gradientTextures.push_back( pTexture );
    }
    return pTexture;
}

void FlashDefinitions::parseEditText( const VMFFile &vmf, const FlashTag &tag ) {
    const std::vector<uint8_t> &data = vmf.getData();
    const uint32_t end = tag.Offset + tag.Length;
    const uint16_t id = Read<uint16_t>( data, tag.Offset );
    FlashEditText &text = editTexts[id];
    types[id] = eFlashCharacterType::CT_EditText;

    FlashBitReader reader( data.data(), end, tag.Offset + 2 );
    reader.readRect( text.XMin, text.XMax, text.YMin, text.YMax );
    uint32_t offset = reader.getOffset();
    const uint8_t flags1 = data[offset];
    const uint8_t flags2 = data[offset + 1];
    offset += 2;
    text.bWordWrap = ( flags1 & 0x40 ) != 0;
    text.bMultiline = ( flags1 & 0x20 ) != 0;
    text.bReadOnly = ( flags1 & 0x08 ) != 0;
    text.bPassword = ( flags1 & 0x10 ) != 0;
    if ( flags1 & 0x01 ) {
        text.FontId = Read<uint16_t>( data, offset );
        text.FontHeight = static_cast<float>( Read<uint16_t>( data, offset + 2 ) );
        offset += 4;
    }
    if ( flags1 & 0x04 ) {
        text.Color = data[offset] | ( data[offset + 1] << 8 ) | ( data[offset + 2] << 16 ) |
                     ( data[offset + 3] << 24 );
        offset += 4;
    }
    if ( flags1 & 0x02 ) {
        offset += 2;
    }
    if ( flags2 & 0x20 ) {
        text.Align = data[offset];
        text.LeftMargin = Read<uint16_t>( data, offset + 1 );
        text.RightMargin = Read<uint16_t>( data, offset + 3 );
        text.Indent = Read<uint16_t>( data, offset + 5 );
        text.Leading = static_cast<float>( Read<int16_t>( data, offset + 7 ) );
        offset += 9;
    }
    text.VariableName = ReadString( data, offset, end );
    // Eden: the initial text is followed by 3 bytes and the authoring text;
    // keep the last non-empty string.
    while ( offset < end ) {
        std::string candidate = ReadString( data, offset, end );
        if ( !candidate.empty() && static_cast<unsigned char>( candidate[0] ) >= 0x20 ) {
            text.Text = candidate;
        }
    }
}

void FlashDefinitions::parseButton( const VMFFile &vmf, const FlashTag &tag ) {
    const std::vector<uint8_t> &data = vmf.getData();
    const uint32_t end = tag.Offset + tag.Length;
    const uint16_t id = Read<uint16_t>( data, tag.Offset );
    FlashButton &button = buttons[id];
    types[id] = eFlashCharacterType::CT_Button;
    const uint16_t actionOffset = Read<uint16_t>( data, tag.Offset + 3 );
    const uint32_t recordsEnd = actionOffset != 0 ? tag.Offset + 3 + actionOffset : end;
    uint32_t offset = tag.Offset + 5;
    while ( offset < recordsEnd && data[offset] != 0 ) {
        FlashButtonRecord record;
        const uint8_t flags = data[offset];
        record.States = flags & 0x0f;
        record.CharacterId = Read<uint16_t>( data, offset + 1 );
        record.Depth = Read<uint16_t>( data, offset + 3 );
        FlashBitReader recordReader( data.data(), end, offset + 5 );
        record.Matrix = recordReader.readMatrix();
        record.ColorTransform = recordReader.readColorTransform( true );
        offset = recordReader.getOffset();
        button.Records.push_back( record );
    }
    // Condition actions (DefineButton2).
    if ( actionOffset != 0 ) {
        uint32_t condition = tag.Offset + 3 + actionOffset;
        while ( condition + 4 <= end ) {
            const uint16_t conditionSize = Read<uint16_t>( data, condition );
            FlashButtonCondition entry;
            entry.Conditions = Read<uint16_t>( data, condition + 2 );
            entry.ActionOffset = condition + 4;
            const uint32_t conditionEnd = conditionSize != 0 ? condition + conditionSize : end;
            entry.ActionLength = conditionEnd > entry.ActionOffset ? conditionEnd - entry.ActionOffset : 0;
            button.Conditions.push_back( entry );
            if ( conditionSize == 0 ) {
                break;
            }
            condition += conditionSize;
        }
    }
}
