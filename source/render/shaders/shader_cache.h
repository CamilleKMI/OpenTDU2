#pragma once

#include "render/shaders/dxso_translator.h"

// On-disk cache of DXSO -> SPIR-V translations (<user data>/shadercache/).
// The .sob files are always the source of truth: the cache is keyed by the
// hash of the original D3D9 bytecode and only saves translation time.
namespace ShaderCache {
void SetEnabled( bool bEnabled );
bool GetOrTranslate( const uint8_t *pBytecode,
                     size_t size,
                     const char *pDebugName,
                     TranslatedShader &outShader );
} // namespace ShaderCache
