#include "dxso_util.h"

#include "dxso_include.h"

namespace dxvk {

  dxvk::mutex                  g_linkerSlotMutex;
  uint32_t                     g_linkerSlotCount = 0;
  std::array<DxsoSemantic, 32> g_linkerSlots;

  // OpenTDU2: deterministic semantic -> location mapping. The original DXVK
  // implementation assigns slots in registration order (process wide), which
  // breaks VS/PS linking as soon as translations come from the on-disk shader
  // cache or from another process (the .sob libraries contain thousands of
  // independent permutations).
  uint32_t RegisterLinkerSlot(DxsoSemantic semantic) {
    const uint32_t index = semantic.usageIndex;
    switch (semantic.usage) {
      case DxsoUsage::Texcoord:     return index < 10 ? index : 24 + (index & 3);
      case DxsoUsage::Color:        return index < 2 ? 10 + index : 28;
      case DxsoUsage::Normal:       return index == 0 ? 12 : 29;
      case DxsoUsage::Fog:          return 13;
      case DxsoUsage::Tangent:      return 14;
      case DxsoUsage::Binormal:     return 15;
      case DxsoUsage::Position:     return index == 0 ? 16 : 17;
      case DxsoUsage::PositionT:    return 17;
      case DxsoUsage::BlendWeight:  return 18;
      case DxsoUsage::BlendIndices: return 19;
      case DxsoUsage::PointSize:    return 20;
      case DxsoUsage::Depth:        return 21;
      case DxsoUsage::TessFactor:   return 22;
      default:                      return 23;
    }
  }

}