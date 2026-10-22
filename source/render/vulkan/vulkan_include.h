#pragma once

// volk replaces the Vulkan prototypes by function pointers loaded at runtime
// (the Vulkan loader / MoltenVK is located through SDL).
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#define OTDU_VK_CHECK( expr )                                                  \
    do {                                                                       \
        VkResult _otduResult = ( expr );                                       \
        if ( _otduResult != VK_SUCCESS ) {                                     \
            OTDU_LOG_ERROR( "%s failed (VkResult %d)\n", #expr,                \
                            static_cast<int32_t>( _otduResult ) );             \
        }                                                                      \
    } while ( 0 )
