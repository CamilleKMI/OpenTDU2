#!/bin/sh
# Configure and build OpenTDU2 on macOS (also works on Linux).
#   ./build_macos.sh [Debug|Release|RelWithDebInfo]
set -e
cd "$(dirname "$0")"
BUILD_TYPE="${1:-Release}"

if [ -z "$VULKAN_SDK" ] && [ -d "$HOME/VulkanSDK" ]; then
    SDK_VERSION="$(ls "$HOME/VulkanSDK" | sort -V | tail -n 1)"
    if [ -d "$HOME/VulkanSDK/$SDK_VERSION/macOS" ]; then
        export VULKAN_SDK="$HOME/VulkanSDK/$SDK_VERSION/macOS"
        echo "Using Vulkan SDK: $VULKAN_SDK"
    fi
fi
if [ -z "$VULKAN_SDK" ] && [ "$(uname)" = "Darwin" ]; then
    echo "warning: Vulkan SDK not found (https://vulkan.lunarg.com/sdk/home#mac)."
    echo "         The build works without it, but the game needs MoltenVK at runtime."
fi

GENERATOR=""
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="-G Ninja"
fi

cmake -S . -B build $GENERATOR -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build --parallel

echo
echo "Done. Run: ./build/bin/opentdu2            (game data auto-detected in the parent folders)"
echo "      or:  ./build/bin/opentdu2 -game /path/to/TestDrive2"
