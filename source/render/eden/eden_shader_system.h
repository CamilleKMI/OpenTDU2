#pragma once

#include "render/shaders/dxso_translator.h"
#include "render/shaders/shader_library.h"

// Material shader selection of the original renderer (edRenderShaderSet /
// GSRender_ShaderMng.cpp in TestDrive2.exe).
//
// A .2DM material references its shader family by GUID (MAT. +0x300, the
// ShaderSet guid of a .sob) and stores its 48 "spec" switches at +0x310:
//
//   0 S_NBTEXTURES  1 S_COORDTEXTURE 2 S_USEGLOSSMAP 3 S_USEREFLECTION
//   4 S_USEAMBIENT  5 S_USEDIFFUSE   6 S_USESPECULAR 7 S_USEEMISSIVE
//   8 S_UVMATRIXMASK 9 S_USESKINNING 10 S_NBPRELIGHT 11 S_FOG
//   12 S_NORMAL 13 S_TANGENT 14 S_BINORMAL 15 S_TANGENTSPACE
//   16 S_PASS_Z 17 S_PASS_SHADOWCAST 18 S_PASS_SHADOWRECEIVE 19 S_MODE
//   20 S_LOD 21 S_USEALPHATEST 22..31 PAD 32..47 S_USERTYPE1..16
//
// (table at 0x00f91468). FUN_008a8950 copies the switches, lets the scene
// type handler adjust them, then FUN_008ae980 hashes them (one hash per LOD,
// spec[20]) and looks the permutation up in each profile ("Item" id).
//
// The hash function (FUN_0041bbc0) and the scene handler live in the
// SecuROM encrypted part of the executable. The hash was recovered from the
// libraries themselves (26492 permutations store both their switches and
// their id): it is GF(2) linear,
//     R = 0; for each byte b: R = S^8(R) ^ (b << 32)
//     S(R) = rotl64(R, 1) ^ (bit31(R) ? 0x00f0d50a00600340 : 0)
//     id = R ^ 0x6422fb175d04f76b
// which matches every stored permutation. The handler sets S_NBTEXTURES to
// the number of texture slots in use (including runtime textures such as the
// sector shadow maps) and S_LOD; a few families have extra rules (see
// EdenRenderer::updateProgram).
struct EdenProgram {
    std::string Name;
    uint64_t ShaderSetGUID = 0;
    uint64_t ItemId = 0;
    uint32_t VSAttrib = 0;
    uint32_t PSAttrib = 0;
    const TranslatedShader *pVS = nullptr;
    const TranslatedShader *pPS = nullptr;
    std::vector<ShaderConstantMember> VSMembers;
    std::vector<ShaderConstantMember> PSMembers;
    uint8_t VSSamplerTypes[16] = {};
    uint8_t PSSamplerTypes[16] = {};
    uint32_t VSSamplerMask = 0;
    uint32_t PSSamplerMask = 0;
    uint32_t NumDifferences = 0; // spec switches that differ from the material
};

class EdenShaderSystem {
public:
    static constexpr uint32_t kNumSpecs = 48;

    enum eSpec : uint32_t {
        S_NBTEXTURES = 0,
        S_COORDTEXTURE = 1,
        S_USESKINNING = 9,
        S_FOG = 11,
        S_PASS_Z = 16,
        S_PASS_SHADOWCAST = 17,
        S_PASS_SHADOWRECEIVE = 18,
        S_MODE = 19,
        S_LOD = 20,
        S_USEALPHATEST = 21,
    };

public:
    EdenShaderSystem();
    ~EdenShaderSystem();

    // Loads every shader library of Euro/Shaders.
    bool initialize();
    void destroy();

    ShaderLibrary *findLibrary( uint64_t guid );
    ShaderLibrary *findLibrary( const char *pName );

    // Permutation of a material (nullptr if the family is unknown).
    const EdenProgram *getProgram( uint64_t guid, const uint8_t *pSpec );
    // Explicit attribs (screen space / helper shaders).
    const EdenProgram *getProgram( const char *pLibrary, uint32_t vsAttrib, uint32_t psAttrib );

    static const char *GetSpecName( uint32_t index );
    static uint64_t HashSpec( const uint8_t *pSpec );

private:
    std::vector<std::unique_ptr<ShaderLibrary>> libraries;
    std::unordered_map<uint64_t, ShaderLibrary *> librariesByGUID;
    std::unordered_map<std::string, ShaderLibrary *> librariesByName;
    std::unordered_map<std::string, std::unique_ptr<EdenProgram>> programs;

private:
    EdenProgram *buildProgram( ShaderLibrary *pLibrary, uint32_t vsAttrib, uint32_t psAttrib );
};
