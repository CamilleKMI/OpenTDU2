#include "shared.h"
#include "walk_mode.h"

#include "game/avatarchoice/intro_cinematic.h"
#include "physics/havok_collision.h"
#include "system/cmd_line.h"

namespace {
constexpr float kReticleRayLength = 50.0f; // FUN_00952410 / FUN_00bd2f80
constexpr float kSelectionCooldown = 0.2f; // FUN_0094f440
// Body sphere and step height: the controller is encrypted [ENC/INF].
constexpr float kRadius = 0.3f;
constexpr float kStepUp = 0.4f;
constexpr float kMaxDrop = 2.0f;

// FpSelectable Type -> /:SelItemType [INF: the wrapper constructors are
// encrypted; intro.vmf ItemType() handles "garage", "car", "vehicle"; the
// garage door has an empty SelectableString, the car volume "car"].
const char *ItemTypeOf( const IntroCinematic::Volume &volume ) {
    if ( !volume.SelectableString.empty() ) {
        return volume.SelectableString.c_str();
    }
    switch ( volume.SelectableType ) {
    case 5:
        return "garage";
    case 57:
        return "car";
    default:
        return "";
    }
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
    controller.reset( position, heading );
    controller.setCollide(
        [this, &scene]( const Vector3f &from, const Vector3f &to ) { return collide( scene, from, to ); } );
    // Eye height: the start camera above the floor under it.
    float floorY = 0.0f;
    if ( FindHavokFloor( scene.getCollision(), position, 0.1f, 3.0f, floorY ) ) {
        eyeHeight = position.y - floorY;
    }
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
    // FUN_00952410: nearest selectable in front of the camera hit by the
    // reticle ray; FUN_0094f440 SetSelected.
    cooldown -= deltaTime;
    const Vector3f origin = controller.getPosition();
    const Vector3f direction = controller.getForward();
    const auto &selectables = scene.getSelectables();
    int32_t best = -1;
    float bestDistance = kReticleRayLength;
    for ( size_t i = 0; i < selectables.size(); i++ ) {
        float t = 0.0f;
        if ( selectableFilter && !selectableFilter( static_cast<int32_t>( i ) ) ) {
            continue;
        }
        if ( selectables[i].raycast( origin, direction, kReticleRayLength, t ) && t < bestDistance ) {
            best = static_cast<int32_t>( i );
            bestDistance = t;
        }
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
