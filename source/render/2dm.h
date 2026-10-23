#pragma once

#include "core/math.h"
#include "render/render_file.h"

// .2DM (TDU2, version 4.0): material library.
//
//   MATA: HASH + MAT. sections, then MTPA / UVAA (animations), REAL.
//   MAT. payload:
//     +0x000 u64  Hashcode
//     +0x010 char Name[32]
//     +0x030 u8   DepthTest, DepthWrite, ?, CullMode (D3DCULL), ?, AlphaTestEnable, ?, AlphaRef
//     +0x038 u8   SrcBlend, DstBlend (D3DBLEND), BlendOp
//     +0x0b0 float4 Diffuse, Ambient, Specular, Emissive
//     +0x0fc u8   NumTextureSlots
//     +0x100 TextureSlot[n] { u64 SlotType ("DETAIL", "NORMAL", "GLOSS", "CUBEMAP"...);
//                             u64 TextureHashcode; u32 runtime; u32; u8 filter/address... }
//     +0x300 u64  ShaderSet GUID (.sob family), +0x308 shader version
//     +0x310 u8   spec[48] (shader permutation switches, see EdenShaderSystem)
//     +0x340 ParameterDesc[N] { u64 Name ("RAMPCLR1", "MATPARAM"...); u16 Type (2: float4);
//                               u16 Flags; u32 Size (0x10) }
//            followed by the N parameter values (float4 each).
struct MaterialTextureSlot {
    uint64_t SlotType = 0;
    uint64_t TextureHashcode = 0;
    // Slot +0x18 / +0x19: texture addressing U / V, D3DTADDRESS numbering
    // (1 wrap, 2 mirror, 3 clamp) [DATA: 1, 2, 3 all used; ocean runtime
    // material FUN_008be1e0 writes 1 for a world-tiled map, 3 for a projected
    // one (ocean_fix_spec)]; 0 = not set.
    uint8_t AddressU = 0;
    uint8_t AddressV = 0;
};

struct MaterialDesc {
    uint64_t Hashcode = 0;
    std::string Name;
    uint8_t DepthTest = 1;
    uint8_t DepthWrite = 1;
    uint8_t CullMode = 1;   // D3DCULL_NONE = 1, CW = 2, CCW = 3
    uint8_t AlphaTest = 0;
    uint8_t AlphaRef = 0;
    uint8_t SrcBlend = 2;   // D3DBLEND_ONE
    uint8_t DstBlend = 1;   // D3DBLEND_ZERO
    std::vector<MaterialTextureSlot> Textures;    // Non empty slots
    std::vector<MaterialTextureSlot> AllTextures; // Every slot (slot i = sampler s<i>)
    std::unordered_map<uint64_t, Vector4f> Parameters;
    // D3DMATERIAL9 like colors (+0xb0)
    Vector4f DiffuseColor = Vector4f( 1, 1, 1, 1 );
    Vector4f AmbientColor = Vector4f( 1, 1, 1, 1 );
    Vector4f SpecularColor = Vector4f( 0, 0, 0, 1 );
    Vector4f EmissiveColor = Vector4f( 0, 0, 0, 1 );
    // Shader family (ShaderSet guid of a .sob, +0x300) and switches (+0x310)
    uint64_t ShaderSetGUID = 0;
    uint8_t ShaderVersion[2] = { 0, 0 };
    uint8_t Spec[48] = {};
    uint8_t States[16] = {}; // Raw render states (MAT. +0x30)

    bool isAlphaBlended() const { return !( SrcBlend == 2 && DstBlend == 1 ); }
    bool isAdditive() const { return DstBlend == 2; }
    uint64_t findTexture( const char *pSlotType ) const;
    bool getParameter( const char *pName, Vector4f &outValue ) const;
};

class Render2DM {
public:
    inline const std::unordered_map<uint64_t, MaterialDesc> &getMaterials() const {
        return materials;
    }

public:
    bool parse( const uint8_t *pFile, uint32_t fileSize );
    const MaterialDesc *find( uint64_t hashcode ) const;
    // Every material in file order (variants may share a hashcode: avatar
    // colour palettes "<clothe>_mat.2dm").
    inline const std::vector<MaterialDesc> &getOrderedMaterials() const { return orderedMaterials; }
    // UVAA section: ids of the texture coordinate animations (.UVA files,
    // ANM! header id) the library uses.
    inline const std::vector<uint64_t> &getUVAnimationIds() const { return uvAnimationIds; }

private:
    std::unordered_map<uint64_t, MaterialDesc> materials;
    std::vector<MaterialDesc> orderedMaterials;
    std::vector<uint64_t> uvAnimationIds;

    void parseMaterial( const uint8_t *pPayload, uint32_t payloadSize );
};
