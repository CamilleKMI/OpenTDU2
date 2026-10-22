#include "shared.h"
#include "render_device.h"
#include "system/cmd_line.h"

#include <SDL3/SDL_vulkan.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace {
constexpr VkDeviceSize kTransientBufferSize = 512ull * 1024ull * 1024ull; // Z pre-pass + 3 shadow slices + scene
constexpr uint32_t kMaxDescriptorSetsPerFrame = 8192;

VKAPI_ATTR VkBool32 VKAPI_CALL
DebugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
               VkDebugUtilsMessageTypeFlagsEXT,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void * ) {
    if ( severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
        OTDU_LOG_ERROR( "[Vulkan] %s\n", pCallbackData->pMessage );
    } else if ( severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
        OTDU_LOG_WARN( "[Vulkan] %s\n", pCallbackData->pMessage );
    }
    return VK_FALSE;
}

bool HasExtension( const std::vector<VkExtensionProperties> &extensions,
                   const char *pName ) {
    for ( const auto &extension : extensions ) {
        if ( strcmp( extension.extensionName, pName ) == 0 ) {
            return true;
        }
    }
    return false;
}

VkFormat FindDepthFormat( VkPhysicalDevice physicalDevice ) {
    const VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT,
                                    VK_FORMAT_D32_SFLOAT_S8_UINT,
                                    VK_FORMAT_D24_UNORM_S8_UINT,
                                    VK_FORMAT_D16_UNORM };
    for ( VkFormat format : candidates ) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties( physicalDevice, format, &properties );
        if ( properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
            return format;
        }
    }
    return VK_FORMAT_D16_UNORM;
}
} // namespace

RenderDevice::RenderDevice()
    : pWindow( nullptr ),
      instance( VK_NULL_HANDLE ),
      debugMessenger( VK_NULL_HANDLE ),
      surface( VK_NULL_HANDLE ),
      physicalDevice( VK_NULL_HANDLE ),
      device( VK_NULL_HANDLE ),
      graphicsQueue( VK_NULL_HANDLE ),
      graphicsQueueFamily( 0 ),
      allocator( VK_NULL_HANDLE ),
      swapchain( VK_NULL_HANDLE ),
      swapchainFormat( VK_FORMAT_UNDEFINED ),
      swapchainExtent{ 0, 0 },
      mainRenderPass( VK_NULL_HANDLE ),
      bVSync( true ),
      bSwapchainDirty( false ),
      bSupportsBC( false ),
      bFrameStarted( false ),
      frameIndex( 0 ),
      imageIndex( 0 ),
      frameNumber( 0 ),
      uploadCommandPool( VK_NULL_HANDLE ),
      textureSetLayout( VK_NULL_HANDLE ),
      defaultPipelineLayout( VK_NULL_HANDLE ),
      pipelineCache( VK_NULL_HANDLE ) {}

RenderDevice::~RenderDevice() { destroy(); }

bool RenderDevice::createInstance( bool bValidation ) {
    PFN_vkGetInstanceProcAddr pGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr() );
    if ( pGetInstanceProcAddr == nullptr ) {
        OTDU_LOG_ERROR( "Failed to load the Vulkan loader (%s). On macOS, "
                        "install the LunarG Vulkan SDK (MoltenVK).\n",
                        SDL_GetError() );
        return false;
    }
    volkInitializeCustom( pGetInstanceProcAddr );

    uint32_t apiVersion = volkGetInstanceVersion();
    OTDU_LOG_INFO( "Vulkan instance version %u.%u.%u\n",
                   VK_API_VERSION_MAJOR( apiVersion ),
                   VK_API_VERSION_MINOR( apiVersion ),
                   VK_API_VERSION_PATCH( apiVersion ) );

    uint32_t numExtensions = 0;
    vkEnumerateInstanceExtensionProperties( nullptr, &numExtensions, nullptr );
    std::vector<VkExtensionProperties> availableExtensions( numExtensions );
    vkEnumerateInstanceExtensionProperties( nullptr, &numExtensions,
                                            availableExtensions.data() );

    uint32_t numSDLExtensions = 0;
    const char *const *ppSDLExtensions =
        SDL_Vulkan_GetInstanceExtensions( &numSDLExtensions );
    std::vector<const char *> extensions( ppSDLExtensions,
                                          ppSDLExtensions + numSDLExtensions );

    VkInstanceCreateFlags flags = 0;
    if ( HasExtension( availableExtensions,
                       VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) ) {
        extensions.push_back( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    if ( HasExtension( availableExtensions,
                       VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) ) {
        extensions.push_back( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME );
    }

    std::vector<const char *> layers;
    if ( bValidation ) {
        uint32_t numLayers = 0;
        vkEnumerateInstanceLayerProperties( &numLayers, nullptr );
        std::vector<VkLayerProperties> availableLayers( numLayers );
        vkEnumerateInstanceLayerProperties( &numLayers, availableLayers.data() );
        for ( const auto &layer : availableLayers ) {
            if ( strcmp( layer.layerName, "VK_LAYER_KHRONOS_validation" ) == 0 ) {
                layers.push_back( "VK_LAYER_KHRONOS_validation" );
            }
        }
        if ( HasExtension( availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
            extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        }
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "OpenTDU2";
    appInfo.applicationVersion = VK_MAKE_VERSION( 0, 1, 0 );
    appInfo.pEngineName = "OpenTDU2";
    appInfo.engineVersion = VK_MAKE_VERSION( 0, 1, 0 );
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.flags = flags;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>( extensions.size() );
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>( layers.size() );
    createInfo.ppEnabledLayerNames = layers.data();

    VkResult result = vkCreateInstance( &createInfo, nullptr, &instance );
    if ( result != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "vkCreateInstance failed (%d)\n", static_cast<int32_t>( result ) );
        return false;
    }
    volkLoadInstance( instance );

    if ( bValidation && vkCreateDebugUtilsMessengerEXT != nullptr ) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = DebugCallback;
        vkCreateDebugUtilsMessengerEXT( instance, &messengerInfo, nullptr,
                                        &debugMessenger );
    }

    if ( !SDL_Vulkan_CreateSurface( pWindow, instance, nullptr, &surface ) ) {
        OTDU_LOG_ERROR( "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError() );
        return false;
    }
    return true;
}

bool RenderDevice::pickPhysicalDevice() {
    uint32_t numDevices = 0;
    vkEnumeratePhysicalDevices( instance, &numDevices, nullptr );
    if ( numDevices == 0 ) {
        OTDU_LOG_ERROR( "No Vulkan device found\n" );
        return false;
    }
    std::vector<VkPhysicalDevice> devices( numDevices );
    vkEnumeratePhysicalDevices( instance, &numDevices, devices.data() );

    int32_t bestScore = -1;
    for ( VkPhysicalDevice candidate : devices ) {
        uint32_t numFamilies = 0;
        vkGetPhysicalDeviceQueueFamilyProperties( candidate, &numFamilies, nullptr );
        std::vector<VkQueueFamilyProperties> families( numFamilies );
        vkGetPhysicalDeviceQueueFamilyProperties( candidate, &numFamilies,
                                                  families.data() );

        int32_t family = -1;
        for ( uint32_t i = 0; i < numFamilies; i++ ) {
            VkBool32 bPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR( candidate, i, surface, &bPresent );
            if ( ( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) && bPresent ) {
                family = static_cast<int32_t>( i );
                break;
            }
        }
        if ( family < 0 ) {
            continue;
        }

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties( candidate, &properties );
        int32_t score = 1;
        if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
            score = 4;
        } else if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) {
            score = 3;
        }
        if ( score > bestScore ) {
            bestScore = score;
            physicalDevice = candidate;
            graphicsQueueFamily = static_cast<uint32_t>( family );
            deviceName = properties.deviceName;
        }
    }

    if ( physicalDevice == VK_NULL_HANDLE ) {
        OTDU_LOG_ERROR( "No suitable Vulkan device found\n" );
        return false;
    }
    OTDU_LOG_INFO( "Using GPU: %s\n", deviceName.c_str() );
    return true;
}

bool RenderDevice::createDevice() {
    uint32_t numExtensions = 0;
    vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &numExtensions,
                                          nullptr );
    std::vector<VkExtensionProperties> availableExtensions( numExtensions );
    vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &numExtensions,
                                          availableExtensions.data() );

    std::vector<const char *> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if ( HasExtension( availableExtensions, "VK_KHR_portability_subset" ) ) {
        extensions.push_back( "VK_KHR_portability_subset" );
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures( physicalDevice, &supportedFeatures );
    VkPhysicalDeviceFeatures enabledFeatures = {};
    bSupportsBC = supportedFeatures.textureCompressionBC == VK_TRUE;
    enabledFeatures.textureCompressionBC = supportedFeatures.textureCompressionBC;
    enabledFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
    enabledFeatures.fillModeNonSolid = supportedFeatures.fillModeNonSolid;
    enabledFeatures.independentBlend = supportedFeatures.independentBlend;
    enabledFeatures.shaderClipDistance = supportedFeatures.shaderClipDistance;
    OTDU_LOG_INFO( "BC texture compression: %s\n", bSupportsBC ? "yes" : "no (CPU decode)" );

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties( physicalDevice, &properties );
    uniformBufferAlignment = Max<VkDeviceSize>( properties.limits.minUniformBufferOffsetAlignment, 16 );

    // Optional features for the translated D3D9 shaders.
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexingFeatures = {};
    indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demoteFeatures = {};
    demoteFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    const bool bHasIndexing = HasExtension( availableExtensions, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME );
    const bool bHasDemote = HasExtension( availableExtensions, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME );
    if ( bHasIndexing && bHasDemote && vkGetPhysicalDeviceFeatures2 != nullptr ) {
        indexingFeatures.pNext = &demoteFeatures;
        VkPhysicalDeviceFeatures2 features2 = {};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &indexingFeatures;
        vkGetPhysicalDeviceFeatures2( physicalDevice, &features2 );
    }
    bSupportsDxso = bHasIndexing && bHasDemote && indexingFeatures.runtimeDescriptorArray &&
                    demoteFeatures.shaderDemoteToHelperInvocation && supportedFeatures.shaderClipDistance;
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT enabledIndexing = {};
    enabledIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT enabledDemote = {};
    enabledDemote.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    if ( bSupportsDxso ) {
        extensions.push_back( VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME );
        extensions.push_back( VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME );
        if ( HasExtension( availableExtensions, VK_KHR_MAINTENANCE_3_EXTENSION_NAME ) ) {
            extensions.push_back( VK_KHR_MAINTENANCE_3_EXTENSION_NAME );
        }
        enabledIndexing.runtimeDescriptorArray = VK_TRUE;
        enabledIndexing.pNext = &enabledDemote;
        enabledDemote.shaderDemoteToHelperInvocation = VK_TRUE;
    }
    OTDU_LOG_INFO( "Original shaders (.sob via DXSO): %s\n",
                   bSupportsDxso ? "supported" : "unsupported (replacement shaders only)" );

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>( extensions.size() );
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.pEnabledFeatures = &enabledFeatures;
    createInfo.pNext = bSupportsDxso ? &enabledIndexing : nullptr;

    VkResult result = vkCreateDevice( physicalDevice, &createInfo, nullptr, &device );
    if ( result != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "vkCreateDevice failed (%d)\n", static_cast<int32_t>( result ) );
        return false;
    }
    volkLoadDevice( device );
    vkGetDeviceQueue( device, graphicsQueueFamily, 0, &graphicsQueue );

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    if ( vmaCreateAllocator( &allocatorInfo, &allocator ) != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Failed to create VMA allocator\n" );
        return false;
    }

    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache( device, &cacheInfo, nullptr, &pipelineCache );
    return true;
}

bool RenderDevice::createMainRenderPass() {
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = depthTexture.Format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 2;
    createInfo.pAttachments = attachments;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    return vkCreateRenderPass( device, &createInfo, nullptr, &mainRenderPass ) == VK_SUCCESS;
}

bool RenderDevice::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, surface, &capabilities );

    uint32_t numFormats = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, &numFormats, nullptr );
    std::vector<VkSurfaceFormatKHR> formats( numFormats );
    vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, &numFormats,
                                          formats.data() );

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for ( const auto &format : formats ) {
        if ( ( format.format == VK_FORMAT_B8G8R8A8_UNORM ||
               format.format == VK_FORMAT_R8G8B8A8_UNORM ) &&
             format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
            surfaceFormat = format;
            break;
        }
    }
    swapchainFormat = surfaceFormat.format;

    uint32_t numPresentModes = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, surface,
                                               &numPresentModes, nullptr );
    std::vector<VkPresentModeKHR> presentModes( numPresentModes );
    vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, surface,
                                               &numPresentModes, presentModes.data() );
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
#if defined( __APPLE__ )
    // MoltenVK: IMMEDIATE turns the CAMetalLayer display sync off, and the
    // window then shows black frames between the presented images (the
    // rendered images themselves are complete: -lumalog). FIFO only.
    const bool bAllowUnsynced = CmdLine::HasArg( "-novsync" );
#else
    const bool bAllowUnsynced = true;
#endif
    if ( !bVSync && bAllowUnsynced ) {
        for ( VkPresentModeKHR mode : presentModes ) {
            if ( mode == VK_PRESENT_MODE_MAILBOX_KHR ||
                 mode == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
                presentMode = mode;
                break;
            }
        }
    }

    int32_t pixelWidth = 0, pixelHeight = 0;
    SDL_GetWindowSizeInPixels( pWindow, &pixelWidth, &pixelHeight );
    if ( capabilities.currentExtent.width != 0xffffffff ) {
        swapchainExtent = capabilities.currentExtent;
    } else {
        swapchainExtent.width = Clamp<uint32_t>( static_cast<uint32_t>( pixelWidth ),
                                                 capabilities.minImageExtent.width,
                                                 capabilities.maxImageExtent.width );
        swapchainExtent.height = Clamp<uint32_t>( static_cast<uint32_t>( pixelHeight ),
                                                  capabilities.minImageExtent.height,
                                                  capabilities.maxImageExtent.height );
    }
    if ( swapchainExtent.width == 0 || swapchainExtent.height == 0 ) {
        return false;
    }

    uint32_t numImages = Max( capabilities.minImageCount + 1, 3u );
    if ( capabilities.maxImageCount > 0 ) {
        numImages = Min( numImages, capabilities.maxImageCount );
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = numImages;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ( !( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ) ) {
        createInfo.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
            capabilities.supportedCompositeAlpha & ( ~capabilities.supportedCompositeAlpha + 1 ) );
    }
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if ( vkCreateSwapchainKHR( device, &createInfo, nullptr, &swapchain ) != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "vkCreateSwapchainKHR failed\n" );
        return false;
    }

    uint32_t numSwapchainImages = 0;
    vkGetSwapchainImagesKHR( device, swapchain, &numSwapchainImages, nullptr );
    swapchainImages.resize( numSwapchainImages );
    vkGetSwapchainImagesKHR( device, swapchain, &numSwapchainImages, swapchainImages.data() );

    // Depth buffer
    depthTexture.Format = FindDepthFormat( physicalDevice );
    depthTexture.Width = swapchainExtent.width;
    depthTexture.Height = swapchainExtent.height;
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = depthTexture.Format;
        imageInfo.extent = { swapchainExtent.width, swapchainExtent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage( allocator, &imageInfo, &allocInfo, &depthTexture.Image,
                        &depthTexture.Allocation, nullptr );

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthTexture.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthTexture.Format;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCreateImageView( device, &viewInfo, nullptr, &depthTexture.View );
    }

    if ( mainRenderPass == VK_NULL_HANDLE && !createMainRenderPass() ) {
        return false;
    }

    swapchainViews.resize( numSwapchainImages );
    framebuffers.resize( numSwapchainImages );
    renderFinishedSemaphores.resize( numSwapchainImages );
    for ( uint32_t i = 0; i < numSwapchainImages; i++ ) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView( device, &viewInfo, nullptr, &swapchainViews[i] );

        VkImageView attachments[2] = { swapchainViews[i], depthTexture.View };
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = mainRenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        vkCreateFramebuffer( device, &framebufferInfo, nullptr, &framebuffers[i] );

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore( device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i] );
    }

    OTDU_LOG_INFO( "Swapchain created (%ux%u, %u images)\n", swapchainExtent.width,
                   swapchainExtent.height, numSwapchainImages );
    bSwapchainDirty = false;
    return true;
}

void RenderDevice::destroySwapchain() {
    for ( VkFramebuffer framebuffer : framebuffers ) {
        vkDestroyFramebuffer( device, framebuffer, nullptr );
    }
    for ( VkImageView view : swapchainViews ) {
        vkDestroyImageView( device, view, nullptr );
    }
    for ( VkSemaphore semaphore : renderFinishedSemaphores ) {
        vkDestroySemaphore( device, semaphore, nullptr );
    }
    framebuffers.clear();
    swapchainViews.clear();
    renderFinishedSemaphores.clear();

    if ( depthTexture.View != VK_NULL_HANDLE ) {
        vkDestroyImageView( device, depthTexture.View, nullptr );
        vmaDestroyImage( allocator, depthTexture.Image, depthTexture.Allocation );
        depthTexture.View = VK_NULL_HANDLE;
        depthTexture.Image = VK_NULL_HANDLE;
    }
    if ( swapchain != VK_NULL_HANDLE ) {
        vkDestroySwapchainKHR( device, swapchain, nullptr );
        swapchain = VK_NULL_HANDLE;
    }
}

bool RenderDevice::createFrameResources() {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    vkCreateCommandPool( device, &poolInfo, nullptr, &uploadCommandPool );

    VkDescriptorSetLayoutBinding bindings[kMaxTexturesPerSet] = {};
    for ( uint32_t i = 0; i < kMaxTexturesPerSet; i++ ) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kMaxTexturesPerSet;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &textureSetLayout );

    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = kPushConstantsSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &textureSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout( device, &pipelineLayoutInfo, nullptr, &defaultPipelineLayout );

    for ( FrameData &frame : frames ) {
        vkCreateCommandPool( device, &poolInfo, nullptr, &frame.CommandPool );

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers( device, &allocInfo, &frame.CommandBuffer );

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore( device, &semaphoreInfo, nullptr, &frame.ImageAvailable );

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence( device, &fenceInfo, nullptr, &frame.Fence );

        VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          kMaxDescriptorSetsPerFrame * kMaxTexturesPerSet };
        VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.maxSets = kMaxDescriptorSetsPerFrame;
        descriptorPoolInfo.poolSizeCount = 1;
        descriptorPoolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool( device, &descriptorPoolInfo, nullptr, &frame.DescriptorPool );

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = kTransientBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocationInfo = {};
        vmaCreateBuffer( allocator, &bufferInfo, &allocCreateInfo, &frame.TransientBuffer.Buffer,
                         &frame.TransientBuffer.Allocation, &allocationInfo );
        frame.TransientBuffer.pMapped = allocationInfo.pMappedData;
        frame.TransientBuffer.Size = kTransientBufferSize;
    }
    return true;
}

bool RenderDevice::create( SDL_Window *pSDLWindow, bool bEnableVSync, bool bValidation ) {
    pWindow = pSDLWindow;
    bVSync = bEnableVSync;

    if ( !createInstance( bValidation ) || !pickPhysicalDevice() || !createDevice() ) {
        return false;
    }
    if ( !createFrameResources() ) {
        return false;
    }
    if ( !createSwapchain() ) {
        bSwapchainDirty = true;
    }
    return true;
}

void RenderDevice::flushPendingDeletes( FrameData &frame ) {
    for ( GPUTexture *pTexture : frame.PendingTextureDeletes ) {
        vkDestroyImageView( device, pTexture->View, nullptr );
        vmaDestroyImage( allocator, pTexture->Image, pTexture->Allocation );
        delete pTexture;
    }
    frame.PendingTextureDeletes.clear();
    for ( GPUBuffer *pBuffer : frame.PendingBufferDeletes ) {
        vmaDestroyBuffer( allocator, pBuffer->Buffer, pBuffer->Allocation );
        delete pBuffer;
    }
    frame.PendingBufferDeletes.clear();
}

void RenderDevice::destroy() {
    if ( device == VK_NULL_HANDLE ) {
        if ( instance != VK_NULL_HANDLE ) {
            vkDestroyInstance( instance, nullptr );
            instance = VK_NULL_HANDLE;
        }
        return;
    }

    vkDeviceWaitIdle( device );
    destroySwapchain();

    for ( FrameData &frame : frames ) {
        flushPendingDeletes( frame );
        vkDestroyCommandPool( device, frame.CommandPool, nullptr );
        vkDestroySemaphore( device, frame.ImageAvailable, nullptr );
        vkDestroyFence( device, frame.Fence, nullptr );
        vkDestroyDescriptorPool( device, frame.DescriptorPool, nullptr );
        vmaDestroyBuffer( allocator, frame.TransientBuffer.Buffer,
                          frame.TransientBuffer.Allocation );
    }
    for ( auto &sampler : samplers ) {
        vkDestroySampler( device, sampler.second, nullptr );
    }
    samplers.clear();

    vkDestroyPipelineLayout( device, defaultPipelineLayout, nullptr );
    vkDestroyDescriptorSetLayout( device, textureSetLayout, nullptr );
    vkDestroyCommandPool( device, uploadCommandPool, nullptr );
    vkDestroyRenderPass( device, mainRenderPass, nullptr );
    for ( auto &entry : targetRenderPasses ) {
        vkDestroyRenderPass( device, entry.second, nullptr );
    }
    targetRenderPasses.clear();
    vkDestroyPipelineCache( device, pipelineCache, nullptr );
    vmaDestroyAllocator( allocator );
    vkDestroyDevice( device, nullptr );
    device = VK_NULL_HANDLE;

    if ( surface != VK_NULL_HANDLE ) {
        vkDestroySurfaceKHR( instance, surface, nullptr );
    }
    if ( debugMessenger != VK_NULL_HANDLE ) {
        vkDestroyDebugUtilsMessengerEXT( instance, debugMessenger, nullptr );
    }
    vkDestroyInstance( instance, nullptr );
    instance = VK_NULL_HANDLE;
}

void RenderDevice::waitIdle() {
    if ( device != VK_NULL_HANDLE ) {
        vkDeviceWaitIdle( device );
    }
}

bool RenderDevice::beginFrame( const ColorRGBA &clearColor ) {
    FrameData &frame = frames[frameIndex];
    vkWaitForFences( device, 1, &frame.Fence, VK_TRUE, UINT64_MAX );

    if ( bSwapchainDirty || swapchain == VK_NULL_HANDLE ) {
        vkDeviceWaitIdle( device );
        destroySwapchain();
        if ( !createSwapchain() ) {
            return false;
        }
    }

    VkResult result = vkAcquireNextImageKHR( device, swapchain, UINT64_MAX,
                                             frame.ImageAvailable, VK_NULL_HANDLE,
                                             &imageIndex );
    if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
        bSwapchainDirty = true;
        return false;
    } else if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR ) {
        OTDU_LOG_ERROR( "vkAcquireNextImageKHR failed (%d)\n", static_cast<int32_t>( result ) );
        return false;
    }

    vkResetFences( device, 1, &frame.Fence );
    if ( frame.bLumaPending && frame.pLumaProbe != nullptr ) {
        vmaInvalidateAllocation( allocator, frame.pLumaProbe->Allocation, 0, VK_WHOLE_SIZE );
        const uint8_t *pPixels = static_cast<const uint8_t *>( frame.pLumaProbe->pMapped );
        const size_t numPixels = frame.pLumaProbe->Size / 4;
        uint64_t sum = 0;
        size_t numBlack = 0;
        for ( size_t i = 0; i < numPixels; i++ ) {
            const uint32_t value = pPixels[i * 4] + pPixels[i * 4 + 1] + pPixels[i * 4 + 2];
            sum += value;
            numBlack += value < 6 ? 1 : 0;
        }
        OTDU_LOG_INFO( "LUMA frame %llu avg %.1f black %.3f\n", static_cast<unsigned long long>( frame.LumaFrame ),
                       sum / ( 3.0 * numPixels ), static_cast<double>( numBlack ) / numPixels );
        static const bool bFullImage = CmdLine::HasArg( "-glitchcapture" );
        if ( bFullImage && numPixels == static_cast<size_t>( swapchainExtent.width ) * swapchainExtent.height ) {
            // Frames very different from the previous one (one frame
            // glitches): both are saved (glitch/<frame>_a/_b.ppm).
            static std::vector<uint8_t> previous;
            static uint64_t previousFrame = 0;
            static int numSaved = 0;
            const bool bBGRA = swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM || swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
            auto save = [&]( const uint8_t *pData, uint64_t index, const char *pSuffix ) {
                char path[128];
                snprintf( path, sizeof( path ), "glitch_%06llu_%s.ppm", static_cast<unsigned long long>( index ), pSuffix );
                FILE *pFile = fopen( path, "wb" );
                if ( pFile == nullptr ) {
                    return;
                }
                fprintf( pFile, "P6\n%u %u\n255\n", swapchainExtent.width, swapchainExtent.height );
                std::vector<uint8_t> row( swapchainExtent.width * 3 );
                for ( uint32_t y = 0; y < swapchainExtent.height; y++ ) {
                    const uint8_t *pRow = pData + static_cast<size_t>( y ) * swapchainExtent.width * 4;
                    for ( uint32_t x = 0; x < swapchainExtent.width; x++ ) {
                        row[x * 3 + 0] = bBGRA ? pRow[x * 4 + 2] : pRow[x * 4];
                        row[x * 3 + 1] = pRow[x * 4 + 1];
                        row[x * 3 + 2] = bBGRA ? pRow[x * 4] : pRow[x * 4 + 2];
                    }
                    fwrite( row.data(), 1, row.size(), pFile );
                }
                fclose( pFile );
            };
            if ( previous.size() == numPixels * 4 ) {
                uint64_t difference = 0;
                size_t samples = 0;
                for ( size_t i = 0; i < numPixels; i += 97 ) {
                    for ( int c = 0; c < 3; c++ ) {
                        difference += static_cast<uint64_t>( std::abs( static_cast<int>( pPixels[i * 4 + c] ) -
                                                                       static_cast<int>( previous[i * 4 + c] ) ) );
                    }
                    samples++;
                }
                const double meanDifference = static_cast<double>( difference ) / ( 3.0 * Max<size_t>( samples, 1 ) );
                OTDU_LOG_INFO( "GLITCH frame %llu diff %.2f\n", static_cast<unsigned long long>( frame.LumaFrame ),
                               meanDifference );
                const double threshold = CmdLine::GetArgInt( "-glitchthreshold", 12 );
                if ( meanDifference > threshold && numSaved < 40 ) {
                    save( previous.data(), previousFrame, "a" );
                    save( pPixels, frame.LumaFrame, "b" );
                    numSaved++;
                }
            }
            previous.assign( pPixels, pPixels + numPixels * 4 );
            previousFrame = frame.LumaFrame;
        }
        frame.bLumaPending = false;
    }
    flushPendingDeletes( frame );
    vkResetDescriptorPool( device, frame.DescriptorPool, 0 );
    frame.TransientOffset = 0;

    vkResetCommandPool( device, frame.CommandPool, 0 );
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer( frame.CommandBuffer, &beginInfo );

    frameClearColor = clearColor;
    bInMainPass = false;
    currentRenderPass = VK_NULL_HANDLE;
    bFrameStarted = true;
    return true;
}

void RenderDevice::beginMainPass() {
    if ( !bFrameStarted || bInMainPass ) {
        return;
    }
    if ( pCurrentTarget != nullptr ) {
        endRenderTarget();
    }
    FrameData &frame = frames[frameIndex];
    VkClearValue clearValues[2] = {};
    clearValues[0].color = { { frameClearColor.r, frameClearColor.g, frameClearColor.b, frameClearColor.a } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo passInfo = {};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = mainRenderPass;
    passInfo.framebuffer = framebuffers[imageIndex];
    passInfo.renderArea.extent = swapchainExtent;
    passInfo.clearValueCount = 2;
    passInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass( frame.CommandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE );

    VkViewport viewport = { 0.0f, 0.0f, static_cast<float>( swapchainExtent.width ),
                            static_cast<float>( swapchainExtent.height ), 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, swapchainExtent };
    vkCmdSetViewport( frame.CommandBuffer, 0, 1, &viewport );
    vkCmdSetScissor( frame.CommandBuffer, 0, 1, &scissor );
    bInMainPass = true;
    currentRenderPass = mainRenderPass;
}

VkRenderPass RenderDevice::getTargetRenderPass( VkFormat colorFormat, VkFormat depthFormat, bool bClear ) {
    const uint64_t key = static_cast<uint64_t>( colorFormat ) | ( static_cast<uint64_t>( depthFormat ) << 24 ) |
                         ( bClear ? 1ull << 48 : 0ull );
    auto it = targetRenderPasses.find( key );
    if ( it != targetRenderPasses.end() ) {
        return it->second;
    }
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = colorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = bClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = bClear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const bool bDepth = depthFormat != VK_FORMAT_UNDEFINED;
    if ( bDepth ) {
        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = bClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = bClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout =
            bClear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = bDepth ? &depthRef : nullptr;

    const VkPipelineStageFlags attachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags attachmentAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    VkSubpassDependency dependencies[2] = {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = attachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = attachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = attachmentAccess;
    dependencies[0].dstAccessMask = attachmentAccess | VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = attachmentStages;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | attachmentStages;
    dependencies[1].srcAccessMask = attachmentAccess;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | attachmentAccess;

    VkRenderPassCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = bDepth ? 2 : 1;
    createInfo.pAttachments = attachments;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 2;
    createInfo.pDependencies = dependencies;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    vkCreateRenderPass( device, &createInfo, nullptr, &renderPass );
    targetRenderPasses[key] = renderPass;
    return renderPass;
}

RenderTarget *RenderDevice::createRenderTarget( uint32_t width, uint32_t height, VkFormat colorFormat, bool bDepth,
                                                const char *pName ) {
    if ( colorFormat == VK_FORMAT_UNDEFINED ) {
        colorFormat = swapchainFormat;
    }
    RenderTarget *pTarget = new RenderTarget();
    pTarget->Width = width;
    pTarget->Height = height;
    pTarget->bHasDepth = bDepth;

    auto createImage = [&]( GPUTexture &texture, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect ) {
        texture.Format = format;
        texture.Width = width;
        texture.Height = height;
        texture.Name = pName;
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage( allocator, &imageInfo, &allocInfo, &texture.Image, &texture.Allocation, nullptr );
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { aspect, 0, 1, 0, 1 };
        vkCreateImageView( device, &viewInfo, nullptr, &texture.View );
    };
    createImage( pTarget->Color, colorFormat,
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                 VK_IMAGE_ASPECT_COLOR_BIT );
    VkImageView attachments[2] = { pTarget->Color.View, VK_NULL_HANDLE };
    if ( bDepth ) {
        const VkFormat depthFormat = depthTexture.Format;
        createImage( pTarget->Depth, depthFormat,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT );
        const bool bStencil = depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT;
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = pTarget->Depth.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange = {
            static_cast<VkImageAspectFlags>( VK_IMAGE_ASPECT_DEPTH_BIT | ( bStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 ) ),
            0, 1, 0, 1 };
        vkCreateImageView( device, &viewInfo, nullptr, &pTarget->DepthAttachmentView );
        attachments[1] = pTarget->DepthAttachmentView;
    }
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = getTargetRenderPass( colorFormat, bDepth ? depthTexture.Format : VK_FORMAT_UNDEFINED,
                                                      true );
    framebufferInfo.attachmentCount = bDepth ? 2 : 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    vkCreateFramebuffer( device, &framebufferInfo, nullptr, &pTarget->Framebuffer );
    return pTarget;
}

RenderTarget *RenderDevice::createCubeRenderTarget( uint32_t size, bool bDepth, const char *pName ) {
    RenderTarget *pTarget = new RenderTarget();
    pTarget->Width = size;
    pTarget->Height = size;
    pTarget->bHasDepth = bDepth;
    pTarget->NumLayers = 6;
    const VkFormat colorFormat = swapchainFormat;
    GPUTexture &color = pTarget->Color;
    color.Format = colorFormat;
    color.Width = color.Height = size;
    color.ViewType = VK_IMAGE_VIEW_TYPE_CUBE;
    color.Name = pName;
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = colorFormat;
    imageInfo.extent = { size, size, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCreateImage( allocator, &imageInfo, &allocInfo, &color.Image, &color.Allocation, nullptr );
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = color.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = colorFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
    vkCreateImageView( device, &viewInfo, nullptr, &color.View );

    if ( bDepth ) {
        GPUTexture &depth = pTarget->Depth;
        depth.Format = depthTexture.Format;
        depth.Width = depth.Height = size;
        VkImageCreateInfo depthInfo = imageInfo;
        depthInfo.flags = 0;
        depthInfo.format = depthTexture.Format;
        depthInfo.arrayLayers = 1;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vmaCreateImage( allocator, &depthInfo, &allocInfo, &depth.Image, &depth.Allocation, nullptr );
        const bool bStencil =
            depthTexture.Format == VK_FORMAT_D32_SFLOAT_S8_UINT || depthTexture.Format == VK_FORMAT_D24_UNORM_S8_UINT;
        VkImageViewCreateInfo depthViewInfo = {};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = depth.Image;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = depthTexture.Format;
        depthViewInfo.subresourceRange = {
            static_cast<VkImageAspectFlags>( VK_IMAGE_ASPECT_DEPTH_BIT | ( bStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 ) ),
            0, 1, 0, 1 };
        vkCreateImageView( device, &depthViewInfo, nullptr, &pTarget->DepthAttachmentView );
    }
    const VkRenderPass renderPass =
        getTargetRenderPass( colorFormat, bDepth ? depthTexture.Format : VK_FORMAT_UNDEFINED, true );
    for ( uint32_t face = 0; face < 6; face++ ) {
        VkImageViewCreateInfo faceInfo = viewInfo;
        faceInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        faceInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1 };
        vkCreateImageView( device, &faceInfo, nullptr, &pTarget->LayerViews[face] );
        VkImageView attachments[2] = { pTarget->LayerViews[face], pTarget->DepthAttachmentView };
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = bDepth ? 2 : 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = size;
        framebufferInfo.height = size;
        framebufferInfo.layers = 1;
        vkCreateFramebuffer( device, &framebufferInfo, nullptr, &pTarget->LayerFramebuffers[face] );
    }
    return pTarget;
}

void RenderDevice::destroyRenderTarget( RenderTarget *pTarget ) {
    if ( pTarget == nullptr ) {
        return;
    }
    vkDeviceWaitIdle( device );
    for ( uint32_t face = 0; face < 6; face++ ) {
        if ( pTarget->LayerFramebuffers[face] != VK_NULL_HANDLE ) {
            vkDestroyFramebuffer( device, pTarget->LayerFramebuffers[face], nullptr );
        }
        if ( pTarget->LayerViews[face] != VK_NULL_HANDLE ) {
            vkDestroyImageView( device, pTarget->LayerViews[face], nullptr );
        }
    }
    if ( pTarget->Framebuffer != VK_NULL_HANDLE ) {
        vkDestroyFramebuffer( device, pTarget->Framebuffer, nullptr );
    }
    if ( pTarget->DepthAttachmentView != VK_NULL_HANDLE ) {
        vkDestroyImageView( device, pTarget->DepthAttachmentView, nullptr );
    }
    for ( GPUTexture *pTexture : { &pTarget->Color, &pTarget->Depth } ) {
        if ( pTexture->View != VK_NULL_HANDLE ) {
            vkDestroyImageView( device, pTexture->View, nullptr );
        }
        if ( pTexture->Image != VK_NULL_HANDLE ) {
            vmaDestroyImage( allocator, pTexture->Image, pTexture->Allocation );
        }
    }
    delete pTarget;
}

void RenderDevice::beginRenderTarget( RenderTarget *pTarget, bool bClear, const ColorRGBA &clearColor,
                                      uint32_t layer ) {
    if ( !bFrameStarted || bInMainPass || pTarget == nullptr || layer >= pTarget->NumLayers ) {
        return;
    }
    if ( pCurrentTarget != nullptr ) {
        endRenderTarget();
    }
    bClear |= ( pTarget->InitializedLayers & ( 1u << layer ) ) == 0;
    // Shared depth of the cube faces: always cleared.
    bClear |= pTarget->NumLayers > 1;
    VkCommandBuffer commandBuffer = frames[frameIndex].CommandBuffer;
    VkClearValue clearValues[2] = {};
    clearValues[0].color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
    clearValues[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo passInfo = {};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass =
        getTargetRenderPass( pTarget->Color.Format, pTarget->bHasDepth ? pTarget->Depth.Format : VK_FORMAT_UNDEFINED,
                             bClear );
    passInfo.framebuffer = pTarget->NumLayers > 1 ? pTarget->LayerFramebuffers[layer] : pTarget->Framebuffer;
    passInfo.renderArea.extent = { pTarget->Width, pTarget->Height };
    passInfo.clearValueCount = pTarget->bHasDepth ? 2 : 1;
    passInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass( commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE );
    VkViewport viewport = { 0.0f, 0.0f, static_cast<float>( pTarget->Width ), static_cast<float>( pTarget->Height ),
                            0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { pTarget->Width, pTarget->Height } };
    vkCmdSetViewport( commandBuffer, 0, 1, &viewport );
    vkCmdSetScissor( commandBuffer, 0, 1, &scissor );
    pCurrentTarget = pTarget;
    currentRenderPass = passInfo.renderPass;
    pTarget->InitializedLayers |= 1u << layer;
    pTarget->bInitialized = pTarget->InitializedLayers == ( ( 1u << pTarget->NumLayers ) - 1 );
}

void RenderDevice::clearRenderTargetDepth() {
    if ( pCurrentTarget == nullptr || !pCurrentTarget->bHasDepth ) {
        return;
    }
    VkClearAttachment attachment = {};
    attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachment.clearValue.depthStencil = { 1.0f, 0 };
    VkClearRect rect = {};
    rect.rect.extent = { pCurrentTarget->Width, pCurrentTarget->Height };
    rect.layerCount = 1;
    vkCmdClearAttachments( frames[frameIndex].CommandBuffer, 1, &attachment, 1, &rect );
}

bool RenderDevice::copyRenderTargetDepth( RenderTarget *pTarget, int32_t x, int32_t y, uint32_t width, uint32_t height,
                                          GPUBuffer *pDestination ) {
    if ( pTarget == nullptr || !pTarget->bHasDepth || pCurrentTarget != nullptr || bInMainPass ||
         pTarget->Depth.Format != VK_FORMAT_D32_SFLOAT || pDestination == nullptr ||
         pDestination->Size < static_cast<VkDeviceSize>( width ) * height * 4 ) {
        return false;
    }
    x = Clamp<int32_t>( x, 0, static_cast<int32_t>( pTarget->Width - width ) );
    y = Clamp<int32_t>( y, 0, static_cast<int32_t>( pTarget->Height - height ) );
    VkCommandBuffer commandBuffer = frames[frameIndex].CommandBuffer;
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pTarget->Depth.Image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                          nullptr, 0, nullptr, 1, &barrier );
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
    region.imageOffset = { x, y, 0 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyImageToBuffer( commandBuffer, pTarget->Depth.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            pDestination->Buffer, 1, &region );
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                          nullptr, 0, nullptr, 1, &barrier );
    return true;
}

void RenderDevice::endRenderTarget() {
    if ( pCurrentTarget == nullptr ) {
        return;
    }
    vkCmdEndRenderPass( frames[frameIndex].CommandBuffer );
    // Explicit barrier between the offscreen passes (attachment writes ->
    // any later read or write). The render pass external dependencies alone
    // are not honoured by MoltenVK: on an M3 the scene pass read the shadow
    // buffer before it was written (ground missing in ~1 frame in 3).
    static const bool bNoBarrier = CmdLine::HasArg( "-nopassbarrier" );
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    if ( !bNoBarrier )
    vkCmdPipelineBarrier( frames[frameIndex].CommandBuffer,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 1, &barrier, 0, nullptr, 0, nullptr );
    pCurrentTarget = nullptr;
    currentRenderPass = VK_NULL_HANDLE;
}

void RenderDevice::endFrame() {
    if ( !bFrameStarted ) {
        return;
    }
    if ( !bInMainPass ) {
        beginMainPass();
    }
    bFrameStarted = false;
    bInMainPass = false;
    currentRenderPass = VK_NULL_HANDLE;

    FrameData &frame = frames[frameIndex];
    vkCmdEndRenderPass( frame.CommandBuffer );
    static const bool bLumaLog = CmdLine::HasArg( "-lumalog" );
    if ( bLumaLog ) {
        // 8 rows of the presented image copied to a per frame buffer, read
        // back when the frame slot is reused (no CPU / GPU serialization).
        // -glitchcapture: the whole image (one row per buffer row).
        static const bool bFullImage = CmdLine::HasArg( "-glitchcapture" );
        static constexpr uint32_t kMaxRows = 8;
        const uint32_t kNumRows = bFullImage ? swapchainExtent.height : kMaxRows;
        const VkDeviceSize rowSize = static_cast<VkDeviceSize>( swapchainExtent.width ) * 4;
        if ( frame.pLumaProbe == nullptr || frame.pLumaProbe->Size != rowSize * kNumRows ) {
            if ( frame.pLumaProbe != nullptr ) {
                vmaDestroyBuffer( allocator, frame.pLumaProbe->Buffer, frame.pLumaProbe->Allocation );
                delete frame.pLumaProbe;
            }
            frame.pLumaProbe = createBuffer( rowSize * kNumRows, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
        }
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImages[imageIndex];
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier( frame.CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );
        VkBufferImageCopy regions[kMaxRows] = {};
        uint32_t numRegions = 0;
        if ( bFullImage ) {
            regions[0].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            regions[0].imageExtent = { swapchainExtent.width, swapchainExtent.height, 1 };
            numRegions = 1;
        } else {
            for ( uint32_t i = 0; i < kMaxRows; i++ ) {
                regions[i].bufferOffset = rowSize * i;
                regions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                regions[i].imageOffset = { 0, static_cast<int32_t>( swapchainExtent.height * ( 2 * i + 1 ) / ( 2 * kMaxRows ) ), 0 };
                regions[i].imageExtent = { swapchainExtent.width, 1, 1 };
            }
            numRegions = kMaxRows;
        }
        vkCmdCopyImageToBuffer( frame.CommandBuffer, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                frame.pLumaProbe->Buffer, numRegions, regions );
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkBufferMemoryBarrier bufferBarrier = {};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        bufferBarrier.srcQueueFamilyIndex = bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = frame.pLumaProbe->Buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier( frame.CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                              &bufferBarrier, 1, &barrier );
        frame.LumaFrame = frameNumber;
        frame.bLumaPending = true;
    }
    vkEndCommandBuffer( frame.CommandBuffer );

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.ImageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores[imageIndex];
    OTDU_VK_CHECK( vkQueueSubmit( graphicsQueue, 1, &submitInfo, frame.Fence ) );
    static const bool bSerialize = CmdLine::HasArg( "-serializeframes" );
    if ( bSerialize ) {
        vkQueueWaitIdle( graphicsQueue );
    }

    if ( !pendingScreenshot.empty() ) {
        vkQueueWaitIdle( graphicsQueue );
        captureSwapchainImage( pendingScreenshot.c_str() );
        pendingScreenshot.clear();
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR( graphicsQueue, &presentInfo );
    if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
        bSwapchainDirty = true;
    }

    frameIndex = ( frameIndex + 1 ) % kFramesInFlight;
    frameNumber++;
}

VkCommandBuffer RenderDevice::beginImmediateCommands() {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = uploadCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers( device, &allocInfo, &commandBuffer );

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer( commandBuffer, &beginInfo );
    return commandBuffer;
}

void RenderDevice::endImmediateCommands( VkCommandBuffer commandBuffer ) {
    vkEndCommandBuffer( commandBuffer );
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCreateFence( device, &fenceInfo, nullptr, &fence );
    vkQueueSubmit( graphicsQueue, 1, &submitInfo, fence );
    vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX );
    vkDestroyFence( device, fence, nullptr );
    vkFreeCommandBuffers( device, uploadCommandPool, 1, &commandBuffer );
}

GPUTexture *RenderDevice::createTexture2D( uint32_t width,
                                           uint32_t height,
                                           uint32_t numMips,
                                           VkFormat format,
                                           const void *const *ppMipData,
                                           const uint32_t *pMipSizes,
                                           const char *pName ) {
    return createTexture( VK_IMAGE_VIEW_TYPE_2D, width, height, 1, 1, numMips, format, ppMipData, pMipSizes,
                          pName );
}

GPUTexture *RenderDevice::createTexture( VkImageViewType viewType,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t depth,
                                         uint32_t numLayers,
                                         uint32_t numMips,
                                         VkFormat format,
                                         const void *const *ppData,
                                         const uint32_t *pSizes,
                                         const char *pName ) {
    GPUTexture *pTexture = new GPUTexture();
    pTexture->Width = width;
    pTexture->Height = height;
    pTexture->NumMips = numMips;
    pTexture->Format = format;
    pTexture->Name = pName != nullptr ? pName : "";
    pTexture->ViewType = viewType;

    const bool bVolume = viewType == VK_IMAGE_VIEW_TYPE_3D;
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = bVolume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { width, height, bVolume ? depth : 1u };
    imageInfo.mipLevels = numMips;
    imageInfo.arrayLayers = bVolume ? 1u : numLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ( viewType == VK_IMAGE_VIEW_TYPE_CUBE ) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if ( vmaCreateImage( allocator, &imageInfo, &allocInfo, &pTexture->Image,
                         &pTexture->Allocation, nullptr ) != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Failed to create texture '%s' (%ux%u fmt %d)\n", pName, width,
                        height, static_cast<int32_t>( format ) );
        delete pTexture;
        return nullptr;
    }
    const uint32_t numImageLayers = imageInfo.arrayLayers;

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = pTexture->Image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, numMips, 0, numImageLayers };
    vkCreateImageView( device, &viewInfo, nullptr, &pTexture->View );

    // Data order: layer major, then mips.
    const uint32_t numSubresources = numImageLayers * numMips;
    VkDeviceSize totalSize = 0;
    for ( uint32_t i = 0; i < numSubresources; i++ ) {
        totalSize += Align<VkDeviceSize>( pSizes[i], 16 );
    }

    GPUBuffer *pStaging = createBuffer( Max<VkDeviceSize>( totalSize, 16 ),
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, nullptr );
    std::vector<VkBufferImageCopy> regions( numSubresources );
    VkDeviceSize offset = 0;
    for ( uint32_t layer = 0; layer < numImageLayers; layer++ ) {
        for ( uint32_t mip = 0; mip < numMips; mip++ ) {
            const uint32_t i = layer * numMips + mip;
            if ( ppData != nullptr && ppData[i] != nullptr ) {
                memcpy( static_cast<uint8_t *>( pStaging->pMapped ) + offset, ppData[i], pSizes[i] );
            } else {
                memset( static_cast<uint8_t *>( pStaging->pMapped ) + offset, 0, pSizes[i] );
            }
            VkBufferImageCopy &region = regions[i];
            region.bufferOffset = offset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1 };
            region.imageExtent = { Max( width >> mip, 1u ), Max( height >> mip, 1u ),
                                   bVolume ? Max( depth >> mip, 1u ) : 1u };
            offset += Align<VkDeviceSize>( pSizes[i], 16 );
        }
    }
    vmaFlushAllocation( allocator, pStaging->Allocation, 0, VK_WHOLE_SIZE );

    VkCommandBuffer commandBuffer = beginImmediateCommands();
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pTexture->Image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, numMips, 0, numImageLayers };
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                          &barrier );
    vkCmdCopyBufferToImage( commandBuffer, pStaging->Buffer, pTexture->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            static_cast<uint32_t>( regions.size() ), regions.data() );
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                          nullptr, 0, nullptr, 1, &barrier );
    endImmediateCommands( commandBuffer );

    vmaDestroyBuffer( allocator, pStaging->Buffer, pStaging->Allocation );
    delete pStaging;
    return pTexture;
}

void RenderDevice::destroyTexture( GPUTexture *pTexture ) {
    if ( pTexture != nullptr ) {
        frames[frameIndex].PendingTextureDeletes.push_back( pTexture );
    }
}

GPUBuffer *RenderDevice::createBuffer( VkDeviceSize size,
                                       VkBufferUsageFlags usage,
                                       const void *pInitialData ) {
    GPUBuffer *pBuffer = new GPUBuffer();
    pBuffer->Size = size;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocationInfo = {};
    if ( vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &pBuffer->Buffer,
                          &pBuffer->Allocation, &allocationInfo ) != VK_SUCCESS ) {
        delete pBuffer;
        return nullptr;
    }
    pBuffer->pMapped = allocationInfo.pMappedData;
    if ( pInitialData != nullptr && pBuffer->pMapped != nullptr ) {
        memcpy( pBuffer->pMapped, pInitialData, size );
        vmaFlushAllocation( allocator, pBuffer->Allocation, 0, VK_WHOLE_SIZE );
    }
    return pBuffer;
}

void RenderDevice::destroyBuffer( GPUBuffer *pBuffer ) {
    if ( pBuffer != nullptr ) {
        frames[frameIndex].PendingBufferDeletes.push_back( pBuffer );
    }
}

TransientAllocation RenderDevice::allocateTransient( VkDeviceSize size,
                                                     VkDeviceSize alignment ) {
    FrameData &frame = frames[frameIndex];
    VkDeviceSize offset = Align( frame.TransientOffset, alignment );
    TransientAllocation allocation;
    if ( offset + size > frame.TransientBuffer.Size ) {
        OTDU_LOG_WARN( "Transient buffer overflow (%llu bytes requested)\n",
                       static_cast<unsigned long long>( size ) );
        return allocation;
    }
    frame.TransientOffset = offset + size;
    allocation.Buffer = frame.TransientBuffer.Buffer;
    allocation.Offset = offset;
    allocation.pData = static_cast<uint8_t *>( frame.TransientBuffer.pMapped ) + offset;
    return allocation;
}

VkSampler RenderDevice::getSampler( eSamplerFilter filter, eSamplerAddress address ) {
    const uint32_t key = ( static_cast<uint32_t>( filter ) << 8 ) | static_cast<uint32_t>( address );
    auto it = samplers.find( key );
    if ( it != samplers.end() ) {
        return it->second;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    const bool bLinear = filter == eSamplerFilter::SF_Linear;
    samplerInfo.magFilter = bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.minFilter = bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = bLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if ( address == eSamplerAddress::SA_Wrap ) {
        mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    } else if ( address == eSamplerAddress::SA_Mirror ) {
        mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    samplerInfo.addressModeU = mode;
    samplerInfo.addressModeV = mode;
    samplerInfo.addressModeW = mode;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler( device, &samplerInfo, nullptr, &sampler );
    samplers[key] = sampler;
    return sampler;
}

VkDescriptorSet RenderDevice::allocateTextureSet( GPUTexture *const *ppTextures,
                                                  uint32_t numTextures,
                                                  VkSampler sampler ) {
    FrameData &frame = frames[frameIndex];
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = frame.DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if ( vkAllocateDescriptorSets( device, &allocInfo, &set ) != VK_SUCCESS ) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfos[kMaxTexturesPerSet];
    VkWriteDescriptorSet writes[kMaxTexturesPerSet] = {};
    for ( uint32_t i = 0; i < kMaxTexturesPerSet; i++ ) {
        GPUTexture *pTexture = ( i < numTextures ) ? ppTextures[i] : nullptr;
        if ( pTexture == nullptr ) {
            pTexture = ppTextures[0];
        }
        imageInfos[i].sampler = sampler;
        imageInfos[i].imageView = pTexture->View;
        const bool bDepthFormat = pTexture->Format == VK_FORMAT_D32_SFLOAT ||
                                  pTexture->Format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                                  pTexture->Format == VK_FORMAT_D24_UNORM_S8_UINT ||
                                  pTexture->Format == VK_FORMAT_D16_UNORM;
        imageInfos[i].imageLayout =
            bDepthFormat ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets( device, kMaxTexturesPerSet, writes, 0, nullptr );
    return set;
}

VkPipeline RenderDevice::createPipeline( const PipelineDesc &desc ) {
    auto createModule = [&]( const uint32_t *pCode, size_t size ) {
        VkShaderModuleCreateInfo moduleInfo = {};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = size;
        moduleInfo.pCode = pCode;
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule( device, &moduleInfo, nullptr, &module );
        return module;
    };

    VkShaderModule vertexModule = createModule( desc.pVertexSPIRV, desc.VertexSPIRVSize );
    VkShaderModule fragmentModule = createModule( desc.pFragmentSPIRV, desc.FragmentSPIRVSize );

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> bindings;
    for ( uint32_t i = 0; i < desc.BindingStrides.size(); i++ ) {
        bindings.push_back( { i, desc.BindingStrides[i], VK_VERTEX_INPUT_RATE_VERTEX } );
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    for ( const VertexAttributeDesc &attribute : desc.Attributes ) {
        attributes.push_back( { attribute.Location, attribute.Binding, attribute.Format,
                                attribute.Offset } );
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>( bindings.size() );
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>( attributes.size() );
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = desc.Topology;
    inputAssembly.primitiveRestartEnable =
        ( desc.Topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ) ? VK_TRUE : VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = desc.Cull == eCullMode::CM_Back    ? VK_CULL_MODE_BACK_BIT
                          : desc.Cull == eCullMode::CM_Front ? VK_CULL_MODE_FRONT_BIT
                                                             : VK_CULL_MODE_NONE;
    rasterizer.frontFace = desc.bFrontFaceCW ? VK_FRONT_FACE_CLOCKWISE
                                             : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.bDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.bDepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if ( desc.Stencil != eStencilMode::SM_None ) {
        VkStencilOpState op = {};
        op.failOp = VK_STENCIL_OP_KEEP;
        op.depthFailOp = VK_STENCIL_OP_KEEP;
        op.compareMask = 0xff;
        op.writeMask = 0xff;
        op.reference = 0;
        if ( desc.Stencil == eStencilMode::SM_TestEqual ) {
            op.compareOp = VK_COMPARE_OP_EQUAL;
            op.passOp = VK_STENCIL_OP_KEEP;
        } else {
            op.compareOp = VK_COMPARE_OP_ALWAYS;
            op.passOp = desc.Stencil == eStencilMode::SM_Increment ? VK_STENCIL_OP_INCREMENT_AND_CLAMP
                                                                   : VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        }
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.front = op;
        depthStencil.back = op;
    }

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch ( desc.Blend ) {
    case eBlendMode::BM_Alpha:
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case eBlendMode::BM_PremultipliedAlpha:
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case eBlendMode::BM_Additive:
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    default:
        break;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    if ( desc.Stencil == eStencilMode::SM_Increment || desc.Stencil == eStencilMode::SM_Decrement ) {
        blendAttachment.colorWriteMask = 0;
    }
    VkDynamicState dynamicStates[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = desc.Stencil != eStencilMode::SM_None ? 3 : 2;
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
    pipelineInfo.layout = desc.Layout != VK_NULL_HANDLE ? desc.Layout : defaultPipelineLayout;
    pipelineInfo.renderPass = desc.RenderPass != VK_NULL_HANDLE ? desc.RenderPass : mainRenderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineInfo,
                                                 nullptr, &pipeline );
    if ( result != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "vkCreateGraphicsPipelines failed (%d)\n", static_cast<int32_t>( result ) );
    }

    vkDestroyShaderModule( device, vertexModule, nullptr );
    vkDestroyShaderModule( device, fragmentModule, nullptr );
    return pipeline;
}

void RenderDevice::destroyPipeline( VkPipeline pipeline ) {
    if ( pipeline != VK_NULL_HANDLE ) {
        vkDeviceWaitIdle( device );
        vkDestroyPipeline( device, pipeline, nullptr );
    }
}

void RenderDevice::captureSwapchainImage( const char *pPath ) {
    const uint32_t width = swapchainExtent.width;
    const uint32_t height = swapchainExtent.height;
    GPUBuffer *pReadback = createBuffer( static_cast<VkDeviceSize>( width ) * height * 4,
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
    if ( pReadback == nullptr ) {
        return;
    }

    VkCommandBuffer commandBuffer = beginImmediateCommands();
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainImages[imageIndex];
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                          &barrier );
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyImageToBuffer( commandBuffer, swapchainImages[imageIndex],
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pReadback->Buffer, 1,
                            &region );
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                          1, &barrier );
    endImmediateCommands( commandBuffer );
    vmaInvalidateAllocation( allocator, pReadback->Allocation, 0, VK_WHOLE_SIZE );

    // Binary PPM (no dependency), converted from BGRA if needed.
    std::ofstream stream( pPath, std::ios::binary );
    stream << "P6\n" << width << " " << height << "\n255\n";
    const uint8_t *pPixels = static_cast<const uint8_t *>( pReadback->pMapped );
    const bool bBGRA = swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                       swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
    std::vector<uint8_t> row( width * 3 );
    for ( uint32_t y = 0; y < height; y++ ) {
        for ( uint32_t x = 0; x < width; x++ ) {
            const uint8_t *p = pPixels + ( static_cast<size_t>( y ) * width + x ) * 4;
            row[x * 3 + 0] = bBGRA ? p[2] : p[0];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = bBGRA ? p[0] : p[2];
        }
        stream.write( reinterpret_cast<const char *>( row.data() ), row.size() );
    }
    OTDU_LOG_INFO( "Screenshot saved to '%s'\n", pPath );

    vmaDestroyBuffer( allocator, pReadback->Buffer, pReadback->Allocation );
    delete pReadback;
}

bool RenderDevice::saveScreenshot( const char *pPath ) {
    requestScreenshot( pPath );
    return true;
}
