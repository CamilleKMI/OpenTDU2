#include "shared.h"
#include "flash_shader_pipeline.h"

#include "render/renderer_2d.h"
#include "system/cmd_line.h"

namespace {
constexpr const char *kFlashShaderLibrary = "Euro/Shaders/TD_Flash.sob";
constexpr uint32_t kVertexShaderIndex = 2; // pos + color + texcoord
constexpr uint32_t kPixelShaderIndex = 3;  // texld * color
constexpr uint32_t kFontPixelShaderIndex = 7; // alpha = texld.r * color.a, rgb = color

constexpr VkDeviceSize kVSConstantsSize = 16 * 16 + 256 * 16; // int4 i[16]; float4 c[256]
constexpr VkDeviceSize kPSConstantsSize = 16 * 16 + 224 * 16; // int4 i[16]; float4 c[224]
constexpr VkDeviceSize kZeroBufferSize = 4096;
constexpr uint32_t kSpecDwords = 16;
constexpr uint32_t kMaxSetsPerFrame = 2048;

// D3D9SpecializationInfo layout (third_party/dxvk/src/d3d9/d3d9_spec_constants.h)
constexpr uint32_t kSpecAlphaCompareOpDword = 1;
constexpr uint32_t kSpecAlphaCompareOpShift = 21;
} // namespace

FlashShaderPipeline::FlashShaderPipeline()
    : pDevice( nullptr ),
      setLayouts{ VK_NULL_HANDLE, VK_NULL_HANDLE },
      pipelineLayout( VK_NULL_HANDLE ),
      pipeline( VK_NULL_HANDLE ),
      staticPool( VK_NULL_HANDLE ),
      framePoolNumbers{},
      samplerSet( VK_NULL_HANDLE ),
      pZeroBuffer( nullptr ),
      pSpecBuffer( nullptr ),
      bReady( false ) {
    for ( VkDescriptorPool &pool : framePools ) {
        pool = VK_NULL_HANDLE;
    }
}

FlashShaderPipeline::~FlashShaderPipeline() {}

bool FlashShaderPipeline::initialize( RenderDevice *pRenderDevice ) {
    pDevice = pRenderDevice;
    if ( CmdLine::HasArg( "-nosob" ) ) {
        OTDU_LOG_INFO( "Original flash shaders disabled (-nosob)\n" );
        return false;
    }
    if ( !pDevice->supportsDxsoShaders() ) {
        return false;
    }
    if ( !library.load( kFlashShaderLibrary ) ) {
        return false;
    }
    const TranslatedShader *pVertexShader =
        library.getTranslatedShader( ShaderLibrary::SP_VertexShader, kVertexShaderIndex );
    const TranslatedShader *pPixelShader =
        library.getTranslatedShader( ShaderLibrary::SP_PixelShader, kPixelShaderIndex );
    if ( pVertexShader == nullptr || pPixelShader == nullptr ) {
        OTDU_LOG_WARN( "TD_Flash.sob: failed to translate the 2D shaders\n" );
        return false;
    }

    VkDevice device = pDevice->getDevice();

    // Set 0: DXVK D3D9 bindings
    const VkDescriptorSetLayoutBinding set0Bindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 13, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 31, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
          nullptr },
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>( std::size( set0Bindings ) );
    layoutInfo.pBindings = set0Bindings;
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &setLayouts[0] ) != VK_SUCCESS ) {
        return false;
    }
    // Set 1: sampler heap
    const VkDescriptorSetLayoutBinding samplerBinding = { 0, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
                                                          VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &setLayouts[1] ) != VK_SUCCESS ) {
        return false;
    }

    VkPushConstantRange pushRange = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                      RenderDevice::kPushConstantsSize };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if ( vkCreatePipelineLayout( device, &pipelineLayoutInfo, nullptr, &pipelineLayout ) != VK_SUCCESS ) {
        return false;
    }

    PipelineDesc desc;
    desc.pVertexSPIRV = pVertexShader->SPIRV.data();
    desc.VertexSPIRVSize = pVertexShader->SPIRV.size() * sizeof( uint32_t );
    desc.pFragmentSPIRV = pPixelShader->SPIRV.data();
    desc.FragmentSPIRVSize = pPixelShader->SPIRV.size() * sizeof( uint32_t );
    // VS inputs (dcl): position, color, texcoord (locations from the translation)
    for ( const ShaderInputInfo &input : pVertexShader->Inputs ) {
        if ( input.Usage == 0 ) { // POSITION
            desc.Attributes.push_back( { input.Location, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( Vertex2D, X ) } );
        } else if ( input.Usage == 10 ) { // COLOR
            desc.Attributes.push_back( { input.Location, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( Vertex2D, Color ) } );
        } else if ( input.Usage == 5 ) { // TEXCOORD
            desc.Attributes.push_back( { input.Location, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( Vertex2D, U ) } );
        }
    }
    desc.BindingStrides = { sizeof( Vertex2D ) };
    desc.Blend = eBlendMode::BM_Alpha;
    desc.Cull = eCullMode::CM_None;
    desc.Layout = pipelineLayout;
    pipeline = pDevice->createPipeline( desc );
    if ( pipeline == VK_NULL_HANDLE ) {
        OTDU_LOG_WARN( "TD_Flash.sob: pipeline creation failed\n" );
        return false;
    }
    stencilPipelines[0] = pipeline;
    for ( uint32_t mode = 1; mode < static_cast<uint32_t>( eStencilMode::SM_Count ); mode++ ) {
        desc.Stencil = static_cast<eStencilMode>( mode );
        stencilPipelines[mode] = pDevice->createPipeline( desc );
    }
    if ( const TranslatedShader *pFontShader =
             library.getTranslatedShader( ShaderLibrary::SP_PixelShader, kFontPixelShaderIndex ) ) {
        desc.pFragmentSPIRV = pFontShader->SPIRV.data();
        desc.FragmentSPIRVSize = pFontShader->SPIRV.size() * sizeof( uint32_t );
        for ( uint32_t mode = 0; mode < static_cast<uint32_t>( eStencilMode::SM_Count ); mode++ ) {
            desc.Stencil = static_cast<eStencilMode>( mode );
            fontPipelines[mode] = pDevice->createPipeline( desc );
        }
    }

    // Static resources
    std::vector<uint8_t> zeros( kZeroBufferSize, 0 );
    pZeroBuffer = pDevice->createBuffer( kZeroBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, zeros.data() );
    uint32_t specData[kSpecDwords] = {};
    specData[kSpecAlphaCompareOpDword] = static_cast<uint32_t>( VK_COMPARE_OP_ALWAYS ) << kSpecAlphaCompareOpShift;
    pSpecBuffer = pDevice->createBuffer( sizeof( specData ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, specData );

    const VkDescriptorPoolSize staticSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1 } };
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = staticSizes;
    vkCreateDescriptorPool( device, &poolInfo, nullptr, &staticPool );

    const VkDescriptorPoolSize frameSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSetsPerFrame * 5 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSetsPerFrame },
    };
    poolInfo.maxSets = kMaxSetsPerFrame;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = frameSizes;
    for ( VkDescriptorPool &pool : framePools ) {
        vkCreateDescriptorPool( device, &poolInfo, nullptr, &pool );
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = staticPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayouts[1];
    if ( vkAllocateDescriptorSets( device, &allocInfo, &samplerSet ) != VK_SUCCESS ) {
        return false;
    }
    VkDescriptorImageInfo samplerInfo = {};
    samplerInfo.sampler = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Clamp );
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = samplerSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &samplerInfo;
    vkUpdateDescriptorSets( device, 1, &write, 0, nullptr );

    bReady = true;
    OTDU_LOG_INFO( "2D layer uses the original shaders from '%s' (VS #%u, PS #%u)\n", kFlashShaderLibrary,
                   kVertexShaderIndex, kPixelShaderIndex );
    return true;
}

void FlashShaderPipeline::destroy() {
    if ( pDevice == nullptr ) {
        return;
    }
    VkDevice device = pDevice->getDevice();
    for ( uint32_t mode = 1; mode < static_cast<uint32_t>( eStencilMode::SM_Count ); mode++ ) {
        pDevice->destroyPipeline( stencilPipelines[mode] );
        stencilPipelines[mode] = VK_NULL_HANDLE;
    }
    for ( VkPipeline &fontPipeline : fontPipelines ) {
        pDevice->destroyPipeline( fontPipeline );
        fontPipeline = VK_NULL_HANDLE;
    }
    stencilPipelines[0] = VK_NULL_HANDLE;
    pDevice->destroyPipeline( pipeline );
    pipeline = VK_NULL_HANDLE;
    if ( pipelineLayout != VK_NULL_HANDLE ) {
        vkDestroyPipelineLayout( device, pipelineLayout, nullptr );
    }
    for ( VkDescriptorSetLayout &layout : setLayouts ) {
        if ( layout != VK_NULL_HANDLE ) {
            vkDestroyDescriptorSetLayout( device, layout, nullptr );
        }
        layout = VK_NULL_HANDLE;
    }
    for ( VkDescriptorPool &pool : framePools ) {
        if ( pool != VK_NULL_HANDLE ) {
            vkDestroyDescriptorPool( device, pool, nullptr );
        }
        pool = VK_NULL_HANDLE;
    }
    if ( staticPool != VK_NULL_HANDLE ) {
        vkDestroyDescriptorPool( device, staticPool, nullptr );
    }
    staticPool = VK_NULL_HANDLE;
    pDevice->destroyBuffer( pZeroBuffer );
    pDevice->destroyBuffer( pSpecBuffer );
    pZeroBuffer = pSpecBuffer = nullptr;
    pipelineLayout = VK_NULL_HANDLE;
    bReady = false;
}

bool FlashShaderPipeline::bind( VkCommandBuffer commandBuffer, GPUTexture *pTexture, const float *pProjection,
                                eStencilMode stencil, uint32_t stencilReference, bool bFont ) {
    if ( !bReady || pTexture == nullptr ) {
        return false;
    }
    if ( bFont && fontPipelines[static_cast<size_t>( stencil )] == VK_NULL_HANDLE ) {
        return false;
    }
    VkDevice device = pDevice->getDevice();
    const uint32_t slot = pDevice->getFrameSlot();
    if ( framePoolNumbers[slot] != pDevice->getFrameNumber() ) {
        vkResetDescriptorPool( device, framePools[slot], 0 );
        framePoolNumbers[slot] = pDevice->getFrameNumber();
    }

    // VS constants. The replacement path gives x_ndc = p0 * x + p12 and
    // y_ndc = p5 * y + p13 (Vulkan, y down). TD_Flash computes:
    //   X = dot(c60.xyw, (x, y, 1)), Y = dot(c61.xyw, (x, y, 1))
    //   view = (d (2X - 1) / c17.x, -(2Y - 1) d / c18.y, -d), d = 0.05 (2D)
    //   clip = (c17..c20) * view
    // With the projection rows below, ndc = (2X - 1, 1 - 2Y, 0.5), so the
    // D3D (y up) convention is folded into c61.
    TransientAllocation constants =
        pDevice->allocateTransient( kVSConstantsSize, pDevice->getUniformBufferAlignment() );
    if ( constants.pData == nullptr ) {
        return false;
    }
    memset( constants.pData, 0, kVSConstantsSize );
    float *pFloats = reinterpret_cast<float *>( static_cast<uint8_t *>( constants.pData ) + 16 * 16 );
    auto setRegister = [&]( uint32_t index, float x, float y, float z, float w ) {
        pFloats[index * 4 + 0] = x;
        pFloats[index * 4 + 1] = y;
        pFloats[index * 4 + 2] = z;
        pFloats[index * 4 + 3] = w;
    };
    setRegister( 17, 1.0f, 0.0f, 0.0f, 0.0f );
    setRegister( 18, 0.0f, 1.0f, 0.0f, 0.0f );
    setRegister( 19, 0.0f, 0.0f, -0.5f, 0.0f );
    setRegister( 20, 0.0f, 0.0f, -1.0f, 0.0f );
    const float p0 = pProjection[0], p5 = pProjection[5], p12 = pProjection[12], p13 = pProjection[13];
    setRegister( 60, p0 * 0.5f, 0.0f, 0.0f, ( p12 + 1.0f ) * 0.5f );
    setRegister( 61, 0.0f, -p5 * 0.5f, 0.0f, 1.0f - ( p13 + 1.0f ) * 0.5f );

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = framePools[slot];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayouts[0];
    VkDescriptorSet set = VK_NULL_HANDLE;
    if ( vkAllocateDescriptorSets( device, &allocInfo, &set ) != VK_SUCCESS ) {
        return false;
    }

    const VkDescriptorBufferInfo vsConstants = { constants.Buffer, constants.Offset, kVSConstantsSize };
    const VkDescriptorBufferInfo clipPlanes = { pZeroBuffer->Buffer, 0, 6 * 16 };
    const VkDescriptorBufferInfo psConstants = { pZeroBuffer->Buffer, 0, kPSConstantsSize };
    const VkDescriptorBufferInfo sharedPS = { pZeroBuffer->Buffer, 0, 8 * 64 };
    const VkDescriptorBufferInfo specState = { pSpecBuffer->Buffer, 0, kSpecDwords * sizeof( uint32_t ) };
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = pTexture->View;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[6] = {};
    const struct {
        uint32_t Binding;
        const VkDescriptorBufferInfo *pBuffer;
    } buffers[] = { { 0, &vsConstants }, { 3, &clipPlanes }, { 10, &psConstants }, { 12, &sharedPS }, { 31, &specState } };
    for ( uint32_t i = 0; i < 5; i++ ) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = buffers[i].Binding;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i].pBufferInfo = buffers[i].pBuffer;
    }
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = set;
    writes[5].dstBinding = 13;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].pImageInfo = &imageInfo;
    vkUpdateDescriptorSets( device, 6, writes, 0, nullptr );

    // D3D9RenderStateInfo: zero = fog off, alpha ref 0, sampler heap index 0.
    uint8_t renderState[RenderDevice::kPushConstantsSize] = {};

    const VkDescriptorSet sets[2] = { set, samplerSet };
    VkPipeline selected = bFont ? fontPipelines[static_cast<size_t>( stencil )] : stencilPipelines[static_cast<size_t>( stencil )];
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, selected != VK_NULL_HANDLE ? selected : pipeline );
    if ( stencil != eStencilMode::SM_None ) {
        vkCmdSetStencilReference( commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, stencilReference );
    }
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0,
                             nullptr );
    vkCmdPushConstants( commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof( renderState ), renderState );
    return true;
}
