#include "shared.h"
#include "shader_cache.h"

#include "filesystem/gs_file.h"

namespace {
constexpr uint32_t kCacheMagic = 0x32555444; // DTU2
constexpr uint32_t kCacheVersion = 4;

bool gbCacheEnabled = true;
std::mutex gCacheMutex;

uint64_t HashBytes( const uint8_t *p, size_t size ) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for ( size_t i = 0; i < size; i++ ) {
        hash ^= p[i];
        hash *= 0x100000001b3ull;
    }
    return hash ^ ( static_cast<uint64_t>( size ) << 40 );
}

std::filesystem::path GetCachePath( uint64_t hash ) {
    char filename[32];
    snprintf( filename, sizeof( filename ), "%016llx.spvcache",
              static_cast<unsigned long long>( hash ) );
    return gpFile->getUserDataFolder() / "shadercache" / filename;
}

template <typename T> void WritePod( std::ofstream &stream, const T &value ) {
    stream.write( reinterpret_cast<const char *>( &value ), sizeof( T ) );
}
template <typename T> bool ReadPod( std::ifstream &stream, T &value ) {
    return static_cast<bool>(
        stream.read( reinterpret_cast<char *>( &value ), sizeof( T ) ) );
}

bool ReadCache( const std::filesystem::path &path, TranslatedShader &shader ) {
    std::ifstream stream( path, std::ios::binary );
    if ( !stream.is_open() ) {
        return false;
    }
    uint32_t magic = 0, version = 0, stage = 0;
    if ( !ReadPod( stream, magic ) || !ReadPod( stream, version ) ||
         magic != kCacheMagic || version != kCacheVersion ) {
        return false;
    }
    ReadPod( stream, stage );
    shader.Stage = static_cast<eShaderType>( stage );
    ReadPod( stream, shader.InputMask );
    ReadPod( stream, shader.OutputMask );
    ReadPod( stream, shader.UsedSamplers );
    ReadPod( stream, shader.PushConstantsSize );
    ReadPod( stream, shader.MaxDefinedConstant );
    ReadPod( stream, shader.BytecodeHash );

    uint32_t numConstants = 0;
    ReadPod( stream, numConstants );
    shader.Constants.resize( numConstants );
    for ( ShaderConstantInfo &constant : shader.Constants ) {
        uint32_t nameLength = 0;
        ReadPod( stream, nameLength );
        constant.Name.resize( nameLength );
        stream.read( constant.Name.data(), nameLength );
        ReadPod( stream, constant.RegisterSet );
        ReadPod( stream, constant.RegisterIndex );
        ReadPod( stream, constant.RegisterCount );
    }

    uint32_t numInputs = 0;
    ReadPod( stream, numInputs );
    shader.Inputs.resize( numInputs );
    for ( ShaderInputInfo &input : shader.Inputs ) {
        ReadPod( stream, input );
    }

    uint32_t numDwords = 0;
    ReadPod( stream, numDwords );
    shader.SPIRV.resize( numDwords );
    stream.read( reinterpret_cast<char *>( shader.SPIRV.data() ),
                 numDwords * sizeof( uint32_t ) );
    return static_cast<bool>( stream ) && numDwords > 0;
}

void WriteCache( const std::filesystem::path &path,
                 const TranslatedShader &shader ) {
    std::error_code ec;
    std::filesystem::create_directories( path.parent_path(), ec );
    std::ofstream stream( path, std::ios::binary );
    if ( !stream.is_open() ) {
        return;
    }
    WritePod( stream, kCacheMagic );
    WritePod( stream, kCacheVersion );
    WritePod( stream, static_cast<uint32_t>( shader.Stage ) );
    WritePod( stream, shader.InputMask );
    WritePod( stream, shader.OutputMask );
    WritePod( stream, shader.UsedSamplers );
    WritePod( stream, shader.PushConstantsSize );
    WritePod( stream, shader.MaxDefinedConstant );
    WritePod( stream, shader.BytecodeHash );
    WritePod( stream, static_cast<uint32_t>( shader.Constants.size() ) );
    for ( const ShaderConstantInfo &constant : shader.Constants ) {
        WritePod( stream, static_cast<uint32_t>( constant.Name.size() ) );
        stream.write( constant.Name.data(), constant.Name.size() );
        WritePod( stream, constant.RegisterSet );
        WritePod( stream, constant.RegisterIndex );
        WritePod( stream, constant.RegisterCount );
    }
    WritePod( stream, static_cast<uint32_t>( shader.Inputs.size() ) );
    for ( const ShaderInputInfo &input : shader.Inputs ) {
        WritePod( stream, input );
    }
    WritePod( stream, static_cast<uint32_t>( shader.SPIRV.size() ) );
    stream.write( reinterpret_cast<const char *>( shader.SPIRV.data() ),
                  shader.SPIRV.size() * sizeof( uint32_t ) );
}
} // namespace

void ShaderCache::SetEnabled( bool bEnabled ) { gbCacheEnabled = bEnabled; }

bool ShaderCache::GetOrTranslate( const uint8_t *pBytecode,
                                  size_t size,
                                  const char *pDebugName,
                                  TranslatedShader &outShader ) {
    if ( pBytecode == nullptr || size == 0 ) {
        return false;
    }

    const bool bUseCache = gbCacheEnabled && gpFile != nullptr &&
                           !gpFile->getUserDataFolder().empty();
    const uint64_t hash = HashBytes( pBytecode, size );
    if ( bUseCache ) {
        std::lock_guard<std::mutex> lock( gCacheMutex );
        if ( ReadCache( GetCachePath( hash ), outShader ) ) {
            return true;
        }
    }

    if ( !DXSO::Translate( pBytecode, size, pDebugName, outShader ) ) {
        return false;
    }

    if ( bUseCache ) {
        std::lock_guard<std::mutex> lock( gCacheMutex );
        WriteCache( GetCachePath( hash ), outShader );
    }
    return true;
}
