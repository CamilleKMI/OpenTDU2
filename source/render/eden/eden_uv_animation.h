#pragma once

#include "core/math.h"

// Texture coordinate animation (.UVA, an ANM! file with one MAT! track)
// [DATA, decoded from the bank files: caustic_0, Oasis_02_*, artWorks_09_0
// (8x8 flipbook), Water_falls_Detail_2, Pulse_DarkBlue_*]:
//
//   "ANM!" u32 0x10, f32 1.6, u32 0xce, u64 id (= a UVA. entry of the
//   material library UVAA section), "DATA" u32 size,
//   u32 numTracks (1), f32 duration, f32 duration, u32[3] 0, u32 0x1f, u32 0x64,
//   "MAT!" u32 1, u32 0, u32 0, u32 0,
//   4 channels { u32 n; f32 length; f32 times[n]; f32 values[n] }:
//   U offset, V offset, U scale, V scale.
//
// File name: <material>[_<SlotName>]_<slot index>.UVA [INF: every bank file
// follows it; the material hashcode is the identifier of <material>].
// The slot index k selects the "__UVA_k" vertex shader matrix, rows
// (su, 0, 0, offU), (0, sv, 0, offV) dotted with (u, v, 1) [SHADER TD_Water
// vs 46: uvN0 = TC0 * __UVA_0, uvN1 = TC1 * __UVA_1]. Keys are interpolated
// linearly, looping on the duration [INF].
struct EdenUVAnimation {
    struct Channel {
        std::vector<float> Times;
        std::vector<float> Values;
        float evaluate( float time, float defaultValue ) const;
    };

    uint64_t Id = 0;
    float Duration = 0.0f;
    Channel Channels[4]; // offU, offV, scaleU, scaleV

    bool parse( const uint8_t *pData, size_t size );
    // (offU, offV, scaleU, scaleV) at 'time' (seconds, looping).
    Vector4f evaluate( float time ) const;
    // The 4 registers of __UVA_k at 'time'.
    void evaluateRegisters( float time, Vector4f outRegisters[4] ) const;

    // Splits a .UVA bank entry name ("caustic_Normal1_1") into the slot
    // index (1) and the candidate material names ("caustic_Normal1",
    // "caustic"). False if the name does not end with "_<digits>".
    static bool ParseName( const std::string &name, uint32_t &outSlot, std::vector<std::string> &outMaterials );
};
