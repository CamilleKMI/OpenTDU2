# Third-party dependencies.
#
# By default every dependency is fetched from GitHub at configure time (pinned
# tags). To build offline, clone them yourself and pass
#   -DOTDU_DEPS_DIR=/path/to/deps
# where the folder contains SDL/ volk/ VMA/ Vulkan-Headers/ SPIRV-Headers/
# JoltPhysics/ imgui/ stb/ earcut/.
include( FetchContent )

set( OTDU_DEPS_DIR "" CACHE PATH "Folder with pre-cloned third-party dependencies" )

# 'subdir' is the folder holding the CMakeLists.txt to add (use
# "otdu_no_cmake" for header/source-only dependencies we build ourselves).
macro( otdu_declare_dependency name repo tag folder subdir )
    if( OTDU_DEPS_DIR AND EXISTS "${OTDU_DEPS_DIR}/${folder}" )
        FetchContent_Declare( ${name}
            SOURCE_DIR "${OTDU_DEPS_DIR}/${folder}"
            SOURCE_SUBDIR ${subdir} )
    else()
        string( LENGTH "${tag}" _otdu_tag_length )
        if( _otdu_tag_length EQUAL 40 )
            set( _otdu_shallow FALSE )
        else()
            set( _otdu_shallow TRUE )
        endif()
        FetchContent_Declare( ${name}
            GIT_REPOSITORY ${repo}
            GIT_TAG ${tag}
            GIT_SHALLOW ${_otdu_shallow}
            GIT_PROGRESS TRUE
            SOURCE_SUBDIR ${subdir} )
    endif()
endmacro()

otdu_declare_dependency( sdl3 https://github.com/libsdl-org/SDL.git release-3.4.12 SDL . )
otdu_declare_dependency( volk https://github.com/zeux/volk.git vulkan-sdk-1.4.350.0 volk otdu_no_cmake )
otdu_declare_dependency( vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0 VMA otdu_no_cmake )
otdu_declare_dependency( vulkan_headers https://github.com/KhronosGroup/Vulkan-Headers.git vulkan-sdk-1.4.350.0 Vulkan-Headers otdu_no_cmake )
otdu_declare_dependency( spirv_headers https://github.com/KhronosGroup/SPIRV-Headers.git vulkan-sdk-1.4.350.0 SPIRV-Headers otdu_no_cmake )
otdu_declare_dependency( jolt https://github.com/jrouwe/JoltPhysics.git v5.4.0 JoltPhysics Build )
otdu_declare_dependency( imgui https://github.com/ocornut/imgui.git v1.92.0 imgui otdu_no_cmake )
otdu_declare_dependency( earcut https://github.com/mapbox/earcut.hpp.git v2.2.4 earcut otdu_no_cmake )
otdu_declare_dependency( stb https://github.com/nothings/stb.git a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d stb otdu_no_cmake )

# Header-only / source-only dependencies: only populate them.
FetchContent_MakeAvailable( vulkan_headers spirv_headers imgui stb volk vma earcut )

add_library( otdu_vulkan_headers INTERFACE )
target_include_directories( otdu_vulkan_headers INTERFACE "${vulkan_headers_SOURCE_DIR}/include" )

add_library( otdu_spirv_headers INTERFACE )
target_include_directories( otdu_spirv_headers INTERFACE "${spirv_headers_SOURCE_DIR}/include" )

# volk (Vulkan meta-loader): compiled directly so we control the defines.
add_library( otdu_volk STATIC "${volk_SOURCE_DIR}/volk.c" )
target_include_directories( otdu_volk PUBLIC "${volk_SOURCE_DIR}" )
target_compile_definitions( otdu_volk PUBLIC VK_NO_PROTOTYPES )
if( APPLE )
    target_compile_definitions( otdu_volk PUBLIC VK_USE_PLATFORM_METAL_EXT VK_ENABLE_BETA_EXTENSIONS )
endif()
target_link_libraries( otdu_volk PUBLIC otdu_vulkan_headers ${CMAKE_DL_LIBS} )

add_library( otdu_vma INTERFACE )
target_include_directories( otdu_vma INTERFACE "${vma_SOURCE_DIR}/include" )

add_library( otdu_earcut INTERFACE )
target_include_directories( otdu_earcut INTERFACE "${earcut_SOURCE_DIR}/include" )

add_library( otdu_stb INTERFACE )
target_include_directories( otdu_stb INTERFACE "${stb_SOURCE_DIR}" )

# SDL3 (static)
set( SDL_SHARED OFF CACHE BOOL "" FORCE )
set( SDL_STATIC ON CACHE BOOL "" FORCE )
set( SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE )
set( SDL_TESTS OFF CACHE BOOL "" FORCE )
set( SDL_EXAMPLES OFF CACHE BOOL "" FORCE )
set( SDL_RENDER_GPU OFF CACHE BOOL "" FORCE )
set( SDL_GPU OFF CACHE BOOL "" FORCE )
set( SDL_CAMERA OFF CACHE BOOL "" FORCE )
set( SDL_OPENGL OFF CACHE BOOL "" FORCE )
set( SDL_OPENGLES OFF CACHE BOOL "" FORCE )
set( SDL_RENDER OFF CACHE BOOL "" FORCE )
set( SDL_VULKAN ON CACHE BOOL "" FORCE )
FetchContent_MakeAvailable( sdl3 )

# Jolt Physics
set( TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE )
set( TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE )
set( TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE )
set( TARGET_SAMPLES OFF CACHE BOOL "" FORCE )
set( TARGET_VIEWER OFF CACHE BOOL "" FORCE )
set( ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE )
set( INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE )
set( USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE )
set( CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE )
set( OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE )
FetchContent_MakeAvailable( jolt )

# Dear ImGui (debug overlay)
add_library( otdu_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp" )
target_include_directories( otdu_imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" )
target_link_libraries( otdu_imgui PUBLIC SDL3::SDL3-static )
