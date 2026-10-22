#pragma once

#include "game/gs_base.h"
#include "render/eden/eden_renderer.h"
#include "render/renderer_2d.h"
#include "render/texture_cache.h"
#include "render/vulkan/render_device.h"

struct SDL_Window;

// Render service: owns the Vulkan device, the texture cache and the
// renderers (2D batches, 3D scene).
class GSRender : public GameSystem {
public:
    inline RenderDevice &getDevice() { return device; }
    inline TextureCache &getTextureCache() { return textureCache; }
    inline Renderer2D &get2DRenderer() { return renderer2D; }
    // Original shaders renderer (nullptr if unavailable, e.g. -nosob)
    inline EdenRenderer *getEdenRenderer() { return edenRenderer.isReady() ? &edenRenderer : nullptr; }
    inline bool isFrameActive() const { return bFrameActive; }

public:
    GSRender();
    ~GSRender();

    const char *getName() const override { return "Service : Render"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;

    bool createDevice( SDL_Window *pWindow, bool bVSync, bool bValidation );

    bool beginFrame();
    void endFrame();

    void setClearColor( const ColorRGBA &color ) { clearColor = color; }
    void onWindowResized() { device.requestSwapchainRecreation(); }

private:
    RenderDevice device;
    TextureCache textureCache;
    Renderer2D renderer2D;
    EdenRenderer edenRenderer;
    ColorRGBA clearColor;
    bool bFrameActive;
};

extern GSRender *gpRender;
