#pragma once

#include "game/gs_base.h"

union SDL_Event;
struct ImTextureData;

// Dear ImGui integration (debug menus, toggled with F1). Rendered by the 2D
// renderer (ImDrawVert matches Vertex2D).
class GSImGui : public GameSystem {
public:
    GSImGui();
    ~GSImGui();

    const char *getName() const override { return "Service : ImGui"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;

    void processEvent( const SDL_Event &event );
    bool wantsKeyboard() const;
    bool wantsMouse() const;

    void beginFrame( float deltaTime );
    void endFrame();

private:
    bool bInitialized;

private:
    void updateTexture( ImTextureData *pTexture );
};

extern GSImGui *gpImGui;
