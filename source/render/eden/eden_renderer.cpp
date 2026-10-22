#include "shared.h"
#include "eden_renderer.h"
#include "eden_material_handlers.h"

#include "render/texture_cache.h"
#include "system/cmd_line.h"

namespace {
constexpr uint32_t kIntRegistersSize = 16 * 16;
constexpr uint32_t kBoolBitmaskSize = 16;
constexpr uint32_t kVSFloatRegisters = 256;
constexpr uint32_t kPSFloatRegisters = 224;
constexpr VkDeviceSize kVSConstantsSize = kIntRegistersSize + kVSFloatRegisters * 16 + kBoolBitmaskSize;
constexpr VkDeviceSize kPSConstantsSize = kIntRegistersSize + kPSFloatRegisters * 16 + kBoolBitmaskSize;
constexpr VkDeviceSize kZeroBufferSize = 8192;
constexpr uint32_t kSpecDwords = 16;
constexpr uint32_t kMaxSetsPerFrame = 8192;
constexpr uint32_t kNumVSImages = 4;
constexpr uint32_t kNumPSImages = 16;
constexpr uint32_t kVSImageBinding = 6;
constexpr uint32_t kPSImageBinding = 13;
constexpr uint32_t kDefaultAttributeBinding = EdenMesh::kMaxStreams;

// D3D9SpecializationInfo: dword 1 bits 21..23 = alpha compare op.
constexpr uint32_t kSpecAlphaCompareOpShift = 21;
// D3D9RenderStateInfo::alphaRef
constexpr uint32_t kPushAlphaRefOffset = 24;
// GetPushSamplerOffset(0) = MaxSharedPushDataSize
constexpr uint32_t kPushSamplerOffset = 64;

enum eSamplerHeapIndex : uint8_t {
    SH_LinearWrap = 0,
    SH_LinearClamp,
    SH_PointWrap,
    SH_PointClamp,
    SH_LinearMirror,
    SH_Count
};

VkFormat GetAttributeFormat( eVertexAttributeFormat format ) {
    switch ( format ) {
    case VAF_POSITION_R32G32B32_SFLOAT:
    case VAF_NORMAL_R32G32B32_SFLOAT:
    case VAF_TANGENT_R32G32B32_SFLOAT:
    case VAF_BONEWEIGHT_R32G32B32_SFLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VAF_POSITION_R32G32B32A32_SFLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VAF_POSITION_R16G16B16A16_SFLOAT:
    case VAF_NORMAL_R16G16B16A16_SFLOAT:
    case VAF_TANGENT_R16G16B16A16_SFLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case VAF_COLOR_B8G8R8A8_UNORM:
    case VAF_COLOR_B8G8R8A8_UNORM_2:
    case VAF_COLOR_B8G8R8A8_UNORM_3:
    case VAF_COLOR_B8G8R8A8_UNORM_4:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VAF_NORMAL_A2B10G10R10_SNORM_PACK32:
    case VAF_TANGENT_A2B10G10R10_SNORM_PACK32:
        return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case VAF_NORMAL_R16G16B16A16_UNORM:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case VAF_NORMAL_R16G16B16A16_SNORM:
    case VAF_TANGENT_R16G16B16A16_SNORM:
        return VK_FORMAT_R16G16B16A16_SNORM;
    case VAF_TEXCOORD_R32G32_SFLOAT:
    case VAF_BONEWEIGHT_R32G32_SFLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case VAF_TEXCOORD_R16G16_SNORM:
        return VK_FORMAT_R16G16_SNORM;
    case VAF_TEXCOORD_R16G16_SFLOAT:
        return VK_FORMAT_R16G16_SFLOAT;
    case VAF_POSITION_R32_SFLOAT:
    case VAF_BONEWEIGHT_R32_SFLOAT:
        return VK_FORMAT_R32_SFLOAT;
    case VAF_BONEINDEX_R8G8B8A8_USCALED:
        return VK_FORMAT_R8G8B8A8_USCALED;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

VkBlendFactor GetBlendFactor( uint8_t d3dBlend ) {
    switch ( d3dBlend ) {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case 2: return VK_BLEND_FACTOR_ONE;
    case 3: return VK_BLEND_FACTOR_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return VK_BLEND_FACTOR_DST_ALPHA;
    case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return VK_BLEND_FACTOR_DST_COLOR;
    case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

// Writes a D3D matrix (Matrix4x4f memory = D3D row major layout) to shader
// registers. HLSL packs matrices column major by default: register k holds
// column k.
void WriteMatrix( float *pOut, uint32_t numRegisters, uint8_t parameterClass, const Matrix4x4f &matrix ) {
    static const bool bTranspose = CmdLine::HasArg( "-edentranspose" );
    if ( bTranspose ) {
        parameterClass = parameterClass == 2 ? 3 : 2;
    }
    for ( uint32_t k = 0; k < numRegisters && k < 4; k++ ) {
        for ( uint32_t i = 0; i < 4; i++ ) {
            pOut[k * 4 + i] = parameterClass == 2 ? matrix.m[k][i] : matrix.m[i][k];
        }
    }
}

void WriteVector( float *pOut, const Vector4f &v ) {
    pOut[0] = v.x;
    pOut[1] = v.y;
    pOut[2] = v.z;
    pOut[3] = v.w;
}

void WriteVector( float *pOut, const Vector3f &v, float w = 0.0f ) { WriteVector( pOut, Vector4f( v, w ) ); }

Vector3f Mul( const Vector3f &a, const Vector4f &b ) { return Vector3f( a.x * b.x, a.y * b.y, a.z * b.z ); }

std::string StripSuffix( const std::string &name ) {
    const size_t position = name.find( "__" );
    if ( position == std::string::npos || position == 0 ) {
        return name;
    }
    return name.substr( 0, position );
}
} // namespace

void EdenMesh::computeLayoutKey() {
    uint64_t hash = 0xcbf29ce484222325ull;
    auto mix = [&]( uint64_t value ) {
        hash ^= value;
        hash *= 0x100000001b3ull;
    };
    mix( NumStreams );
    for ( uint32_t i = 0; i < kMaxStreams; i++ ) {
        mix( Strides[i] );
    }
    mix( static_cast<uint64_t>( Topology ) );
    for ( const RawVertexAttribute &attribute : Attributes ) {
        mix( ( static_cast<uint64_t>( attribute.Usage ) << 32 ) | ( static_cast<uint64_t>( attribute.UsageIndex ) << 24 ) |
             ( static_cast<uint64_t>( attribute.Stream ) << 16 ) | ( static_cast<uint64_t>( attribute.Format ) << 8 ) );
        mix( attribute.Offset );
    }
    LayoutKey = hash;
}

EdenRenderer::EdenRenderer()
    : pDevice( nullptr ),
      pTextureCache( nullptr ),
      setLayouts{ VK_NULL_HANDLE, VK_NULL_HANDLE },
      pipelineLayout( VK_NULL_HANDLE ),
      staticPool( VK_NULL_HANDLE ),
      framePools{},
      framePoolNumbers{},
      samplerSet( VK_NULL_HANDLE ),
      pZeroBuffer( nullptr ),
      pDefaultAttributes( nullptr ),
      pWhite2D( nullptr ),
      pWhiteCube( nullptr ),
      pWhite3D( nullptr ),
      pShadowRealTime( nullptr ),
      pFlatNormal( nullptr ),
      pScreenTexture( nullptr ),
      numDrawCalls( 0 ),
      bInScene( false ),
      bReady( false ) {}

EdenRenderer::~EdenRenderer() {}

GPUTexture *EdenRenderer::createWhiteTexture( VkImageViewType viewType, const char *pName ) {
    static const uint32_t kWhite = 0xffffffffu;
    const uint32_t numLayers = viewType == VK_IMAGE_VIEW_TYPE_CUBE ? 6 : 1;
    const void *data[6] = { &kWhite, &kWhite, &kWhite, &kWhite, &kWhite, &kWhite };
    const uint32_t sizes[6] = { 4, 4, 4, 4, 4, 4 };
    return pDevice->createTexture( viewType, 1, 1, 1, numLayers, 1, VK_FORMAT_R8G8B8A8_UNORM, data, sizes, pName );
}

bool EdenRenderer::initialize( RenderDevice *pRenderDevice, TextureCache *pCache ) {
    pDevice = pRenderDevice;
    pTextureCache = pCache;
    if ( CmdLine::HasArg( "-nosob" ) || !pDevice->supportsDxsoShaders() ) {
        return false;
    }
    if ( !shaderSystem.initialize() ) {
        return false;
    }
    VkDevice device = pDevice->getDevice();

    // Set 0: DXVK D3D9 resource slots (see computeResourceSlotId).
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back( { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr } );
    bindings.push_back( { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr } );
    for ( uint32_t i = 0; i < kNumVSImages; i++ ) {
        bindings.push_back(
            { kVSImageBinding + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr } );
    }
    bindings.push_back( { 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    bindings.push_back( { 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    for ( uint32_t i = 0; i < kNumPSImages; i++ ) {
        bindings.push_back(
            { kPSImageBinding + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    }
    bindings.push_back( { 31, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>( bindings.size() );
    layoutInfo.pBindings = bindings.data();
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &setLayouts[0] ) != VK_SUCCESS ) {
        return false;
    }
    // Set 1: sampler heap
    const VkDescriptorSetLayoutBinding samplerBinding = {
        0, VK_DESCRIPTOR_TYPE_SAMPLER, SH_Count, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
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

    // Static resources
    std::vector<uint8_t> zeros( kZeroBufferSize, 0 );
    pZeroBuffer = pDevice->createBuffer( kZeroBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, zeros.data() );
    const float defaults[] = {
        0.0f, 0.0f, 0.0f, 1.0f, // 0: generic
        1.0f, 1.0f, 1.0f, 1.0f, // 16: colors
        0.0f, 1.0f, 0.0f, 0.0f, // 32: normal
        1.0f, 0.0f, 0.0f, 0.0f, // 48: tangent / binormal
    };
    pDefaultAttributes = pDevice->createBuffer( sizeof( defaults ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, defaults );
    pWhite2D = createWhiteTexture( VK_IMAGE_VIEW_TYPE_2D, "eden_white_2d" );
    pWhiteCube = createWhiteTexture( VK_IMAGE_VIEW_TYPE_CUBE, "eden_white_cube" );
    pWhite3D = createWhiteTexture( VK_IMAGE_VIEW_TYPE_3D, "eden_white_3d" );
    {
        static const uint32_t kBlack = 0u;
        const void *data = &kBlack;
        const uint32_t size = 4;
        pBlack2D = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &data, &size,
                                           "eden_black_2d" );
    }
    {
        // gTexSamplerShadowRealTime (s15): screen space sun shadow / light
        // buffer. x, w: sun visibility, y, z: packed additional lighting.
        static const uint32_t kLit = 0xff0000ffu; // RGBA (255, 0, 0, 255)
        const void *data = &kLit;
        const uint32_t size = 4;
        pShadowRealTime = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &data,
                                                  &size, "eden_shadow_realtime" );
        static const uint32_t kFlatNormal = 0xffff8080u; // (128, 128, 255, 255)
        const void *normalData = &kFlatNormal;
        pFlatNormal = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                              &normalData, &size, "eden_flat_normal" );
    }

    samplerHeap.resize( SH_Count );
    samplerHeap[SH_LinearWrap] = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Wrap );
    samplerHeap[SH_LinearClamp] = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Clamp );
    samplerHeap[SH_PointWrap] = pDevice->getSampler( eSamplerFilter::SF_Point, eSamplerAddress::SA_Wrap );
    samplerHeap[SH_PointClamp] = pDevice->getSampler( eSamplerFilter::SF_Point, eSamplerAddress::SA_Clamp );
    samplerHeap[SH_LinearMirror] = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Mirror );

    const VkDescriptorPoolSize staticSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, SH_Count } };
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = staticSizes;
    vkCreateDescriptorPool( device, &poolInfo, nullptr, &staticPool );

    const VkDescriptorPoolSize frameSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSetsPerFrame * 5 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSetsPerFrame * ( kNumVSImages + kNumPSImages ) },
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
    std::vector<VkDescriptorImageInfo> samplerInfos( SH_Count );
    for ( uint32_t i = 0; i < SH_Count; i++ ) {
        samplerInfos[i].sampler = samplerHeap[i];
    }
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = samplerSet;
    write.dstBinding = 0;
    write.descriptorCount = SH_Count;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = samplerInfos.data();
    vkUpdateDescriptorSets( device, 1, &write, 0, nullptr );

    bReady = pZeroBuffer != nullptr && pDefaultAttributes != nullptr && pWhite2D != nullptr &&
             pWhiteCube != nullptr && pWhite3D != nullptr;
    return bReady;
}

void EdenRenderer::destroy() {
    if ( pDevice == nullptr ) {
        return;
    }
    VkDevice device = pDevice->getDevice();
    for ( auto &pipeline : pipelines ) {
        pDevice->destroyPipeline( pipeline.second );
    }
    pipelines.clear();
    if ( pipelineLayout != VK_NULL_HANDLE ) {
        vkDestroyPipelineLayout( device, pipelineLayout, nullptr );
        pipelineLayout = VK_NULL_HANDLE;
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
    for ( std::vector<VkDescriptorPool> &pools : overflowPools ) {
        for ( VkDescriptorPool pool : pools ) {
            vkDestroyDescriptorPool( device, pool, nullptr );
        }
        pools.clear();
    }
    if ( staticPool != VK_NULL_HANDLE ) {
        vkDestroyDescriptorPool( device, staticPool, nullptr );
        staticPool = VK_NULL_HANDLE;
    }
    pDevice->destroyBuffer( pZeroBuffer );
    pDevice->destroyBuffer( pDefaultAttributes );
    pDevice->destroyTexture( pWhite2D );
    pDevice->destroyTexture( pWhiteCube );
    pDevice->destroyTexture( pWhite3D );
    pDevice->destroyTexture( pShadowRealTime );
    pDevice->destroyTexture( pFlatNormal );
    pDevice->destroyTexture( pBlack2D );
    pBlack2D = nullptr;
    pZeroBuffer = pDefaultAttributes = nullptr;
    pWhite2D = pWhiteCube = pWhite3D = nullptr;
    shaderSystem.destroy();
    bReady = false;
}

void EdenRenderer::setupMaterial( const MaterialDesc &desc, EdenMaterial &outMaterial, const char *pTextureOwner ) {
    outMaterial.Name = desc.Name;
    outMaterial.Hashcode = desc.Hashcode;
    outMaterial.ShaderSetGUID = desc.ShaderSetGUID;
    memcpy( outMaterial.Spec, desc.Spec, sizeof( outMaterial.Spec ) );
    memcpy( outMaterial.States, desc.States, sizeof( outMaterial.States ) );
    outMaterial.Parameters = desc.Parameters;
    outMaterial.Diffuse = desc.DiffuseColor;
    outMaterial.Ambient = desc.AmbientColor;
    outMaterial.Specular = desc.SpecularColor;
    outMaterial.Emissive = desc.EmissiveColor;
    outMaterial.SrcBlend = desc.SrcBlend;
    outMaterial.DstBlend = desc.DstBlend;
    static const bool bNoBlend = CmdLine::HasArg( "-edennoblend" );
    if ( bNoBlend ) {
        outMaterial.SrcBlend = 2;
        outMaterial.DstBlend = 1;
    }
    outMaterial.CullMode = desc.CullMode;
    outMaterial.AlphaRef = desc.AlphaRef;
    outMaterial.bAlphaTest = desc.AlphaRef != 0 && desc.AlphaTest == 0;
    outMaterial.bDepthTest = desc.DepthTest != 0;
    outMaterial.bDepthWrite = desc.DepthWrite != 0;

    for ( uint32_t i = 0; i < 16; i++ ) {
        outMaterial.pTextures[i] = nullptr;
        outMaterial.SamplerIndex[i] = SH_LinearWrap;
    }
    for ( size_t i = 0; i < desc.AllTextures.size() && i < 16; i++ ) {
        const uint64_t textureHash = desc.AllTextures[i].TextureHashcode;
        outMaterial.bNormalSlot[i] =
            ToUpper( IdentifierToString( desc.AllTextures[i].SlotType ) ).find( "NORMAL" ) != std::string::npos;
        if ( textureHash == 0 ) {
            continue;
        }
        GPUTexture *pTexture = nullptr;
        if ( pTextureOwner != nullptr ) {
            pTexture = pTextureCache->find( pTextureOwner, IdentifierToString( textureHash ).c_str() );
        }
        if ( pTexture == nullptr ) {
            pTexture = pTextureCache->find( textureHash );
        }
        outMaterial.pTextures[i] = pTexture;
        if ( pTexture == nullptr ) {
            OTDU_LOG_DEBUG( "Eden material '%s': texture '%s' (slot %zu) not found\n", desc.Name.c_str(),
                            IdentifierToString( textureHash ).c_str(), i );
        }
    }
    for ( size_t i = 0; i < 16; i++ ) {
        outMaterial.TextureHashes[i] = i < desc.AllTextures.size() ? desc.AllTextures[i].TextureHashcode : 0;
        outMaterial.SlotTypes[i] = i < desc.AllTextures.size() ? desc.AllTextures[i].SlotType : 0;
    }
    outMaterial.NumSlots = static_cast<uint32_t>( Min<size_t>( desc.AllTextures.size(), 16 ) );

    // Load time preparation of the original renderer (GUID / switches /
    // render states rewritten by the shader family handler).
    outMaterial.Bucket = ApplyEdenMaterialHandler( outMaterial );
    applyRenderStates( outMaterial );
    updateProgram( outMaterial );
}

void EdenRenderer::applyRenderStates( EdenMaterial &material ) {
    const uint8_t *pStates = material.States;
    const bool bBlend = pStates[0xa] != 6;
    material.SrcBlend = bBlend ? pStates[8] : 2;
    material.DstBlend = bBlend ? pStates[9] : 1;
    static const bool bNoBlend = CmdLine::HasArg( "-edennoblend" );
    if ( bNoBlend ) {
        material.SrcBlend = 2;
        material.DstBlend = 1;
    }
    material.bDepthTest = pStates[0] != 0;
    material.bDepthWrite = pStates[1] != 0 && pStates[1] != 2;
    material.CullMode = pStates[3];
    material.AlphaRef = pStates[7];
    material.bAlphaTest = pStates[5] == 0;
    material.DepthFunc = pStates[0xb] <= 8 ? pStates[0xb] : 0;
    // TD_ShadowBox volumes (back faces drawn with GREATEREQUAL) are wound
    // the other way round than the rest of the scene in this renderer
    // (observed: their stated culling keeps the front faces).
    // (4 = OpenTDU2 internal "cull the other face"; 3 is two-sided).
    if ( material.ShaderSetGUID == 0x132435465768798aull && ( material.CullMode == 2 || material.CullMode == 3 ) ) {
        material.CullMode = material.CullMode == 2 ? 4 : 2;
    }
}

bool EdenRenderer::assignTexture( EdenMaterial &material, const char *pSlotType, GPUTexture *pTexture ) {
    const uint64_t slotHash = GetIdentifier64bit( pSlotType );
    for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
        if ( material.SlotTypes[i] == slotHash ) {
            material.pTextures[i] = pTexture;
            material.TextureHashes[i] = pTexture != nullptr ? GetIdentifier64bit( pTexture->Name.c_str() ) : 0;
            return true;
        }
    }
    return false;
}

void EdenRenderer::updateProgram( EdenMaterial &material ) {
    // Runtime switches (the scene type handler of the original renderer is in
    // the encrypted part of the executable; its effect is reproduced from the
    // permutations the libraries contain).
    uint8_t spec[EdenShaderSystem::kNumSpecs];
    memcpy( spec, material.Spec, sizeof( spec ) );
    uint32_t numTextures = 0;
    for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
        if ( material.TextureHashes[i] != 0 ) {
            numTextures++;
        }
    }
    ShaderLibrary *pLibrary = shaderSystem.findLibrary( material.ShaderSetGUID );
    const std::string family = pLibrary != nullptr ? ToUpper( pLibrary->getName() ) : std::string();
    if ( family == "TD_HEIGHTMAP" || family == "TD_GROUND_HM" ) {
        // Slots: COLOR, SHADOW (+ night lights, grass), DETAIL1, DETAIL1 normal,
        // DETAIL2, DETAIL2 normal, DETAIL3 (normal xy + gloss + color).
        // S_USERTYPE4: grid (0: sector 3 m, 1: area 24 m, 2: island 384 m)
        // S_USERTYPE7: detail layers mask.
        spec[35] = material.HeightfieldGrid;
        if ( material.HeightfieldGrid == 0 ) {
            const bool bDetail1 = material.TextureHashes[2] != 0;
            const bool bDetail2 = material.TextureHashes[4] != 0;
            const bool bDetail3 = material.TextureHashes[6] != 0;
            spec[38] = static_cast<uint8_t>( ( bDetail1 ? 1 : 0 ) | ( bDetail2 ? 2 : 0 ) | ( bDetail3 ? 4 : 0 ) );
            spec[0] = static_cast<uint8_t>( 2 + ( bDetail1 ? 2 : 0 ) + ( bDetail2 ? 2 : 0 ) + ( bDetail3 ? 1 : 0 ) );
        } else {
            spec[38] = 0;
            spec[0] = 1;
        }
    } else if ( numTextures > 0 ) {
        spec[EdenShaderSystem::S_NBTEXTURES] = static_cast<uint8_t>( numTextures );
    }
    material.pProgram = shaderSystem.getProgram( material.ShaderSetGUID, spec );
}

void EdenRenderer::beginScene( const EdenSceneParams &params ) {
    scene = params;
    viewProjection = scene.Projection * scene.View;
    deferredDraws.clear();
    waterDraws.clear();
    numDrawCalls = 0;
    bInScene = true;

    VkDevice device = pDevice->getDevice();
    const uint32_t slot = pDevice->getFrameSlot();
    if ( framePoolNumbers[slot] != pDevice->getFrameNumber() ) {
        vkResetDescriptorPool( device, framePools[slot], 0 );
        for ( VkDescriptorPool pool : overflowPools[slot] ) {
            vkResetDescriptorPool( device, pool, 0 );
        }
        static const bool bLogSets = CmdLine::HasArg( "-logsets" );
        if ( bLogSets ) {
            OTDU_LOG_INFO( "Eden sets last frame of slot %u: %u\n", slot, setsAllocated[slot] );
        }
        setsAllocated[slot] = 0;
        overflowUsed[slot] = 0;
        framePoolNumbers[slot] = pDevice->getFrameNumber();
    }

    resumeScene();
}

void EdenRenderer::resumeScene() {
    // D3D clip space (y up): flipped viewport (Vulkan clip space is y down).
    VkCommandBuffer commandBuffer = pDevice->getCommandBuffer();
    const float width = static_cast<float>( scene.Width );
    const float height = static_cast<float>( scene.Height );
    static const bool bNoFlip = CmdLine::HasArg( "-edennoflip" );
    VkViewport viewport = { 0.0f, bNoFlip ? 0.0f : height, width, bNoFlip ? height : -height, 0.0f, 1.0f };
    vkCmdSetViewport( commandBuffer, 0, 1, &viewport );
}

void EdenRenderer::flushWater() {
    for ( const DeferredDraw &draw : waterDraws ) {
        recordDraw( draw.Mesh, *draw.pMaterial, draw.World, draw.pParams.get() );
    }
    waterDraws.clear();
}

void EdenRenderer::flushDeferred() {
    flushWater();
    std::sort( deferredDraws.begin(), deferredDraws.end(),
               []( const DeferredDraw &a, const DeferredDraw &b ) { return a.Distance > b.Distance; } );
    for ( const DeferredDraw &draw : deferredDraws ) {
        recordDraw( draw.Mesh, *draw.pMaterial, draw.World, draw.pParams.get() );
    }
    deferredDraws.clear();
}

void EdenRenderer::endScene() {
    if ( !bInScene ) {
        return;
    }
    flushWater();
    std::sort( deferredDraws.begin(), deferredDraws.end(),
               []( const DeferredDraw &a, const DeferredDraw &b ) { return a.Distance > b.Distance; } );
    for ( const DeferredDraw &draw : deferredDraws ) {
        recordDraw( draw.Mesh, *draw.pMaterial, draw.World, draw.pParams.get() );
    }
    deferredDraws.clear();

    VkCommandBuffer commandBuffer = pDevice->getCommandBuffer();
    const float width = static_cast<float>( scene.Width );
    const float height = static_cast<float>( scene.Height );
    VkViewport viewport = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
    vkCmdSetViewport( commandBuffer, 0, 1, &viewport );
    bInScene = false;
}

void EdenRenderer::draw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                         const EdenDrawParams *pParams ) {
    if ( !bReady || !bInScene || material.pProgram == nullptr || mesh.IndexCount == 0 ) {
        return;
    }
    if ( bReflectionPass && ( material.isBlended() || material.ShaderSetGUID == 0x0cea000000000002ull ) ) {
        return;
    }
    if ( bShadowCaster ) {
        // Blended materials cast only when alpha tested (vegetation leaves:
        // their S_PASS_SHADOWCAST permutation texkills).
        if ( material.isBlended() && !material.bAlphaTest ) {
            return;
        }
        const EdenMaterial *pCaster = getCasterMaterial( material );
        if ( pCaster != nullptr && pCaster->pProgram != nullptr ) {
            recordDraw( mesh, *pCaster, world, pParams );
        }
        return;
    }
    if ( bDepthOnly ) {
        // Z pre-pass (ZPREPASS layer): drawn with the materials' S_PASS_Z
        // permutations (TD_Road ps_100, TD_Building ps_544...: depth write,
        // no blending); families without one: opaque and alpha tested draws.
        if ( const EdenMaterial *pZPass = getZPassMaterial( material ) ) {
            recordDraw( mesh, *pZPass, world, pParams );
            return;
        }
        if ( material.isBlended() && !material.bAlphaTest ) {
            return;
        }
        recordDraw( mesh, material, world, pParams );
        return;
    }
    // Water buckets (TD_Water 0x19 / 0x2e, TD_WaterRiver 0x17 / 0x18) come
    // after the opaque, avatar and building buckets in the layer order
    // [CODE 0x00d70cf0 + layer table 0x00f7f604]: the opaque water draws are
    // kept until the blended draws are flushed (and recorded first).
    const bool bLateWater = !material.isBlended() && material.Bucket >= 0x17 && material.Bucket <= 0x19;
    const bool bLateWaterAlpha = !material.isBlended() && material.Bucket == 0x2e;
    if ( bLateWater || bLateWaterAlpha ||
         ( material.isBlended() && ( pParams == nullptr || pParams->bDeferBlended ) ) ) {
        DeferredDraw deferred;
        if ( pParams != nullptr ) {
            deferred.pParams = std::make_shared<EdenDrawParams>( *pParams );
            if ( pParams->pSkinMatrices != nullptr && pParams->NumSkinMatrices > 0 ) {
                deferred.pParams->SkinStorage.assign( pParams->pSkinMatrices,
                                                      pParams->pSkinMatrices + pParams->NumSkinMatrices );
                deferred.pParams->pSkinMatrices = deferred.pParams->SkinStorage.data();
            }
        }
        deferred.Mesh = mesh;
        deferred.pMaterial = &material;
        deferred.World = world;
        const Vector3f position = world.getTranslation();
        deferred.Distance = ( position - scene.CameraPosition ).length();
        if ( bLateWater || bLateWaterAlpha ) {
            waterDraws.push_back( deferred );
        } else {
            deferredDraws.push_back( deferred );
        }
        return;
    }
    recordDraw( mesh, material, world, pParams );
}

VkPipeline EdenRenderer::getPipeline( const EdenMesh &mesh, const EdenMaterial &material, bool bMirrored ) {
    const EdenProgram *pProgram = material.pProgram;
    static const bool bSwapCull = CmdLine::HasArg( "-edenswapcull" );
    static const bool bNoCull = CmdLine::HasArg( "-nocull" );
    uint8_t cullMode = bNoCull ? 1 : material.CullMode;
    // Eden cull state 3 = two-sided (data: *_dblS meshes, eyelashes, car
    // glass, the DJ shirt; spec_skin_zones 3.1).
    if ( cullMode == 3 ) {
        cullMode = 1;
    }
    // Mirroring world matrix (negative determinant): D3D culls in screen
    // space, the winding of the mesh flips.
    if ( bMirrored && ( cullMode == 2 || cullMode == 4 ) ) {
        cullMode = cullMode == 2 ? 4 : 2;
    }
    char key[256];
    const uint8_t writeMask = bDepthOnly ? 0 : material.ColorWriteMask;
    snprintf( key, sizeof( key ), "%p:%p:%016llx:%u:%u:%u:%u:%u:%u:%u:%u", static_cast<void *>( pDevice->getCurrentRenderPass() ),
              static_cast<const void *>( pProgram->pVS ),
              static_cast<unsigned long long>( mesh.LayoutKey ), material.SrcBlend, material.DstBlend, cullMode,
              material.bDepthTest ? 1 : 0, material.bDepthWrite ? 1 : 0, material.isBlended() ? 1 : 0,
              material.BlendOp, writeMask );
    std::string depthFuncKey = ":z" + std::to_string( material.DepthFunc );
    std::string fullKey = std::string( key ) + ":" + std::to_string( reinterpret_cast<uintptr_t>( pProgram->pPS ) ) +
                          depthFuncKey;
    auto it = pipelines.find( fullKey );
    if ( it != pipelines.end() ) {
        return it->second;
    }

    VkDevice device = pDevice->getDevice();
    auto createModule = [&]( const std::vector<uint32_t> &code ) {
        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * sizeof( uint32_t );
        info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule( device, &info, nullptr, &module );
        return module;
    };
    VkShaderModule vertexModule = createModule( pProgram->pVS->SPIRV );
    VkShaderModule fragmentModule = createModule( pProgram->pPS->SPIRV );

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    // Vertex input: match the VS dcl semantics with the original streams.
    std::vector<VkVertexInputBindingDescription> bindings;
    for ( uint32_t i = 0; i < mesh.NumStreams; i++ ) {
        bindings.push_back( { i, mesh.Strides[i], VK_VERTEX_INPUT_RATE_VERTEX } );
    }
    bool bUsesDefaults = false;
    std::vector<VkVertexInputAttributeDescription> attributes;
    for ( const ShaderInputInfo &input : pProgram->pVS->Inputs ) {
        const RawVertexAttribute *pMatch = nullptr;
        for ( const RawVertexAttribute &attribute : mesh.Attributes ) {
            if ( attribute.Usage == input.Usage && attribute.UsageIndex == input.UsageIndex ) {
                pMatch = &attribute;
                break;
            }
        }
        const VkFormat format = pMatch != nullptr ? GetAttributeFormat( pMatch->Format ) : VK_FORMAT_UNDEFINED;
        if ( pMatch != nullptr && format != VK_FORMAT_UNDEFINED && pMatch->Stream < mesh.NumStreams ) {
            attributes.push_back( { input.Location, pMatch->Stream, format, pMatch->Offset } );
            continue;
        }
        uint32_t offset = 0;
        if ( input.Usage == 10 ) { // COLOR
            offset = 16;
        } else if ( input.Usage == 3 ) { // NORMAL
            offset = 32;
        } else if ( input.Usage == 6 || input.Usage == 7 ) { // TANGENT, BINORMAL
            offset = 48;
        }
        attributes.push_back( { input.Location, kDefaultAttributeBinding, VK_FORMAT_R32G32B32A32_SFLOAT, offset } );
        bUsesDefaults = true;
    }
    if ( bUsesDefaults ) {
        while ( bindings.size() < kDefaultAttributeBinding ) {
            bindings.push_back( { static_cast<uint32_t>( bindings.size() ), 4, VK_VERTEX_INPUT_RATE_VERTEX } );
        }
        bindings.push_back( { kDefaultAttributeBinding, 0, VK_VERTEX_INPUT_RATE_VERTEX } );
    }

    if ( Logger::IsVerbose() ) {
        std::string description;
        for ( const ShaderInputInfo &input : pProgram->pVS->Inputs ) {
            description += "[u" + std::to_string( input.Usage ) + "." + std::to_string( input.UsageIndex ) + "@" +
                           std::to_string( input.Location ) + "]";
        }
        for ( const VkVertexInputAttributeDescription &attribute : attributes ) {
            description += " loc" + std::to_string( attribute.location ) + "=b" + std::to_string( attribute.binding ) +
                           "+" + std::to_string( attribute.offset ) + "/f" + std::to_string( attribute.format );
        }
        OTDU_LOG_DEBUG( "Eden pipeline %s (%s): %s blend %u/%u op %u at %d zw %d mask %x\n", pProgram->Name.c_str(),
                        material.Name.c_str(), description.c_str(), material.SrcBlend, material.DstBlend,
                        material.BlendOp, material.bAlphaTest ? 1 : 0, material.bDepthWrite ? 1 : 0, writeMask );
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>( bindings.size() );
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>( attributes.size() );
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = mesh.Topology;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // D3D9: front faces are clockwise on screen, D3DCULL_CCW (3) culls the
    // back faces, D3DCULL_CW (2) the front faces. With the flipped viewport,
    // the D3D clockwise triangles are counter clockwise for Vulkan.
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    // D3D: clockwise front faces (the flipped viewport keeps the screen
    // space winding of D3D).
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    if ( cullMode == 4 ) {
        rasterizer.cullMode = bSwapCull ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    } else if ( cullMode == 2 ) {
        rasterizer.cullMode = bSwapCull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
    }
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = material.bDepthTest ? VK_TRUE : VK_FALSE;
    // D3DRS_ZWRITEENABLE of the material, blended or not.
    depthStencil.depthWriteEnable = material.bDepthWrite ? VK_TRUE : VK_FALSE;
    // D3DCMPFUNC (MAT. render state +0x3b: TD_ShadowBox uses GREATEREQUAL)
    static constexpr VkCompareOp kCompareOps[9] = { VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_NEVER,
                                                    VK_COMPARE_OP_LESS,          VK_COMPARE_OP_EQUAL,
                                                    VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
                                                    VK_COMPARE_OP_NOT_EQUAL,     VK_COMPARE_OP_GREATER_OR_EQUAL,
                                                    VK_COMPARE_OP_ALWAYS };
    depthStencil.depthCompareOp = kCompareOps[material.DepthFunc <= 8 ? material.DepthFunc : 0];

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = ( ( writeMask & 1 ) ? VK_COLOR_COMPONENT_R_BIT : 0 ) |
                                     ( ( writeMask & 2 ) ? VK_COLOR_COMPONENT_G_BIT : 0 ) |
                                     ( ( writeMask & 4 ) ? VK_COLOR_COMPONENT_B_BIT : 0 ) |
                                     ( ( writeMask & 8 ) ? VK_COLOR_COMPONENT_A_BIT : 0 );
    if ( material.isBlended() ) {
        static constexpr VkBlendOp kBlendOps[6] = { VK_BLEND_OP_ADD, VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT,
                                                    VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX };
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = GetBlendFactor( material.SrcBlend );
        blendAttachment.dstColorBlendFactor = GetBlendFactor( material.DstBlend );
        blendAttachment.colorBlendOp = kBlendOps[material.BlendOp <= 5 ? material.BlendOp : 1];
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass =
        pDevice->getCurrentRenderPass() != VK_NULL_HANDLE ? pDevice->getCurrentRenderPass() : pDevice->getRenderPass();

    VkPipeline pipeline = VK_NULL_HANDLE;
    if ( vkCreateGraphicsPipelines( device, pDevice->getPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline ) !=
         VK_SUCCESS ) {
        OTDU_LOG_WARN( "Eden: pipeline creation failed for %s\n", pProgram->Name.c_str() );
        pipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule( device, vertexModule, nullptr );
    vkDestroyShaderModule( device, fragmentModule, nullptr );
    pipelines[fullKey] = pipeline;
    return pipeline;
}

void EdenRenderer::fillConstants( const std::vector<ShaderConstantMember> &members, float *pRegisters,
                                  uint32_t numRegisters, const EdenMaterial &material, const Matrix4x4f &world,
                                  const EdenDrawParams *pParams, const char *pProgramName, bool bPixel ) {
    const Matrix4x4f worldView = scene.View * world;
    const Matrix4x4f worldViewProjection = viewProjection * world;
    Matrix4x4f inverseWorld;
    bool bInverseWorld = false;
    auto getInverseWorld = [&]() -> const Matrix4x4f & {
        if ( !bInverseWorld ) {
            inverseWorld = world.inverse();
            bInverseWorld = true;
        }
        return inverseWorld;
    };

    for ( const ShaderConstantMember &member : members ) {
        if ( member.RegisterSet != 2 || member.Register + member.NumRegisters > numRegisters ) {
            continue;
        }
        float *pOut = pRegisters + member.Register * 4;
        const std::string &name = member.Name;

        // Stage specific overrides (the same name in the VS and the PS:
        // TD_Ocean / TD_ocean_nmap_from_hmap "matParam").
        {
            const auto &overrides = material.StageConstants[bPixel ? 1 : 0];
            auto it = overrides.find( name );
            if ( it != overrides.end() ) {
                for ( uint32_t i = 0; i < member.NumRegisters && i < it->second.size(); i++ ) {
                    WriteVector( pOut + i * 4, it->second[i] );
                }
                continue;
            }
        }
        if ( pParams != nullptr ) {
            const auto &stageVectors = bPixel ? pParams->PixelVectors : pParams->VertexVectors;
            auto itStage = stageVectors.find( name );
            if ( itStage != stageVectors.end() ) {
                WriteVector( pOut, itStage->second );
                continue;
            }
            auto it = pParams->Vectors.find( name );
            if ( it != pParams->Vectors.end() ) {
                WriteVector( pOut, it->second );
                continue;
            }
            auto itMatrix = pParams->Matrices.find( name );
            if ( itMatrix != pParams->Matrices.end() ) {
                WriteMatrix( pOut, member.NumRegisters, member.Class, itMatrix->second );
                continue;
            }
            auto itArray = pParams->Arrays.find( name );
            if ( itArray != pParams->Arrays.end() ) {
                for ( uint32_t i = 0; i < member.NumRegisters && i < itArray->second.size(); i++ ) {
                    WriteVector( pOut + i * 4, itArray->second[i] );
                }
                continue;
            }
        }
        if ( pParams != nullptr && pParams->pAvatarLighting != nullptr ) {
            // Avatar light set (instance ambient / directional / spot lights)
            // and the CARINTEG / ATTEFACT symbols written by FUN_00caf850 for
            // every avatar family except TD_Avatar (it keeps its 2DM values).
            const EdenAvatarLighting &light = *pParams->pAvatarLighting;
            const uint64_t family = material.ShaderSetGUID;
            const bool bTdAvatar = family == 0xd5013ed0db79dadbull;
            const bool bSymbols = !bTdAvatar && ( family == 0xd5013ed0db79bbbbull || family == 0xd5013ed0dbcdcdcdull ||
                                                  family == 0xd5013ed0dbcddbcaull || family == 0xd5013ed0db79aeaeull ||
                                                  family == 0xd5013ed0cacadacaull || family == 0xd5013ed0dba9dacaull ||
                                                  family == 0xd5013ed0dbdededeull || family == 0x413ab646426c5358ull );
            bool bHandled = true;
            if ( name == "__DirLight_Dir_0" ) {
                WriteVector( pOut, getInverseWorld().transformVector( light.Direction ).normalized() );
            } else if ( name == "__AmbLight_Ambient_0" ) {
                WriteVector( pOut, Mul( light.Ambient, material.Ambient ), 1.0f );
            } else if ( name == "__DirLight_Diffuse_0" || name == "__DirLight_Diffuse_NoInstance_0" ) {
                WriteVector( pOut, Mul( light.Diffuse, material.Diffuse ), 1.0f );
            } else if ( name == "__DirLight_Specular_0" || name == "__DirLight_Specular_NoInstance_0" ) {
                WriteVector( pOut, Mul( light.Specular, material.Specular ), 1.0f );
            } else if ( name == "__SpotLight_Pos_0" ) {
                WriteVector( pOut, getInverseWorld().transformPoint( light.SpotPosition ), 1.0f );
            } else if ( name == "__SpotLight_Ambient_0" ) {
                // Material colour multiply of the spot light [INF: binder ENC].
                WriteVector( pOut, Mul( light.SpotAmbient, material.Ambient ), 1.0f );
            } else if ( name == "__SpotLight_Diffuse_0" ) {
                WriteVector( pOut, Mul( light.SpotDiffuse, material.Diffuse ), 1.0f );
            } else if ( name == "__SpotLight_Specular_0" ) {
                WriteVector( pOut, Mul( light.SpotSpecular, material.Specular ), 1.0f );
            } else if ( bSymbols && ToUpper( StripSuffix( name ) ) == "CARINTEG" ) {
                // (p1, p2, shadow, 1) with AVATAROUTCARLIGHTFACTOR p1 = p2 = 0:
                // xy = (0, 1) gives the same k = 1 without the 0 * inf of
                // D3D9 (NaN in SPIR-V).
                WriteVector( pOut, Vector4f( 0.0f, 1.0f, light.Shadow, 1.0f ) );
            } else if ( bSymbols && ToUpper( StripSuffix( name ) ) == "ATTEFACT" ) {
                WriteVector( pOut, Vector4f( 1.0f, 1.0f, 1.0f, light.Environment ) );
            } else if ( family == 0xd5013ed0db79baedull && ToUpper( StripSuffix( name ) ) == "SHADOWATTENUATION" ) {
                WriteVector( pOut, Vector4f( light.Shadow, light.Shadow, light.Shadow, 1.0f ) );
            } else {
                bHandled = false;
            }
            if ( bHandled ) {
                continue;
            }
        }
        {
            auto it = scene.Vectors.find( name );
            if ( it != scene.Vectors.end() ) {
                WriteVector( pOut, it->second );
                continue;
            }
            auto itMatrix = scene.Matrices.find( name );
            if ( itMatrix != scene.Matrices.end() ) {
                WriteMatrix( pOut, member.NumRegisters, member.Class, itMatrix->second );
                continue;
            }
        }

        // Matrices
        if ( name == "__World_View_Proj__float4x4" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, worldViewProjection );
        } else if ( name == "__World__float4x4" || name == "LastWorld__float4x4" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, world );
        } else if ( name == "__World_View__float4x4" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, worldView );
        } else if ( name == "__Inv_World__float4x4" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, getInverseWorld() );
        } else if ( name == "__Inv_World_View__float4x4" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, worldView.inverse() );
        } else if ( name == "__T_Inv_World_View__float4x4" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, worldView.inverse().transposed() );
        } else if ( name == "View" || name == "LastView" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View );
        } else if ( name == "Proj" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.Projection );
        } else if ( name == "ViewProj" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, viewProjection );
        } else if ( name == "InvView" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View.inverse() );
        } else if ( name == "InvProj" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.Projection.inverse() );
        } else if ( name == "InvViewProj" ) {
            WriteMatrix( pOut, member.NumRegisters, member.Class, viewProjection.inverse() );
        } else if ( name.rfind( "__UVA_", 0 ) == 0 ) {
            // Texture coordinate animation of slot k (.UVA): register rows
            // (su, 0, 0, offU), (0, sv, 0, offV) [SHADER TD_Water vs 46].
            const uint32_t slot = static_cast<uint32_t>( name[6] - '0' );
            if ( slot < 4 && material.UVAnimations[slot] != nullptr ) {
                Vector4f registers[4];
                material.UVAnimations[slot]->evaluateRegisters( scene.Time, registers );
                for ( uint32_t i = 0; i < member.NumRegisters && i < 4; i++ ) {
                    WriteVector( pOut + i * 4, registers[i] );
                }
            } else {
                WriteMatrix( pOut, member.NumRegisters, member.Class, Matrix4x4f::Identity() );
            }
        } else if ( name == "__Skin_Matrix__array96" ) {
            const uint32_t numBones = member.NumRegisters / 3;
            for ( uint32_t bone = 0; bone < numBones; bone++ ) {
                const Matrix4x4f matrix = ( pParams != nullptr && bone < pParams->NumSkinMatrices )
                                              ? pParams->pSkinMatrices[bone]
                                              : Matrix4x4f::Identity();
                WriteMatrix( pOut + bone * 12, 3, 3, matrix );
            }
        }
        // Camera / lights
        else if ( name == "__CamPos_ObjSpace" ) {
            WriteVector( pOut, getInverseWorld().transformPoint( scene.CameraPosition ), 1.0f );
        } else if ( name == "__CamDir_ObjSpace" ) {
            WriteVector( pOut, getInverseWorld().transformVector( scene.CameraDirection ).normalized() );
        } else if ( name == "__DirLight_Dir_0" ) {
            WriteVector( pOut, getInverseWorld().transformVector( scene.SunDirection ).normalized() );
        } else if ( name == "__DirLight_Dir_1" ) {
            WriteVector( pOut, getInverseWorld().transformVector( Vector3f( 0.0f, -1.0f, 0.0f ) ) );
        } else if ( name == "__DirLight_Diffuse_0" || name == "__DirLight_Diffuse_NoInstance_0" ) {
            WriteVector( pOut, Mul( scene.SunDiffuse, material.Diffuse ), 1.0f );
        } else if ( name == "__DirLight_RawDiffuse_0" ) {
            WriteVector( pOut, scene.SunDiffuse, 1.0f );
        } else if ( name == "__DirLight_Specular_0" || name == "__DirLight_Specular_NoInstance_0" ) {
            WriteVector( pOut, Mul( scene.SunSpecular, material.Specular ), 1.0f );
        } else if ( name == "__DirLight_Emissive_0" || name == "__Material_Emissive" ) {
            WriteVector( pOut, material.Emissive );
        } else if ( name == "__AmbLight_Ambient_0" ) {
            WriteVector( pOut, Mul( scene.Ambient, material.Ambient ), 1.0f );
        } else if ( name == "__AmbLight_RawAmbient_0" ) {
            WriteVector( pOut, scene.Ambient, 1.0f );
        } else if ( name == "__Material_Specular" ) {
            WriteVector( pOut, material.Specular );
        } else if ( name == "__Instance_Color" ) {
            WriteVector( pOut, Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) );
        } else if ( name == "__Alpha_Ref" ) {
            const float reference = material.AlphaRef / 255.0f;
            WriteVector( pOut, Vector4f( reference, reference, reference, reference ) );
        } else if ( name.rfind( "__SpotLight_", 0 ) == 0 || name.rfind( "__AmbLight_", 0 ) == 0 ||
                    name.rfind( "__DirLight_", 0 ) == 0 || name.rfind( "Shadow_", 0 ) == 0 ) {
            // Second lights, spot lights, real time shadows: disabled
        } else if ( name == "__Lod_Dist" ) {
            WriteVector( pOut, Vector4f( 0.0f, 1.0f, 0.0f, 1.0f ) );
        }
        // Frame / scene
        else if ( name.rfind( "UserVector", 0 ) == 0 ) {
            const uint32_t index = static_cast<uint32_t>( name[10] - '0' );
            if ( index < 8 ) {
                WriteVector( pOut, scene.UserVectors[index] );
            }
        } else if ( name == "Time_" ) {
            WriteVector( pOut, Vector4f( scene.Time, scene.Time * 0.5f, scene.Time * 0.25f, 1.0f ) );
        } else if ( name == "FogColor" ) {
            WriteVector( pOut, scene.FogColor, 1.0f );
        } else if ( name == "FogConfig" ) {
            WriteVector( pOut, scene.FogConfig );
        } else if ( name == "RenderTargetSize" ) {
            // xy: pixel to UV (shaders sample screen space buffers with vPos * xy)
            WriteVector( pOut, Vector4f( 1.0f / scene.Width, 1.0f / scene.Height, static_cast<float>( scene.Width ),
                                         static_cast<float>( scene.Height ) ) );
        } else if ( name == "StereoUvMadd" ) {
            WriteVector( pOut, Vector4f( 1.0f, 1.0f, 0.0f, 0.0f ) );
        } else {
            // Material parameter (.2DM): Eden identifier of the upper case name.
            const std::string baseName = StripSuffix( name );
            auto it = material.Parameters.find( GetIdentifier64bit( baseName.c_str() ) );
            if ( it != material.Parameters.end() ) {
                WriteVector( pOut, it->second );
            } else if ( Logger::IsVerbose() ) {
                const std::string warnKey = std::string( pProgramName ) + "|" + name;
                if ( warnedParameters.insert( warnKey ).second ) {
                    OTDU_LOG_DEBUG( "Eden: no value for '%s' (%s, material '%s')\n", name.c_str(), pProgramName,
                                    material.Name.c_str() );
                }
            }
        }
    }
}

void EdenRenderer::recordDraw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                               const EdenDrawParams *pParams ) {
    const EdenProgram *pProgram = material.pProgram;
    const float determinant =
        world.m[0][0] * ( world.m[1][1] * world.m[2][2] - world.m[1][2] * world.m[2][1] ) -
        world.m[0][1] * ( world.m[1][0] * world.m[2][2] - world.m[1][2] * world.m[2][0] ) +
        world.m[0][2] * ( world.m[1][0] * world.m[2][1] - world.m[1][1] * world.m[2][0] );
    VkPipeline pipeline = getPipeline( mesh, material, ( determinant < 0.0f ) != scene.bMirroredView );
    if ( pipeline == VK_NULL_HANDLE ) {
        return;
    }
    VkDevice device = pDevice->getDevice();
    VkCommandBuffer commandBuffer = pDevice->getCommandBuffer();
    const VkDeviceSize alignment = pDevice->getUniformBufferAlignment();

    TransientAllocation vsConstants = pDevice->allocateTransient( kVSConstantsSize, alignment );
    TransientAllocation psConstants = pDevice->allocateTransient( kPSConstantsSize, alignment );
    TransientAllocation specConstants = pDevice->allocateTransient( kSpecDwords * 4, alignment );
    if ( vsConstants.pData == nullptr || psConstants.pData == nullptr || specConstants.pData == nullptr ) {
        return;
    }
    memset( vsConstants.pData, 0, kVSConstantsSize );
    memset( psConstants.pData, 0, kPSConstantsSize );
    fillConstants( pProgram->VSMembers,
                   reinterpret_cast<float *>( static_cast<uint8_t *>( vsConstants.pData ) + kIntRegistersSize ),
                   kVSFloatRegisters, material, world, pParams, pProgram->Name.c_str(), false );
    fillConstants( pProgram->PSMembers,
                   reinterpret_cast<float *>( static_cast<uint8_t *>( psConstants.pData ) + kIntRegistersSize ),
                   kPSFloatRegisters, material, world, pParams, pProgram->Name.c_str(), true );
    static const bool bDumpConstants = CmdLine::HasArg( "-edendumpconstants" );
    if ( bDumpConstants && dumpedPrograms.insert( pProgram->Name ).second ) {
        const float *pVS = reinterpret_cast<const float *>( static_cast<uint8_t *>( vsConstants.pData ) + kIntRegistersSize );
        const float *pPS = reinterpret_cast<const float *>( static_cast<uint8_t *>( psConstants.pData ) + kIntRegistersSize );
        std::string text;
        char line[160];
        for ( uint32_t r = 0; r < kVSFloatRegisters; r++ ) {
            if ( pVS[r * 4] != 0.0f || pVS[r * 4 + 1] != 0.0f || pVS[r * 4 + 2] != 0.0f || pVS[r * 4 + 3] != 0.0f ) {
                snprintf( line, sizeof( line ), "  vs c%u = %g %g %g %g\n", r, pVS[r * 4], pVS[r * 4 + 1], pVS[r * 4 + 2],
                          pVS[r * 4 + 3] );
                text += line;
            }
        }
        for ( uint32_t r = 0; r < kPSFloatRegisters; r++ ) {
            if ( pPS[r * 4] != 0.0f || pPS[r * 4 + 1] != 0.0f || pPS[r * 4 + 2] != 0.0f || pPS[r * 4 + 3] != 0.0f ) {
                snprintf( line, sizeof( line ), "  ps c%u = %g %g %g %g\n", r, pPS[r * 4], pPS[r * 4 + 1], pPS[r * 4 + 2],
                          pPS[r * 4 + 3] );
                text += line;
            }
        }
        OTDU_LOG_INFO( "Eden constants %s (%s):\n%s", pProgram->Name.c_str(), material.Name.c_str(), text.c_str() );
    }
    uint32_t *pSpec = static_cast<uint32_t *>( specConstants.pData );
    memset( pSpec, 0, kSpecDwords * 4 );
    const uint32_t alphaCompareOp =
        material.bAlphaTest ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
    pSpec[1] = alphaCompareOp << kSpecAlphaCompareOpShift;

    VkDescriptorSet set = allocateFrameSet();
    if ( set == VK_NULL_HANDLE ) {
        return;
    }

    const VkDescriptorBufferInfo bufferInfos[5] = {
        { vsConstants.Buffer, vsConstants.Offset, kVSConstantsSize },
        { pZeroBuffer->Buffer, 0, 6 * 16 },
        { psConstants.Buffer, psConstants.Offset, kPSConstantsSize },
        { pZeroBuffer->Buffer, 0, 8 * 64 },
        { specConstants.Buffer, specConstants.Offset, kSpecDwords * 4 },
    };
    const uint32_t bufferBindings[5] = { 0, 3, 10, 12, 31 };
    VkDescriptorImageInfo imageInfos[kNumVSImages + kNumPSImages] = {};
    auto pickTexture = [&]( uint8_t samplerType, GPUTexture *pTexture ) -> GPUTexture * {
        const VkImageViewType wanted = samplerType == 3   ? VK_IMAGE_VIEW_TYPE_CUBE
                                       : samplerType == 4 ? VK_IMAGE_VIEW_TYPE_3D
                                                          : VK_IMAGE_VIEW_TYPE_2D;
        if ( pTexture != nullptr && pTexture->ViewType == wanted ) {
            return pTexture;
        }
        if ( pTexture != nullptr && Logger::IsVerbose() ) {
            const std::string key = material.Name + "|" + pTexture->Name;
            if ( warnedParameters.insert( key ).second ) {
                OTDU_LOG_DEBUG( "Eden: material '%s' (%s): texture '%s' view type %d, sampler wants %d\n",
                                material.Name.c_str(), pProgram->Name.c_str(), pTexture->Name.c_str(),
                                static_cast<int32_t>( pTexture->ViewType ), static_cast<int32_t>( wanted ) );
            }
        }
        return wanted == VK_IMAGE_VIEW_TYPE_CUBE ? pWhiteCube : wanted == VK_IMAGE_VIEW_TYPE_3D ? pWhite3D : pWhite2D;
    };
    for ( uint32_t i = 0; i < kNumVSImages; i++ ) {
        GPUTexture *pTexture = pickTexture( pProgram->VSSamplerTypes[i], nullptr );
        imageInfos[i].imageView = pTexture->View;
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    for ( uint32_t i = 0; i < kNumPSImages; i++ ) {
        GPUTexture *pSource = material.pTextures[i];
        if ( pSource == nullptr && i == 15 ) {
            pSource = pShadowRealTimeOverride != nullptr ? pShadowRealTimeOverride : pShadowRealTime;
        } else if ( pSource == nullptr && pScreenTexture != nullptr && i < material.NumSlots &&
                    material.TextureHashes[i] == 1ull && material.ShaderSetGUID == 0x132435465768798aull ) {
            // Runtime screen buffer slots (TD_ShadowBox gViewPos...)
            pSource = pScreenTexture;
        } else if ( pSource == nullptr && material.ProjectiveSlot == static_cast<int8_t>( i ) ) {
            // GSReflection projective map (FUN_00d7c030: the RT colour of
            // the REFLECT layers, else a default texture).
            pSource = pProjectiveTexture != nullptr ? pProjectiveTexture : pBlack2D;
        } else if ( pSource == nullptr && material.bNormalSlot[i] ) {
            pSource = pFlatNormal;
        }
        static const bool bChecker = CmdLine::HasArg( "-edenchecker" );
        if ( bChecker && i != 15 ) {
            pSource = pTextureCache->getCheckerTexture();
        }
        GPUTexture *pTexture = pickTexture( pProgram->PSSamplerTypes[i], pSource );
        imageInfos[kNumVSImages + i].imageView = pTexture->View;
        imageInfos[kNumVSImages + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet writes[5 + 2] = {};
    for ( uint32_t i = 0; i < 5; i++ ) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = bufferBindings[i];
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = set;
    writes[5].dstBinding = kVSImageBinding;
    writes[5].descriptorCount = kNumVSImages;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].pImageInfo = imageInfos;
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = set;
    writes[6].dstBinding = kPSImageBinding;
    writes[6].descriptorCount = kNumPSImages;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[6].pImageInfo = imageInfos + kNumVSImages;
    vkUpdateDescriptorSets( device, 7, writes, 0, nullptr );

    uint8_t pushData[RenderDevice::kPushConstantsSize] = {};
    const uint32_t alphaRef = material.AlphaRef;
    memcpy( pushData + kPushAlphaRefOffset, &alphaRef, sizeof( alphaRef ) );
    for ( uint32_t i = 0; i < 16; i++ ) {
        const uint16_t samplerIndex = material.SamplerIndex[i];
        memcpy( pushData + kPushSamplerOffset + i * 2, &samplerIndex, sizeof( samplerIndex ) );
    }

    const VkDescriptorSet sets[2] = { set, samplerSet };
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0, nullptr );
    vkCmdPushConstants( commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof( pushData ), pushData );

    VkBuffer vertexBuffers[EdenMesh::kMaxStreams + 1];
    VkDeviceSize offsets[EdenMesh::kMaxStreams + 1];
    for ( uint32_t i = 0; i < EdenMesh::kMaxStreams; i++ ) {
        if ( i < mesh.NumStreams && mesh.pStreamBuffers[i] != nullptr ) {
            vertexBuffers[i] = mesh.pStreamBuffers[i]->Buffer;
            offsets[i] = mesh.StreamOffsets[i];
        } else {
            vertexBuffers[i] = pDefaultAttributes->Buffer;
            offsets[i] = 0;
        }
    }
    vertexBuffers[EdenMesh::kMaxStreams] = pDefaultAttributes->Buffer;
    offsets[EdenMesh::kMaxStreams] = 0;
    vkCmdBindVertexBuffers( commandBuffer, 0, EdenMesh::kMaxStreams + 1, vertexBuffers, offsets );
    vkCmdBindIndexBuffer( commandBuffer, mesh.pIndexBuffer->Buffer, 0, VK_INDEX_TYPE_UINT32 );
    vkCmdDrawIndexed( commandBuffer, mesh.IndexCount, 1, mesh.FirstIndex, mesh.VertexOffset, 0 );
    numDrawCalls++;
}

VkDescriptorSet EdenRenderer::allocateFrameSet() {
    // A frame can need more sets than one pool holds (environment face, Z
    // pre-pass, 3 shadow slices, reflection, scene): further pools are
    // created on demand instead of dropping the draws.
    VkDevice device = pDevice->getDevice();
    const uint32_t slot = pDevice->getFrameSlot();
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayouts[0];
    VkDescriptorSet set = VK_NULL_HANDLE;
    // Pools are switched on the set count: some drivers (lavapipe) do not
    // fail past maxSets and corrupt their pool instead.
    const uint32_t poolIndex = setsAllocated[slot] / kMaxSetsPerFrame;
    setsAllocated[slot]++;
    if ( poolIndex <= overflowUsed[slot] ) {
        allocInfo.descriptorPool =
            overflowUsed[slot] == 0 ? framePools[slot] : overflowPools[slot][overflowUsed[slot] - 1];
        if ( vkAllocateDescriptorSets( device, &allocInfo, &set ) == VK_SUCCESS ) {
            return set;
        }
    }
    if ( overflowUsed[slot] == overflowPools[slot].size() ) {
        const VkDescriptorPoolSize frameSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSetsPerFrame * 5 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSetsPerFrame * ( kNumVSImages + kNumPSImages ) },
        };
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxSetsPerFrame;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = frameSizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if ( vkCreateDescriptorPool( device, &poolInfo, nullptr, &pool ) != VK_SUCCESS ) {
            OTDU_LOG_ERROR( "Eden: descriptor pool creation failed\n" );
            return VK_NULL_HANDLE;
        }
        overflowPools[slot].push_back( pool );
        OTDU_LOG_INFO( "Eden: descriptor pool %zu added (frame slot %u)\n", overflowPools[slot].size(), slot );
    }
    allocInfo.descriptorPool = overflowPools[slot][overflowUsed[slot]++];
    if ( vkAllocateDescriptorSets( device, &allocInfo, &set ) != VK_SUCCESS ) {
        return VK_NULL_HANDLE;
    }
    return set;
}

const EdenMaterial *EdenRenderer::getZPassMaterial( const EdenMaterial &material ) {
    auto it = zPassMaterials.find( &material );
    if ( it != zPassMaterials.end() && it->second.first == material.Hashcode ) {
        return it->second.second.get();
    }
    std::unique_ptr<EdenMaterial> pZPass;
    if ( material.pProgram != nullptr && material.Spec[EdenShaderSystem::S_PASS_Z] == 0 ) {
        pZPass = std::make_unique<EdenMaterial>( material );
        pZPass->Spec[EdenShaderSystem::S_PASS_Z] = 1;
        updateProgram( *pZPass );
        // A family without S_PASS_Z permutations resolves to the same item.
        if ( pZPass->pProgram == nullptr || pZPass->pProgram->ItemId == material.pProgram->ItemId ) {
            pZPass.reset();
        } else {
            pZPass->SrcBlend = 2;
            pZPass->DstBlend = 1;
            pZPass->BlendOp = 1;
            pZPass->bDepthTest = true;
            pZPass->bDepthWrite = true;
        }
    }
    if ( Logger::IsVerbose() ) {
        OTDU_LOG_DEBUG( "Eden: Z pass of '%s' (%s): %s\n", material.Name.c_str(),
                        material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-",
                        pZPass != nullptr ? pZPass->pProgram->Name.c_str() : "none" );
    }
    EdenMaterial *pResult = pZPass.get();
    zPassMaterials[&material] = { material.Hashcode, std::move( pZPass ) };
    return pResult;
}

const EdenMaterial *EdenRenderer::getCasterMaterial( const EdenMaterial &material ) {
    auto it = casterMaterials.find( &material );
    if ( it != casterMaterials.end() && it->second.first == material.Hashcode ) {
        return it->second.second.get();
    }
    std::unique_ptr<EdenMaterial> pCaster;
    ShaderLibrary *pLibrary = shaderSystem.findLibrary( material.ShaderSetGUID );
    const std::string family = pLibrary != nullptr ? ToUpper( pLibrary->getName() ) : std::string();
    if ( material.pProgram == nullptr || material.Spec[EdenShaderSystem::S_USESKINNING] != 0 ||
         family == "TD_HEIGHTMAP" || family == "TD_GROUND_HM" || material.ShaderSetGUID == 0x0cea000000000002ull ) {
        // Skinned meshes (no skinned caster here) and the heightfields (their
        // vertex shader rebuilds the grid, self shadowing is in the sector
        // shadow maps): no caster.
    } else if ( family == "TD_VEGETATION" || family == "TD_VEGETATIONSPR" || family == "EDRENDER_GOURAUD" ) {
        // Families with their own S_PASS_SHADOWCAST permutations (wind,
        // billboard leaves).
        pCaster = std::make_unique<EdenMaterial>( material );
        pCaster->Spec[EdenShaderSystem::S_PASS_SHADOWCAST] = 1;
        // The caster writes the light depth: no blending.
        pCaster->SrcBlend = 2;
        pCaster->DstBlend = 1;
        updateProgram( *pCaster );
    } else {
        // TD_ShadowCaster (ac78df32bc4589ff): vs_4 / ps_1, alpha tested
        // vs_5 / ps_2 (texkill( base.a - __Alpha_Ref )).
        pCaster = std::make_unique<EdenMaterial>();
        pCaster->Name = material.Name + " (caster)";
        pCaster->Hashcode = material.Hashcode;
        pCaster->ShaderSetGUID = 0xac78df32bc4589ffull;
        pCaster->CullMode = material.CullMode;
        pCaster->AlphaRef = material.AlphaRef;
        pCaster->bAlphaTest = material.bAlphaTest;
        if ( material.bAlphaTest ) {
            pCaster->pTextures[0] = material.pTextures[0];
            pCaster->SamplerIndex[0] = material.SamplerIndex[0];
            pCaster->pProgram = shaderSystem.getProgram( "TD_ShadowCaster", 5, 2 );
        } else {
            pCaster->pProgram = shaderSystem.getProgram( "TD_ShadowCaster", 4, 1 );
        }
    }
    if ( pCaster != nullptr ) {
        pCaster->SrcBlend = 2;
        pCaster->DstBlend = 1;
        pCaster->BlendOp = 1;
        pCaster->ColorWriteMask = 0xf;
        pCaster->bDepthTest = true;
        pCaster->bDepthWrite = true;
        pCaster->DepthFunc = 0;
    }
    EdenMaterial *pResult = pCaster.get();
    casterMaterials[&material] = { material.Hashcode, std::move( pCaster ) };
    return pResult;
}
