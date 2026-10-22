#pragma once

#include "render/shaders/shader_library.h"
#include "render/vulkan/render_device.h"

// Renders the 2D/flash layer with the game's own shaders (Euro/Shaders/
// TD_Flash.sob), translated at load time from D3D9 bytecode to SPIR-V through
// DXVK's DXSO compiler. Reproduces the D3D9 constant registers the shaders
// expect and DXVK's resource binding model:
//
//   set 0: b0  VS constants (int4 i[16], float4 c[256])
//          b3  clip planes
//          b10 PS constants (int4 i[16], float4 c[224])
//          b12 shared PS state (texture stage bump env)
//          b13 sampler 0 image (separate sampled image)
//          b31 specialization state UBO (sampler types, alpha test op...)
//   set 1: b0  sampler heap (runtime array, indexed from push constants)
//   push constants: D3D9RenderStateInfo (fog, alpha ref, point size, sampler indices)
//
// TD_Flash permutations used (see docs/formats.md):
//   VS #2: position + color + texcoord, 2D transform c60/c61, projection c17-c20
//   PS #3: texld s0 * color (generic textured quad)
//   PS #7: alpha = texld s0 .r * color.a, rgb = color (font glyph atlases:
//          the fla_<font>.2db pages are white on black DXT1 without alpha)
class FlashShaderPipeline {
public:
    inline bool isReady() const { return bReady; }

public:
    FlashShaderPipeline();
    ~FlashShaderPipeline();

    bool initialize( RenderDevice *pDevice );
    void destroy();

    // Binds the pipeline and the resources for a draw. pProjection is the
    // column-major orthographic matrix used by the replacement shader (canvas
    // to clip space); it is converted to the D3D9 constants expected by TD_Flash.
    bool bind( VkCommandBuffer commandBuffer, GPUTexture *pTexture, const float *pProjection,
               eStencilMode stencil = eStencilMode::SM_None, uint32_t stencilReference = 0, bool bFont = false );

private:
    RenderDevice *pDevice;
    ShaderLibrary library;
    VkDescriptorSetLayout setLayouts[2];
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkPipeline stencilPipelines[static_cast<size_t>( eStencilMode::SM_Count )] = {};
    VkPipeline fontPipelines[static_cast<size_t>( eStencilMode::SM_Count )] = {};
    VkDescriptorPool staticPool;
    VkDescriptorPool framePools[RenderDevice::kFramesInFlight];
    uint64_t framePoolNumbers[RenderDevice::kFramesInFlight];
    VkDescriptorSet samplerSet;
    GPUBuffer *pZeroBuffer;  // clip planes, PS constants, shared PS state
    GPUBuffer *pSpecBuffer;  // spec_state UBO
    bool bReady;
};
