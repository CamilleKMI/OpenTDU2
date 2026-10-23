#pragma once

#include "render/render_api.h"
#include "render/vulkan/vulkan_include.h"

struct SDL_Window;

struct GPUTexture {
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t NumMips = 1;
    VkImageViewType ViewType = VK_IMAGE_VIEW_TYPE_2D;
    std::string Name;
};

struct GPUBuffer {
    VkBuffer Buffer = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    void *pMapped = nullptr;
    VkDeviceSize Size = 0;
};

// Sub-allocation from the per-frame host visible ring buffer (dynamic vertex,
// index and uniform data). Valid until the frame is presented.
struct TransientAllocation {
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceSize Offset = 0;
    void *pData = nullptr;
};

// Offscreen render target: color (sampled after the pass) + optional depth
// (sampled after the pass through a depth only view). The render passes of
// render targets in the main formats are compatible with the main render
// pass, so the same pipelines can be used.
struct RenderTarget {
    GPUTexture Color;
    GPUTexture Depth;                               // View: depth aspect, sampled
    VkImageView DepthAttachmentView = VK_NULL_HANDLE; // Depth (+ stencil) aspect
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool bHasDepth = false;
    bool bInitialized = false; // Contents defined (a pass can load them)
    // Cube targets: one attachment view / framebuffer per face (Color.View
    // is the cube view), the depth buffer is shared by the faces.
    uint32_t NumLayers = 1;
    uint32_t InitializedLayers = 0; // Bit mask
    VkImageView LayerViews[6] = {};
    VkFramebuffer LayerFramebuffers[6] = {};
};

enum class eBlendMode : uint8_t {
    BM_Opaque = 0,
    BM_Alpha,
    BM_PremultipliedAlpha,
    BM_Additive,
};

enum class eCullMode : uint8_t {
    CM_None = 0,
    CM_Back,
    CM_Front,
};

struct VertexAttributeDesc {
    uint32_t Location;
    uint32_t Binding;
    VkFormat Format;
    uint32_t Offset;
};

// Stencil use of a pipeline (flash masks: ClipDepth layers).
enum class eStencilMode : uint8_t {
    SM_None = 0,
    SM_Increment, // mask shape: stencil + 1, no colour
    SM_Decrement, // mask shape removed: stencil - 1, no colour
    SM_TestEqual, // masked content: stencil == reference
    SM_Count
};

struct PipelineDesc {
    const uint32_t *pVertexSPIRV = nullptr;
    size_t VertexSPIRVSize = 0; // in bytes
    const uint32_t *pFragmentSPIRV = nullptr;
    size_t FragmentSPIRVSize = 0; // in bytes

    std::vector<VertexAttributeDesc> Attributes;
    std::vector<uint32_t> BindingStrides;
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    eBlendMode Blend = eBlendMode::BM_Opaque;
    eCullMode Cull = eCullMode::CM_None;
    bool bDepthTest = false;
    bool bDepthWrite = false;
    bool bFrontFaceCW = false;
    eStencilMode Stencil = eStencilMode::SM_None; // reference: dynamic state
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE; // Main render pass if null
};

enum class eSamplerFilter : uint8_t { SF_Linear = 0, SF_Point };
enum class eSamplerAddress : uint8_t { SA_Clamp = 0, SA_Wrap, SA_Mirror };

class RenderDevice {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxTexturesPerSet = 4;
    static constexpr uint32_t kPushConstantsSize = 128;

    inline VkDevice getDevice() const { return device; }
    inline VkCommandBuffer getCommandBuffer() const {
        return frames[frameIndex].CommandBuffer;
    }
    inline uint32_t getBackbufferWidth() const { return swapchainExtent.width; }
    inline uint32_t getBackbufferHeight() const { return swapchainExtent.height; }
    inline VkRenderPass getMainRenderPass() const { return mainRenderPass; }
    inline VkPipelineLayout getDefaultPipelineLayout() const {
        return defaultPipelineLayout;
    }
    inline bool supportsBC() const { return bSupportsBC; }
    // Features required by the SPIR-V produced from the original D3D9 shaders
    // (.sob -> DXVK DXSO): runtime descriptor arrays, demote, clip distances.
    inline bool supportsDxsoShaders() const { return bSupportsDxso; }
    inline uint32_t getFrameSlot() const { return frameIndex; }
    inline VkDeviceSize getUniformBufferAlignment() const { return uniformBufferAlignment; }
    inline uint64_t getFrameNumber() const { return frameNumber; }
    inline const char *getDeviceName() const { return deviceName.c_str(); }

public:
    RenderDevice();
    ~RenderDevice();

    bool create( SDL_Window *pWindow, bool bVSync, bool bValidation );
    void destroy();
    void waitIdle();

    // Frame flow. beginFrame returns false if the swapchain is not ready
    // (e.g. minimized window).
    // beginFrame opens the command buffer; the main (swapchain) render pass
    // starts with beginMainPass (or at endFrame). Offscreen render targets
    // are drawn in between.
    bool beginFrame( const ColorRGBA &clearColor );
    void beginMainPass();
    void endFrame();
    inline bool isInMainPass() const { return bInMainPass; }

    // Offscreen render targets (colorFormat VK_FORMAT_UNDEFINED: swapchain
    // format).
    RenderTarget *createRenderTarget( uint32_t width, uint32_t height, VkFormat colorFormat, bool bDepth,
                                      const char *pName );
    RenderTarget *createCubeRenderTarget( uint32_t size, bool bDepth, const char *pName );
    void destroyRenderTarget( RenderTarget *pTarget );
    void beginRenderTarget( RenderTarget *pTarget, bool bClear, const ColorRGBA &clearColor = ColorRGBA(),
                            uint32_t layer = 0 );
    void endRenderTarget();
    // Clears the depth of the render target being drawn (inside its pass).
    void clearRenderTargetDepth();
    // Copies a region of a render target depth (D32_SFLOAT) to a host
    // visible buffer (outside render passes; read it once the frame slot
    // fence has been waited, i.e. kFramesInFlight frames later).
    bool copyRenderTargetDepth( RenderTarget *pTarget, int32_t x, int32_t y, uint32_t width, uint32_t height,
                                GPUBuffer *pDestination );
    // Render pass of the pass being recorded (pipelines are created for it).
    inline VkRenderPass getCurrentRenderPass() const { return currentRenderPass; }
    inline VkFormat getSwapchainFormat() const { return swapchainFormat; }
    void requestSwapchainRecreation() { bSwapchainDirty = true; }

    // Resources
    GPUTexture *createTexture2D( uint32_t width,
                                 uint32_t height,
                                 uint32_t numMips,
                                 VkFormat format,
                                 const void *const *ppMipData,
                                 const uint32_t *pMipSizes,
                                 const char *pName );
    // Generic texture (2D, cube: 6 layers, 3D). ppData/pSizes: numLayers x
    // numMips entries, layer major (one entry per mip for volumes).
    GPUTexture *createTexture( VkImageViewType viewType,
                               uint32_t width,
                               uint32_t height,
                               uint32_t depth,
                               uint32_t numLayers,
                               uint32_t numMips,
                               VkFormat format,
                               const void *const *ppData,
                               const uint32_t *pSizes,
                               const char *pName );
    void destroyTexture( GPUTexture *pTexture );
    inline VkRenderPass getRenderPass() const { return mainRenderPass; }
    inline VkPipelineCache getPipelineCache() const { return pipelineCache; }
    inline VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }

    GPUBuffer *createBuffer( VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             const void *pInitialData );
    void destroyBuffer( GPUBuffer *pBuffer );

    TransientAllocation allocateTransient( VkDeviceSize size,
                                           VkDeviceSize alignment = 16 );

    VkSampler getSampler( eSamplerFilter filter, eSamplerAddress address ) {
        return getSampler( filter, address, address );
    }
    VkSampler getSampler( eSamplerFilter filter, eSamplerAddress addressU, eSamplerAddress addressV );

    // Default pipeline layout: set 0 = kMaxTexturesPerSet combined image
    // samplers (fragment), push constants = kPushConstantsSize bytes.
    VkDescriptorSet allocateTextureSet( GPUTexture *const *ppTextures,
                                        uint32_t numTextures,
                                        VkSampler sampler );

    VkPipeline createPipeline( const PipelineDesc &desc );
    void destroyPipeline( VkPipeline pipeline );

    // Screenshot of the last presented frame (debug / automated tests).
    bool saveScreenshot( const char *pPath );
    void requestScreenshot( const char *pPath ) { pendingScreenshot = pPath; }

private:
    struct FrameData {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        GPUBuffer TransientBuffer;
        VkDeviceSize TransientOffset = 0;
        std::vector<GPUTexture *> PendingTextureDeletes;
        std::vector<GPUBuffer *> PendingBufferDeletes;
        // -lumalog: rows of the presented image (diagnostics).
        GPUBuffer *pLumaProbe = nullptr;
        uint64_t LumaFrame = 0;
        bool bLumaPending = false;
    };

    SDL_Window *pWindow;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VmaAllocator allocator;
    std::string deviceName;

    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    GPUTexture depthTexture;
    VkRenderPass mainRenderPass;
    bool bVSync;
    bool bSwapchainDirty;
    bool bSupportsBC;
    bool bSupportsDxso = false;
    VkDeviceSize uniformBufferAlignment = 256;
    bool bFrameStarted;
    bool bInMainPass = false;
    ColorRGBA frameClearColor;
    VkRenderPass currentRenderPass = VK_NULL_HANDLE;
    RenderTarget *pCurrentTarget = nullptr;
    std::unordered_map<uint64_t, VkRenderPass> targetRenderPasses;

    FrameData frames[kFramesInFlight];
    uint32_t frameIndex;
    uint32_t imageIndex;
    uint64_t frameNumber;

    VkCommandPool uploadCommandPool;
    VkDescriptorSetLayout textureSetLayout;
    VkPipelineLayout defaultPipelineLayout;
    VkPipelineCache pipelineCache;
    std::unordered_map<uint32_t, VkSampler> samplers;
    std::string pendingScreenshot;

private:
    bool createInstance( bool bValidation );
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool createMainRenderPass();
    VkRenderPass getTargetRenderPass( VkFormat colorFormat, VkFormat depthFormat, bool bClear );
    bool createFrameResources();
    void flushPendingDeletes( FrameData &frame );
    VkCommandBuffer beginImmediateCommands();
    void endImmediateCommands( VkCommandBuffer commandBuffer );
    void captureSwapchainImage( const char *pPath );
};
