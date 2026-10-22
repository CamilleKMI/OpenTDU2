#include "shared.h"
#include "fullscreen_pass.h"

#include "render/shaders/builtin/fullscreen_vert.h"

FullscreenPass::FullscreenPass() : pDevice( nullptr ) {}

FullscreenPass::~FullscreenPass() { destroy(); }

void FullscreenPass::destroy() {
    if ( pDevice != nullptr ) {
        for ( auto &entry : pipelines ) {
            pDevice->destroyPipeline( entry.second );
        }
    }
    pipelines.clear();
    pDevice = nullptr;
}

void FullscreenPass::draw( RenderDevice &device, const uint32_t *pFragmentSPIRV, size_t fragmentSize,
                           GPUTexture *const *ppTextures, uint32_t numTextures, const void *pPushConstants,
                           uint32_t pushConstantsSize, eBlendMode blend, eSamplerFilter filter ) {
    pDevice = &device;
    const VkRenderPass renderPass = device.getCurrentRenderPass();
    if ( renderPass == VK_NULL_HANDLE || numTextures == 0 ) {
        return;
    }
    char key[96];
    snprintf( key, sizeof( key ), "%p:%p:%u", static_cast<const void *>( pFragmentSPIRV ),
              static_cast<void *>( renderPass ), static_cast<uint32_t>( blend ) );
    VkPipeline pipeline = VK_NULL_HANDLE;
    auto it = pipelines.find( key );
    if ( it != pipelines.end() ) {
        pipeline = it->second;
    } else {
        PipelineDesc desc;
        desc.pVertexSPIRV = kSPIRV_fullscreen_vert;
        desc.VertexSPIRVSize = sizeof( kSPIRV_fullscreen_vert );
        desc.pFragmentSPIRV = pFragmentSPIRV;
        desc.FragmentSPIRVSize = fragmentSize;
        desc.Blend = blend;
        desc.Cull = eCullMode::CM_None;
        desc.RenderPass = renderPass;
        pipeline = device.createPipeline( desc );
        pipelines[key] = pipeline;
    }
    if ( pipeline == VK_NULL_HANDLE ) {
        return;
    }
    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    VkDescriptorSet set =
        device.allocateTextureSet( ppTextures, numTextures, device.getSampler( filter, eSamplerAddress::SA_Clamp ) );
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device.getDefaultPipelineLayout(), 0, 1,
                             &set, 0, nullptr );
    if ( pPushConstants != nullptr && pushConstantsSize > 0 ) {
        vkCmdPushConstants( commandBuffer, device.getDefaultPipelineLayout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            Min<uint32_t>( pushConstantsSize, RenderDevice::kPushConstantsSize ), pPushConstants );
    }
    vkCmdDraw( commandBuffer, 3, 1, 0, 0 );
}
