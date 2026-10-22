#include "shared.h"
#include "gs_render.h"

GSRender *gpRender = nullptr;

GSRender::GSRender() : clearColor( 0.0f, 0.0f, 0.0f, 1.0f ), bFrameActive( false ) {
    gpRender = this;
}

GSRender::~GSRender() { gpRender = nullptr; }

bool GSRender::initialize( TestDriveGameInstance *pInstance ) {
    return GameSystem::initialize( pInstance );
}

bool GSRender::createDevice( SDL_Window *pWindow, bool bVSync, bool bValidation ) {
    if ( !device.create( pWindow, bVSync, bValidation ) ) {
        return false;
    }
    textureCache.initialize( &device );
    if ( !renderer2D.initialize( &device, textureCache.getWhiteTexture() ) ) {
        OTDU_LOG_ERROR( "Failed to initialize the 2D renderer\n" );
        return false;
    }
    if ( !edenRenderer.initialize( &device, &textureCache ) ) {
        OTDU_LOG_WARN( "Original shaders renderer unavailable\n" );
    }
    return true;
}

void GSRender::terminate() {
    device.waitIdle();
    edenRenderer.destroy();
    renderer2D.destroy();
    textureCache.destroy();
    device.destroy();
}

bool GSRender::beginFrame() {
    bFrameActive = device.beginFrame( clearColor );
    return bFrameActive;
}

void GSRender::endFrame() {
    if ( bFrameActive ) {
        device.endFrame();
        bFrameActive = false;
    }
}
