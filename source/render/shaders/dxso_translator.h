#pragma once

#include "render/render_api.h"

// Constant declared in the CTAB comment block of a D3D9 shader (the HLSL
// compiler output kept by Eden in the .sob libraries).
struct ShaderConstantInfo {
    std::string Name;
    uint16_t RegisterSet = 0; // D3DXREGISTER_SET (0: bool, 1: int4, 2: float4, 3: sampler)
    uint16_t RegisterIndex = 0;
    uint16_t RegisterCount = 0;
};

struct ShaderInputInfo {
    uint8_t Usage = 0; // D3DDECLUSAGE
    uint8_t UsageIndex = 0;
    uint8_t Location = 0;
};

struct TranslatedShader {
    eShaderType Stage = eShaderType::ST_Invalid;
    std::vector<uint32_t> SPIRV;
    std::vector<ShaderConstantInfo> Constants;
    std::vector<ShaderInputInfo> Inputs; // VS only
    uint32_t InputMask = 0;
    uint32_t OutputMask = 0;
    uint32_t UsedSamplers = 0;
    uint32_t PushConstantsSize = 0;
    uint32_t MaxDefinedConstant = 0;
    uint64_t BytecodeHash = 0;
};

// Flattened CTAB entry: top-level constants and members of the Eden
// parameter structs (Var_VS, Var_PS, Var_Scene_VS...).
struct ShaderConstantMember {
    std::string Name;       // member name ("__World_View_Proj__float4x4") or constant name
    std::string StructName; // owning struct ("Var_VS"), empty for top-level constants
    uint16_t RegisterSet = 0; // D3DXREGISTER_SET
    uint16_t Register = 0;
    uint16_t NumRegisters = 0; // registers actually allocated by the compiler
    uint8_t Class = 0;         // D3DXPARAMETER_CLASS (0 scalar, 1 vector, 2 rows, 3 columns)
    uint8_t Rows = 1;
    uint8_t Columns = 1;
    uint16_t Elements = 1;
};

namespace DXSO {
// Parses the CTAB comment and flattens the struct members.
bool ParseConstantMembers( const uint8_t *pBytecode, size_t size,
                           std::vector<ShaderConstantMember> &outMembers );

// Texture type of each sampler declared by dcl (2: 2D, 3: cube, 4: volume).
void ParseSamplerTypes( const uint8_t *pBytecode, size_t size, uint8_t outTypes[16], uint32_t &outMask );

// Parses the CTAB comment of a D3D9 bytecode stream.
bool ParseConstantTable( const uint8_t *pBytecode,
                         size_t size,
                         std::vector<ShaderConstantInfo> &outConstants );

// Returns the size (in bytes) of a D3D9 bytecode stream (up to and including
// the END token), or 0 if invalid.
size_t GetBytecodeSize( const uint8_t *pBytecode, size_t maxSize );

// Translates D3D9 shader model 2/3 bytecode to SPIR-V (through DXVK's DXSO
// compiler).
bool Translate( const uint8_t *pBytecode,
                size_t size,
                const char *pDebugName,
                TranslatedShader &outShader );
} // namespace DXSO
