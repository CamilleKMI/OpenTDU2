#pragma once

#include "game/walkmode/walk_controller.h"

#include <functional>
#include <string>

class IntroCinematic;

// Walk mode manager (DAT_00fd95e0, docs/re/tess_garage_spec.md 4.1) on an
// IntroCinematic scene: first person controller on the scene collision
// (.shk), camera, FpSelectable selection (position within 10 m in front
// of the eye, reticle ray on the volume, not occluded; 0.2 s re-selection
// cooldown), INTRO movie ItemType('over'/'out').
// Look-at (docs/re/facefx_spec.md 6.3.3): the selection never makes the
// hovered character look at the player (no look-at call in FUN_00952410,
// FUN_0094f440 or the FpSelectable enter/leave vf14/vf18) [CODE]. The only
// walk-mode look-at, FUN_00b858f0 at 0x9520b9, is in the player-avatar state
// machine FUN_009519a0 (states 6 and 8: the self-view camera type 10, avatar
// faded in) and makes the PLAYER's avatar look at the camera; OpenTDU2 has no
// self-view yet, so there is no caller (API: IntroCinematic::setLookAtTarget).
class WalkMode {
public:
    // Starts at an Entity__Camera (cam_entree_xx / cam_entree).
    bool start( IntroCinematic &scene, const std::string &cameraName, float fov );
    void update( IntroCinematic &scene, float deltaTime, bool bInputEnabled );
    // Camera back to the walk pose (after a cinematic).
    void resume( IntroCinematic &scene );
    void clearSelection( IntroCinematic &scene );

    inline const Vector3f &getPosition() const { return controller.getPosition(); }
    inline Vector3f getForward() const { return controller.getForward(); }
    // Selectables refused by the filter are not picked (FIRST_WM: the "car"
    // item needs the driver door open, FUN_009aa560 carReady).
    using SelectableFilter = std::function<bool( int32_t index )>;
    void setSelectableFilter( SelectableFilter filter ) { selectableFilter = std::move( filter ); }
    inline int32_t getSelected() const { return selected; }
    // Called when the selection changes, before /:SelItemType and
    // ItemType('over') (the selectable wrapper vf14 sets its own movie
    // variables first, e.g. the shop car data FUN_00da50e0); -1: none.
    using SelectionCallback = std::function<void( int32_t index )>;
    // FUN_0094f440 end: the walk HUD movie (+0xd10) gets gotoAndPlay("on" /
    // "off") on its "cursor" clip (selected / none) [CODE: 0x94f78a..0x94f7b0];
    // the SHOPSCAR cursor then calls ItemAction -> ItemType itself. Without
    // it (INTRO.vmf modes) ItemType('over' / 'out') is called on the root.
    void setCursorLabels( bool bEnable ) { bCursorLabels = bEnable; }
    void setSelectionCallback( SelectionCallback callback ) { selectionCallback = std::move( callback ); }
    // /:SelItemType of the selection ("garage", "car"...), empty if none.
    const char *getSelectedType( const IntroCinematic &scene ) const;

private:
    WalkController controller;
    float fov = 1.0472f;
    float eyeHeight = 1.6f; // floor + 0.9 (proxy centre) + 0.7 [CODE, walk_mode.cpp]
    int32_t selected = -1;
    float cooldown = 0.0f;
    SelectableFilter selectableFilter;
    SelectionCallback selectionCallback;
    bool bCursorLabels = false;

    Vector3f collide( const IntroCinematic &scene, const Vector3f &from, const Vector3f &to ) const;
    void updateSelection( IntroCinematic &scene, float deltaTime );
};
