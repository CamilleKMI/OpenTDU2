#include "shared.h"
#include "renderer_2d.h"

#include "render/font.h"
#include "render/shaders/builtin/ui2d_frag.h"
#include "render/shaders/builtin/ui2d_vert.h"

#include "imgui.h"

namespace {
struct PushConstants2D {
    float Projection[16];
    float Params[4];
};

void BuildOrthographic( float left, float right, float top, float bottom, float *pOut ) {
    memset( pOut, 0, sizeof( float ) * 16 );
    pOut[0] = 2.0f / ( right - left );
    pOut[5] = 2.0f / ( bottom - top );
    pOut[10] = 1.0f;
    pOut[12] = -( right + left ) / ( right - left );
    pOut[13] = -( bottom + top ) / ( bottom - top );
    pOut[15] = 1.0f;
}
} // namespace

Renderer2D::Renderer2D()
    : pDevice( nullptr ),
      pWhite( nullptr ),
      pipeline( VK_NULL_HANDLE ),
      pCurrentTexture( nullptr ),
      bCurrentAlphaOnly( false ),
      currentSaturation( 1.0f ),
      canvasWidth( 1280.0f ),
      canvasHeight( 720.0f ),
      scale( 1.0f ),
      scaleY( 1.0f ),
      offsetX( 0.0f ),
      offsetY( 0.0f ) {}

Renderer2D::~Renderer2D() {}

bool Renderer2D::initialize( RenderDevice *pRenderDevice, GPUTexture *pWhiteTexture ) {
    pDevice = pRenderDevice;
    pWhite = pWhiteTexture;

    PipelineDesc desc;
    desc.pVertexSPIRV = kSPIRV_ui2d_vert;
    desc.VertexSPIRVSize = sizeof( kSPIRV_ui2d_vert );
    desc.pFragmentSPIRV = kSPIRV_ui2d_frag;
    desc.FragmentSPIRVSize = sizeof( kSPIRV_ui2d_frag );
    desc.Attributes = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( Vertex2D, X ) },
                        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( Vertex2D, U ) },
                        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( Vertex2D, Color ) } };
    desc.BindingStrides = { sizeof( Vertex2D ) };
    desc.Blend = eBlendMode::BM_Alpha;
    desc.Cull = eCullMode::CM_None;
    pipeline = pDevice->createPipeline( desc );
    stencilPipelines[0] = pipeline;
    for ( uint32_t mode = 1; mode < static_cast<uint32_t>( eStencilMode::SM_Count ); mode++ ) {
        desc.Stencil = static_cast<eStencilMode>( mode );
        stencilPipelines[mode] = pDevice->createPipeline( desc );
    }
    vertices.reserve( 4096 );
    // Prefer the original shaders when the device supports them (the GLSL
    // pipeline stays as fallback and for effects TD_Flash does not cover).
    flashPipeline.initialize( pDevice );
    return pipeline != VK_NULL_HANDLE;
}

void Renderer2D::destroy() {
    if ( pDevice != nullptr ) {
        flashPipeline.destroy();
        for ( uint32_t mode = 1; mode < static_cast<uint32_t>( eStencilMode::SM_Count ); mode++ ) {
            pDevice->destroyPipeline( stencilPipelines[mode] );
            stencilPipelines[mode] = VK_NULL_HANDLE;
        }
        pDevice->destroyPipeline( pipeline );
    }
    pipeline = VK_NULL_HANDLE;
}

void Renderer2D::begin( float virtualWidth, float virtualHeight, bool bCover ) {
    canvasWidth = virtualWidth;
    canvasHeight = virtualHeight;
    const float backbufferWidth = static_cast<float>( pDevice->getBackbufferWidth() );
    const float backbufferHeight = static_cast<float>( pDevice->getBackbufferHeight() );
    scale = bCover ? Max( backbufferWidth / canvasWidth, backbufferHeight / canvasHeight )
                   : Min( backbufferWidth / canvasWidth, backbufferHeight / canvasHeight );
    offsetX = ( backbufferWidth - canvasWidth * scale ) * 0.5f;
    offsetY = ( backbufferHeight - canvasHeight * scale ) * 0.5f;
    scaleY = scale;
    vertices.clear();
    pCurrentTexture = nullptr;
    currentSaturation = 1.0f;
}

void Renderer2D::beginRect( float virtualWidth, float virtualHeight, float x0, float y0, float width,
                            float height ) {
    canvasWidth = virtualWidth;
    canvasHeight = virtualHeight;
    const float backbufferWidth = static_cast<float>( pDevice->getBackbufferWidth() );
    const float backbufferHeight = static_cast<float>( pDevice->getBackbufferHeight() );
    scale = backbufferWidth * width / canvasWidth;
    scaleY = backbufferHeight * height / canvasHeight;
    offsetX = x0 * backbufferWidth;
    offsetY = y0 * backbufferHeight;
    vertices.clear();
    pCurrentTexture = nullptr;
    currentSaturation = 1.0f;
}

void Renderer2D::end() {
    flush();
    currentStencil = eStencilMode::SM_None;
    currentStencilReference = 0;
}

void Renderer2D::setStencil( eStencilMode mode, uint32_t reference ) {
    if ( mode != currentStencil || reference != currentStencilReference ) {
        flush();
        currentStencil = mode;
        currentStencilReference = reference;
    }
}

void Renderer2D::screenToCanvas( float screenX, float screenY, float &outX, float &outY ) const {
    outX = ( screenX - offsetX ) / scale;
    outY = ( screenY - offsetY ) / scaleY;
}

void Renderer2D::setTexture( GPUTexture *pTexture, bool bAlphaOnly ) {
    if ( pTexture == nullptr ) {
        pTexture = pWhite;
    }
    if ( pTexture != pCurrentTexture || bAlphaOnly != bCurrentAlphaOnly ) {
        flush();
        pCurrentTexture = pTexture;
        bCurrentAlphaOnly = bAlphaOnly;
    }
}

void Renderer2D::pushQuad( float x0, float y0, float x1, float y1,
                           float u0, float v0, float u1, float v1,
                           uint32_t colorTop, uint32_t colorBottom ) {
    // Two triangles (no index buffer)
    vertices.push_back( { x0, y0, u0, v0, colorTop } );
    vertices.push_back( { x1, y0, u1, v0, colorTop } );
    vertices.push_back( { x1, y1, u1, v1, colorBottom } );
    vertices.push_back( { x0, y0, u0, v0, colorTop } );
    vertices.push_back( { x1, y1, u1, v1, colorBottom } );
    vertices.push_back( { x0, y1, u0, v1, colorBottom } );
}

void Renderer2D::drawQuad( GPUTexture *pTexture,
                           const Rect2D &destination,
                           const Rect2D &uv,
                           uint32_t color ) {
    setTexture( pTexture, false );
    pushQuad( destination.X, destination.Y, destination.X + destination.Width,
              destination.Y + destination.Height, uv.X, uv.Y, uv.X + uv.Width,
              uv.Y + uv.Height, color, color );
}

void Renderer2D::drawTriangles( GPUTexture *pTexture, const Vertex2D *pVertices, uint32_t numVertices,
                                bool bFontAtlas ) {
    if ( numVertices == 0 ) {
        return;
    }
    setTexture( pTexture, bFontAtlas );
    vertices.insert( vertices.end(), pVertices, pVertices + numVertices );
}

void Renderer2D::drawRect( const Rect2D &destination, uint32_t color ) {
    drawQuad( pWhite, destination, color );
}

void Renderer2D::drawGradient( const Rect2D &destination, uint32_t colorTop, uint32_t colorBottom ) {
    setTexture( pWhite, false );
    pushQuad( destination.X, destination.Y, destination.X + destination.Width,
              destination.Y + destination.Height, 0, 0, 1, 1, colorTop, colorBottom );
}

void Renderer2D::drawRectOutline( const Rect2D &r, float thickness, uint32_t color ) {
    drawRect( Rect2D( r.X, r.Y, r.Width, thickness ), color );
    drawRect( Rect2D( r.X, r.Y + r.Height - thickness, r.Width, thickness ), color );
    drawRect( Rect2D( r.X, r.Y, thickness, r.Height ), color );
    drawRect( Rect2D( r.X + r.Width - thickness, r.Y, thickness, r.Height ), color );
}

void Renderer2D::drawText( const Font *pFont,
                           const char *pText,
                           float x,
                           float y,
                           float size,
                           uint32_t color,
                           eTextAlign align,
                           float letterSpacing ) {
    if ( pFont == nullptr || pText == nullptr ) {
        return;
    }
    setTexture( pFont->getTexture(), pFont->isAlphaOnly() );

    // y is the top of the line box.
    const char *pLineStart = pText;
    float lineY = y + pFont->getAscent() * size;
    while ( *pLineStart != '\0' ) {
        const char *pLineEnd = strchr( pLineStart, '\n' );
        std::string line = pLineEnd != nullptr ? std::string( pLineStart, pLineEnd )
                                               : std::string( pLineStart );
        float penX = x;
        if ( align != eTextAlign::TA_Left ) {
            float width = pFont->measureText( line.c_str(), size, letterSpacing );
            penX -= ( align == eTextAlign::TA_Center ) ? width * 0.5f : width;
        }

        const char *p = line.c_str();
        while ( *p != '\0' ) {
            uint32_t codepoint = Font::DecodeUTF8( p );
            const Font::Glyph *pGlyph = pFont->findGlyph( codepoint );
            if ( pGlyph == nullptr ) {
                penX += 0.26f * size + letterSpacing;
                continue;
            }
            pushQuad( penX + pGlyph->X0 * size, lineY + pGlyph->Y0 * size,
                      penX + pGlyph->X1 * size, lineY + pGlyph->Y1 * size, pGlyph->U0,
                      pGlyph->V0, pGlyph->U1, pGlyph->V1, color, color );
            penX += pGlyph->Advance * size + letterSpacing;
        }

        if ( pLineEnd == nullptr ) {
            break;
        }
        pLineStart = pLineEnd + 1;
        lineY += pFont->getLineHeight() * size;
    }
}

void Renderer2D::recordDraw( const Vertex2D *pVertices, uint32_t numVertices,
                             const uint16_t *pIndices, uint32_t numIndices,
                             GPUTexture *pTexture, bool bAlphaOnly,
                             const float *pProjection, const VkRect2D &scissor ) {
    if ( numVertices == 0 ) {
        return;
    }
    VkCommandBuffer commandBuffer = pDevice->getCommandBuffer();

    TransientAllocation vertexAllocation =
        pDevice->allocateTransient( numVertices * sizeof( Vertex2D ), 16 );
    if ( vertexAllocation.pData == nullptr ) {
        return;
    }
    memcpy( vertexAllocation.pData, pVertices, numVertices * sizeof( Vertex2D ) );

    TransientAllocation indexAllocation;
    if ( pIndices != nullptr ) {
        indexAllocation = pDevice->allocateTransient( numIndices * sizeof( uint16_t ), 16 );
        if ( indexAllocation.pData == nullptr ) {
            return;
        }
        memcpy( indexAllocation.pData, pIndices, numIndices * sizeof( uint16_t ) );
    }

    auto issueDraw = [&]() {
        vkCmdSetScissor( commandBuffer, 0, 1, &scissor );
        vkCmdBindVertexBuffers( commandBuffer, 0, 1, &vertexAllocation.Buffer, &vertexAllocation.Offset );
        if ( pIndices != nullptr ) {
            vkCmdBindIndexBuffer( commandBuffer, indexAllocation.Buffer, indexAllocation.Offset,
                                  VK_INDEX_TYPE_UINT16 );
            vkCmdDrawIndexed( commandBuffer, numIndices, 1, 0, 0, 0 );
        } else {
            vkCmdDraw( commandBuffer, numVertices, 1, 0, 0 );
        }
    };

    if ( flashPipeline.isReady() && currentSaturation >= 0.999f &&
         flashPipeline.bind( commandBuffer, pTexture, pProjection, currentStencil, currentStencilReference, bAlphaOnly ) ) {
        issueDraw();
        return;
    }

    VkSampler sampler = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Clamp );
    VkDescriptorSet set = pDevice->allocateTextureSet( &pTexture, 1, sampler );
    if ( set == VK_NULL_HANDLE ) {
        return;
    }

    PushConstants2D push;
    memcpy( push.Projection, pProjection, sizeof( push.Projection ) );
    push.Params[0] = bAlphaOnly ? 1.0f : 0.0f;
    push.Params[1] = currentSaturation;
    push.Params[2] = 0.0f;
    push.Params[3] = 0.0f;

    VkPipeline selected = stencilPipelines[static_cast<size_t>( currentStencil )];
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, selected != VK_NULL_HANDLE ? selected : pipeline );
    if ( currentStencil != eStencilMode::SM_None ) {
        vkCmdSetStencilReference( commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentStencilReference );
    }
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pDevice->getDefaultPipelineLayout(), 0, 1, &set, 0, nullptr );
    vkCmdPushConstants( commandBuffer, pDevice->getDefaultPipelineLayout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof( push ), &push );
    issueDraw();
}

void Renderer2D::flush() {
    if ( vertices.empty() || pCurrentTexture == nullptr ) {
        vertices.clear();
        return;
    }

    float projection[16];
    const float backbufferWidth = static_cast<float>( pDevice->getBackbufferWidth() );
    const float backbufferHeight = static_cast<float>( pDevice->getBackbufferHeight() );
    // Canvas -> backbuffer pixels -> clip space
    const float left = -offsetX / scale;
    const float top = -offsetY / scaleY;
    BuildOrthographic( left, left + backbufferWidth / scale, top,
                       top + backbufferHeight / scaleY, projection );

    VkRect2D scissor = { { 0, 0 },
                         { pDevice->getBackbufferWidth(), pDevice->getBackbufferHeight() } };
    recordDraw( vertices.data(), static_cast<uint32_t>( vertices.size() ), nullptr, 0,
                pCurrentTexture, bCurrentAlphaOnly, projection, scissor );
    vertices.clear();
}

void Renderer2D::drawImGui( ImDrawData *pDrawData, GPUTexture *pFontTexture ) {
    flush();
    if ( pDrawData == nullptr || pDrawData->CmdListsCount == 0 ) {
        return;
    }

    float projection[16];
    const ImVec2 displayPos = pDrawData->DisplayPos;
    const ImVec2 displaySize = pDrawData->DisplaySize;
    BuildOrthographic( displayPos.x, displayPos.x + displaySize.x, displayPos.y,
                       displayPos.y + displaySize.y, projection );
    const ImVec2 clipScale = pDrawData->FramebufferScale;

    const float savedSaturation = currentSaturation;
    currentSaturation = 1.0f;
    static_assert( sizeof( ImDrawVert ) == sizeof( Vertex2D ), "ImDrawVert layout mismatch" );
    static_assert( sizeof( ImDrawIdx ) == sizeof( uint16_t ), "ImDrawIdx must be 16 bits" );

    for ( int listIndex = 0; listIndex < pDrawData->CmdListsCount; listIndex++ ) {
        const ImDrawList *pList = pDrawData->CmdLists[listIndex];
        for ( const ImDrawCmd &command : pList->CmdBuffer ) {
            if ( command.UserCallback != nullptr ) {
                continue;
            }
            float clipMinX = ( command.ClipRect.x - displayPos.x ) * clipScale.x;
            float clipMinY = ( command.ClipRect.y - displayPos.y ) * clipScale.y;
            float clipMaxX = ( command.ClipRect.z - displayPos.x ) * clipScale.x;
            float clipMaxY = ( command.ClipRect.w - displayPos.y ) * clipScale.y;
            clipMinX = Max( clipMinX, 0.0f );
            clipMinY = Max( clipMinY, 0.0f );
            clipMaxX = Min( clipMaxX, static_cast<float>( pDevice->getBackbufferWidth() ) );
            clipMaxY = Min( clipMaxY, static_cast<float>( pDevice->getBackbufferHeight() ) );
            if ( clipMaxX <= clipMinX || clipMaxY <= clipMinY ) {
                continue;
            }
            VkRect2D scissor;
            scissor.offset = { static_cast<int32_t>( clipMinX ), static_cast<int32_t>( clipMinY ) };
            scissor.extent = { static_cast<uint32_t>( clipMaxX - clipMinX ),
                               static_cast<uint32_t>( clipMaxY - clipMinY ) };

            GPUTexture *pTexture = reinterpret_cast<GPUTexture *>( command.GetTexID() );
            if ( pTexture == nullptr ) {
                pTexture = pFontTexture != nullptr ? pFontTexture : pWhite;
            }

            recordDraw( reinterpret_cast<const Vertex2D *>( pList->VtxBuffer.Data + command.VtxOffset ),
                        static_cast<uint32_t>( pList->VtxBuffer.Size - command.VtxOffset ),
                        pList->IdxBuffer.Data + command.IdxOffset, command.ElemCount, pTexture,
                        false, projection, scissor );
        }
    }
    currentSaturation = savedSaturation;

    VkRect2D fullScissor = { { 0, 0 },
                             { pDevice->getBackbufferWidth(), pDevice->getBackbufferHeight() } };
    vkCmdSetScissor( pDevice->getCommandBuffer(), 0, 1, &fullScissor );
}
