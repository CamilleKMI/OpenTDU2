#pragma once

#include "render/vulkan/render_device.h"

// Fullscreen triangle passes with OpenTDU2 GLSL fragment shaders (texture
// copies, depth -> view position buffer...). Records into the render pass
// being recorded on the device (pipelines are cached per render pass).
class FullscreenPass {
public:
    FullscreenPass();
    ~FullscreenPass();

    void destroy();

    void draw( RenderDevice &device,
               const uint32_t *pFragmentSPIRV,
               size_t fragmentSize,
               GPUTexture *const *ppTextures,
               uint32_t numTextures,
               const void *pPushConstants = nullptr,
               uint32_t pushConstantsSize = 0,
               eBlendMode blend = eBlendMode::BM_Opaque,
               eSamplerFilter filter = eSamplerFilter::SF_Linear );

private:
    RenderDevice *pDevice;
    std::unordered_map<std::string, VkPipeline> pipelines;
};
