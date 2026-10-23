#include "shared.h"
#include "walk_mode.h"

#include "game/avatarchoice/intro_cinematic.h"
#include "physics/havok_collision.h"
#include "system/cmd_line.h"

namespace {
// FUN_00952410 (walk-mode manager per frame, docs/re/tess_garage_spec.md
// 4.1): a selectable is a candidate when its position (wrapper vf08 =
// entity +0x40, the orient_pos_scale pos) is in front of the eye, nearer
// than the wrapper radius vf2c (FUN_00953d50 = DAT_00edd270 = 10.0 m, the
// same for every wrapper vtable) and nearer than the best candidate so far;
// then the reticle ray must hit it (vf04).
// (The 50 m of FUN_00952410, DAT_00f4a040, is the /cursor depth raycast
// FUN_00bd2f80 -> cursor vf+0x100, not a selection distance.)
constexpr float kSelectRadius = 10.0f;
constexpr float kSelectionCooldown = 0.2f; // FUN_0094f440
// Walk camera (camera type 9, ctor FUN_00c823e0, docs/re/tess_garage_spec.md
// 4.3b) [CODE]: FUN_00c825a0 casts a ray 10 m (DAT_00edd270) down from the
// camera and creates the Havok character proxy (FUN_00bcea00: capsule radius
// 0.3 (DAT_00ede42c), height 1.2 (DAT_00edf24c, vertices +-0.6)) with its
// centre at hit + 0.3 (DAT_00f4a178) + 0.6 (DAT_00f49ea0); FUN_00c82840 sets
// the eye offset camera+0x3c0 = (0, 0.7 (DAT_00eddb08), 0) above the proxy
// centre, and FUN_00950470 puts the avatar at eye - 0.7 - 0.9 (the floor).
// Eye = floor + 0.3 + 0.6 + 0.7 = 1.6 m, the same for man and woman (no
// avatar size term in this code).
constexpr float kProxyCenterHeight = 0.3f + 0.6f;
constexpr float kEyeOffset = 0.7f;
constexpr float kEyeHeight = kProxyCenterHeight + kEyeOffset;
constexpr float kStartRayLength = 10.0f;
// Body sphere = the proxy radius [CODE]; step height: the character proxy
// step/slope handling is encrypted (FUN_006929b0, max slope arg 80) [ENC/INF].
constexpr float kRadius = 0.3f;
constexpr float kStepUp = 0.4f;
constexpr float kMaxDrop = 2.0f;

// FpSelectable Type -> /:SelItemType: FUN_00953350 (wrapper vf0c) looks the
// Type (entity +0x170) up in the table at 0x00edd580 ({type, name, icon,
// ...}, 16 bytes, ends at -1); Type 57 (0x39) uses the wrapper vf3c instead
// (encrypted [ENC]; [INF] the SelectableString, "car" for FST_EnterCar).
struct SelectableTypeName {
    int32_t Type;
    const char *pName;
};
constexpr SelectableTypeName kSelectableTypeNames[] = {
    { 0, "exit" },          { 1, "carseller" },      { 4, "stolencar" },     { 2, "player" },
    { 3, "carrenter" },     { 5, "garage" },         { 6, "living" },        { 54, "club" },
    { 7, "realtor" },       { 8, "photographer" },   { 9, "diary" },         { 10, "dressing" },
    { 49, "game_progress" }, { 50, "bar" },          { 51, "office" },       { 52, "photos" },
    { 53, "photo_expo" },   { 12, "billboard" },     { 13, "billboard" },    { 14, "billboard" },
    { 55, "billboard" },    { 15, "billboard" },     { 16, "billboard" },    { 33, "billboard" },
    { 34, "billboard" },    { 35, "billboard" },     { 36, "billboard" },    { 37, "billboard" },
    { 38, "billboard" },    { 39, "billboard" },     { 40, "billboard" },    { 41, "billboard" },
    { 42, "billboard" },    { 43, "billboard" },     { 44, "billboard" },    { 45, "billboard" },
    { 46, "billboard" },    { 47, "billboard" },     { 48, "billboard" },    { 17, "npc" },
    { 18, "npc" },          { 19, "npc" },           { 20, "tutor" },        { 21, "npc_receiver" },
    { 22, "goodies" },      { 23, "npc_tuner" },     { 24, "npc_stickers" }, { 25, "npc_stickers" },
    { 26, "npc" },          { 27, "surgeon" },       { 28, "npc_stickers" }, { 29, "hairdresser" },
    { 30, "npc" },          { 31, "npc" },           { 32, "npc_airport_hostess" }, { 56, "jacuzzi" },
};

const char *ItemTypeOf( const IntroCinematic::Volume &volume ) {
    if ( volume.SelectableType == 57 ) {
        return volume.SelectableString.c_str();
    }
    for ( const SelectableTypeName &entry : kSelectableTypeNames ) {
        if ( entry.Type == volume.SelectableType ) {
            return entry.pName;
        }
    }
    return ""; // the table sentinel: "exit"'s icon, empty name
}
} // namespace

bool WalkMode::start( IntroCinematic &scene, const std::string &cameraName, float newFov ) {
    fov = newFov;
    selected = -1;
    cooldown = 0.0f;
    Vector3f position;
    Quaternionf heading;
    if ( !scene.getEntityCameraLocal( cameraName, position, heading ) ) {
        OTDU_LOG_WARN( "WalkMode: camera %s not found\n", cameraName.c_str() );
        return false;
    }
    // Test shortcut: -walkat x,y,z,yawDegrees (scene-local eye position).
    if ( const char *pAt = CmdLine::GetArgValue( "-walkat" ) ) {
        float yawDegrees = 0.0f;
        if ( sscanf( pAt, "%f,%f,%f,%f", &position.x, &position.y, &position.z, &yawDegrees ) == 4 ) {
            const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yawDegrees * kDegToRad );
            heading = Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
        }
    }
    controller.setCollide(
        [this, &scene]( const Vector3f &from, const Vector3f &to ) { return collide( scene, from, to ); } );
    // FUN_00c825a0: the proxy is dropped on the floor under the start camera
    // (ray 10 m down), the eye is 1.6 m above it (see kEyeHeight). No floor
    // hit: the original uses camera+0x330 + 2 m [CODE, field not traced];
    // OpenTDU2 then keeps the start camera height.
    eyeHeight = kEyeHeight;
    float floorY = 0.0f;
    if ( FindHavokFloor( scene.getCollision(), position, 0.1f, kStartRayLength, floorY ) ) {
        OTDU_LOG_INFO( "WalkMode: %s y %.3f, floor %.3f -> eye %.3f\n", cameraName.c_str(), position.y, floorY,
                       floorY + eyeHeight );
        position.y = floorY + eyeHeight;
    }
    controller.reset( position, heading );
    resume( scene );
    return true;
}

void WalkMode::resume( IntroCinematic &scene ) {
    scene.setLocalCamera( controller.getPosition(), controller.getHeading(), fov );
}

Vector3f WalkMode::collide( const IntroCinematic &scene, const Vector3f &from, const Vector3f &to ) const {
    // Scene collision (.shk): the body sphere is pushed out of the walls,
    // the feet follow the floor (step up / drop).
    const HavokCollision &collision = scene.getCollision();
    if ( collision.Indices.empty() && collision.Boxes.empty() ) {
        return Vector3f( to.x, from.y, to.z );
    }
    Vector3f feet( to.x, from.y - eyeHeight, to.z );
    const Vector3f body =
        ResolveSphereHavokCollision( collision, feet + Vector3f( 0.0f, kStepUp + kRadius, 0.0f ), kRadius );
    feet = Vector3f( body.x, feet.y, body.z );
    // Floor under the body footprint (the body is not a point: the house
    // floor meshes have seams, e.g. a 0.2 m gap at the living room door
    // x 9.3..9.5): the centre, else the highest floor within the radius.
    float floorY = 0.0f;
    bool bFloor = FindHavokFloor( collision, feet, kStepUp, kMaxDrop, floorY );
    for ( int32_t i = 0; i < 8 && !bFloor; i++ ) {
        const float angle = i * ( 2.0f * kPI / 8.0f );
        float y = 0.0f;
        if ( FindHavokFloor( collision, feet + Vector3f( std::cos( angle ) * kRadius, 0.0f, std::sin( angle ) * kRadius ),
                             kStepUp, kMaxDrop, y ) ) {
            floorY = y;
            bFloor = true;
        }
    }
    if ( !bFloor ) {
        return from;
    }
    feet.y = floorY;
    return feet + Vector3f( 0.0f, eyeHeight, 0.0f );
}

const char *WalkMode::getSelectedType( const IntroCinematic &scene ) const {
    const auto &selectables = scene.getSelectables();
    if ( selected < 0 || selected >= static_cast<int32_t>( selectables.size() ) ) {
        return "";
    }
    return ItemTypeOf( selectables[selected] );
}

void WalkMode::clearSelection( IntroCinematic &scene ) {
    if ( selected >= 0 ) {
        scene.getUi().invoke( "/", "ItemType", { "out" } );
    }
    selected = -1;
}

void WalkMode::updateSelection( IntroCinematic &scene, float deltaTime ) {
    // FUN_00952410: per selectable (wrapper list +0xb80), d = position - eye
    // (eye = camera +0x310):
    //   dot(forward, d / |d|) > 0, |d|^2 < vf2c()^2 (10 m), |d|^2 < best,
    //   vf04(eye, forward) (FUN_009533b0 -> FUN_00953d80 for a box: the ray
    //   meets the volume and the scene collision ray (FUN_00bd2f80) up to the
    //   far side is free or hits inside the volume);
    // the nearest position wins; FUN_0094f440 SetSelected.
    cooldown -= deltaTime;
    const Vector3f origin = controller.getPosition();
    const Vector3f direction = controller.getForward();
    const auto &selectables = scene.getSelectables();
    const HavokCollision &collision = scene.getCollision();
    int32_t best = -1;
    float bestDistance2 = kSelectRadius * kSelectRadius;
    for ( size_t i = 0; i < selectables.size(); i++ ) {
        if ( selectableFilter && !selectableFilter( static_cast<int32_t>( i ) ) ) {
            continue;
        }
        const IntroCinematic::Volume &volume = selectables[i];
        const Vector3f toCenter = volume.Center - origin;
        const float distance2 = toCenter.dot( toCenter );
        if ( toCenter.dot( direction ) <= 0.0f || distance2 >= bestDistance2 ) {
            continue;
        }
        float tEnter = 0.0f;
        float tExit = 0.0f;
        if ( !volume.raycast( origin, direction, kSelectRadius * 2.0f, tEnter, &tExit ) ) {
            continue;
        }
        float tHit = 0.0f;
        if ( RaycastHavokCollision( collision, origin, direction, tExit, tHit ) &&
             !volume.contains( origin + direction * tHit ) ) {
            continue; // occluded (FUN_00ccb960: the hit point is not in the volume)
        }
        best = static_cast<int32_t>( i );
        bestDistance2 = distance2;
    }
    if ( best == selected || ( cooldown > 0.0f && best != -1 ) ) {
        return;
    }
    FlashPlayer &ui = scene.getUi();
    if ( selected >= 0 ) {
        ui.invoke( "/", "ItemType", { "out" } );
    }
    selected = best;
    cooldown = kSelectionCooldown;
    if ( best >= 0 ) {
        ui.setVariable( "/:SelItemType", ItemTypeOf( selectables[best] ) );
        ui.invoke( "/", "ItemType", { "over" } );
    }
}

void WalkMode::update( IntroCinematic &scene, float deltaTime, bool bInputEnabled ) {
    controller.update( deltaTime, bInputEnabled );
    resume( scene );
    if ( bInputEnabled ) {
        updateSelection( scene, deltaTime );
    }
}
