#pragma once

struct EdenMaterial;

// Material preparation of the original renderer (TestDrive2.exe
// FUN_00d71900): the shader set GUID of the material (MAT. +0x300) selects a
// handler in the table at 0x00f8fa10 { u64 GUID; handler(MAT *, Key *) }
// which rewrites the permutation switches (MAT. +0x310), sometimes the GUID
// itself (materials exported with a family that has no library of its own,
// e.g. 1b38348331d3942a -> TD_CarPaint_NM) and the render states (MAT.
// +0x30), then returns the render bucket. Blended materials that do not end
// up in the car transparency buckets (0x2b, 0x2c) do not write depth.
//
// Key offsets in the handlers: +0x00 GUID, +0x10 + i = switch i
// (S_NBTEXTURES, S_COORD, S_GLOSS... S_USERTYPE1 = +0x30).
// MAT offsets: +0x30 render states { DepthTest, DepthWrite (1 on, 2 off), ?,
// Cull, ?, AlphaTestDisable, ?, AlphaRef, SrcBlend, DstBlend, BlendOp (6:
// no blending), ... }, +0xfc number of texture slots, +0x100 + 0x20 i slots
// { u64 type; u64 texture hashcode (1 / -1: runtime texture); ... }.
//
// Returns the render bucket.
int32_t ApplyEdenMaterialHandler( EdenMaterial &material );
