#include "shared.h"
#include "gs_imgui.h"

#include "render/gs_render.h"
#include "tdu_instance.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

GSImGui *gpImGui = nullptr;

GSImGui::GSImGui() : bInitialized( false ) { gpImGui = this; }

GSImGui::~GSImGui() { gpImGui = nullptr; }

bool GSImGui::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendRendererName = "OpenTDU2 Vulkan";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan( pInstance->getWindow() );
    bInitialized = true;
    return true;
}

void GSImGui::terminate() {
    if ( !bInitialized ) {
        return;
    }
    for ( ImTextureData *pTexture : ImGui::GetPlatformIO().Textures ) {
        if ( pTexture->TexID != ImTextureID_Invalid ) {
            gpRender->getDevice().destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
            pTexture->SetTexID( ImTextureID_Invalid );
            pTexture->SetStatus( ImTextureStatus_Destroyed );
        }
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    bInitialized = false;
}

void GSImGui::processEvent( const SDL_Event &event ) {
    if ( bInitialized ) {
        ImGui_ImplSDL3_ProcessEvent( &event );
    }
}

bool GSImGui::wantsKeyboard() const {
    return bInitialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool GSImGui::wantsMouse() const { return bInitialized && ImGui::GetIO().WantCaptureMouse; }

void GSImGui::beginFrame( float deltaTime ) {
    ImGui_ImplSDL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.DeltaTime = deltaTime;
    ImGui::NewFrame();
}

void GSImGui::updateTexture( ImTextureData *pTexture ) {
    RenderDevice &device = gpRender->getDevice();
    if ( pTexture->Status == ImTextureStatus_WantDestroy ) {
        device.destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
        pTexture->SetTexID( ImTextureID_Invalid );
        pTexture->SetStatus( ImTextureStatus_Destroyed );
        return;
    }
    if ( pTexture->Status != ImTextureStatus_WantCreate &&
         pTexture->Status != ImTextureStatus_WantUpdates ) {
        return;
    }

    // Simple path: (re)create the whole texture on any change.
    if ( pTexture->TexID != ImTextureID_Invalid ) {
        device.destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
    }
    std::vector<uint8_t> rgba( static_cast<size_t>( pTexture->Width ) * pTexture->Height * 4 );
    const uint8_t *pPixels = static_cast<const uint8_t *>( pTexture->GetPixels() );
    if ( pTexture->Format == ImTextureFormat_Alpha8 ) {
        for ( size_t i = 0; i < static_cast<size_t>( pTexture->Width ) * pTexture->Height; i++ ) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = pPixels[i];
        }
    } else {
        memcpy( rgba.data(), pPixels, rgba.size() );
    }
    GPUTexture *pGPUTexture = gpRender->getTextureCache().createFromRGBA(
        static_cast<uint32_t>( pTexture->Width ), static_cast<uint32_t>( pTexture->Height ),
        rgba.data(), "ImGui" );
    pTexture->SetTexID( reinterpret_cast<ImTextureID>( pGPUTexture ) );
    pTexture->SetStatus( ImTextureStatus_OK );
}

void GSImGui::endFrame() {
    ImGui::Render();
    ImDrawData *pDrawData = ImGui::GetDrawData();
    if ( pDrawData->Textures != nullptr ) {
        for ( ImTextureData *pTexture : *pDrawData->Textures ) {
            if ( pTexture->Status != ImTextureStatus_OK ) {
                updateTexture( pTexture );
            }
        }
    }
    gpRender->get2DRenderer().drawImGui( pDrawData, nullptr );
}
