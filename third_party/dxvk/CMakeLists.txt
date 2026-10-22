# Subset of DXVK (zlib/libpng license, see LICENSE) used to translate D3D9
# shader bytecode (DXSO, stored in TDU2 .sob files) to SPIR-V at runtime.
# Sources come from the OpenTDU fork (modified dxso module, see README.md).
file( GLOB OTDU_DXVK_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dxso/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/spirv/*.cpp" )

add_library( otdu_dxso STATIC ${OTDU_DXVK_SRC} )
target_include_directories( otdu_dxso PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" )
target_include_directories( otdu_dxso PRIVATE "${OTDU_SRC_FOLDER}" )
target_link_libraries( otdu_dxso PUBLIC otdu_vulkan_headers otdu_spirv_headers )
target_compile_features( otdu_dxso PUBLIC cxx_std_17 )
if( NOT MSVC )
    target_compile_options( otdu_dxso PRIVATE -w )
endif()
