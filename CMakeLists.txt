cmake_minimum_required( VERSION 3.24 )

project( OpenTDU2 VERSION 0.1.0 LANGUAGES C CXX )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED True )
set( CMAKE_POSITION_INDEPENDENT_CODE ON )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

if( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
    set( CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE )
endif()

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" )

set( OTDU_SRC_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/source" )
set( OTDU_THIRDPARTY_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/third_party" )

option( OTDU_DEVBUILD "Developer build (asserts on unimplemented functions)" OFF )

include( cmake/Dependencies.cmake )
add_subdirectory( "${OTDU_THIRDPARTY_FOLDER}/dxvk" )

# Engine (everything but the entry point) is built as a static library so the
# tools can reuse the asset readers.
file( GLOB_RECURSE OTDU_ENGINE_SRC CONFIGURE_DEPENDS
    "${OTDU_SRC_FOLDER}/*.cpp"
    "${OTDU_SRC_FOLDER}/*.h" )
list( FILTER OTDU_ENGINE_SRC EXCLUDE REGEX ".*/source/main\\.cpp$" )

add_library( otdu_engine STATIC ${OTDU_ENGINE_SRC} )
target_include_directories( otdu_engine PUBLIC "${OTDU_SRC_FOLDER}" )
target_link_libraries( otdu_engine PUBLIC
    otdu_dxso otdu_volk otdu_vma otdu_stb otdu_earcut otdu_imgui
    otdu_vulkan_headers SDL3::SDL3-static Jolt )
target_precompile_headers( otdu_engine PRIVATE "${OTDU_SRC_FOLDER}/pch.h" )
if( OTDU_DEVBUILD )
    target_compile_definitions( otdu_engine PUBLIC OTDU_DEVBUILD )
endif()
if( MSVC )
    target_compile_options( otdu_engine PRIVATE /W3 )
    target_compile_definitions( otdu_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX )
else()
    target_compile_options( otdu_engine PRIVATE -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces )
endif()

add_executable( opentdu2 "${OTDU_SRC_FOLDER}/main.cpp" )
target_link_libraries( opentdu2 PRIVATE otdu_engine )
if( APPLE )
    # Let volk find libvulkan/MoltenVK from the LunarG Vulkan SDK.
    if( DEFINED ENV{VULKAN_SDK} )
        set_target_properties( opentdu2 PROPERTIES BUILD_RPATH "$ENV{VULKAN_SDK}/lib;/usr/local/lib;/opt/homebrew/lib" )
    else()
        set_target_properties( opentdu2 PROPERTIES BUILD_RPATH "/usr/local/lib;/opt/homebrew/lib" )
    endif()
endif()

add_subdirectory( tools )
