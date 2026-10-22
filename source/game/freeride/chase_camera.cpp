#include "shared.h"
#include "chase_camera.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"

namespace {
// Rotation matrix (columns x, y, z) -> quaternion.
Quaternionf QuaternionFromBasis( const Vector3f &x, const Vector3f &y, const Vector3f &z ) {
    const float m00 = x.x, m11 = y.y, m22 = z.z;
    const float trace = m00 + m11 + m22;
    Quaternionf q;
    if ( trace > 0.0f ) {
        const float s = std::sqrt( trace + 1.0f ) * 2.0f;
        q = Quaternionf( ( y.z - z.y ) / s, ( z.x - x.z ) / s, ( x.y - y.x ) / s, 0.25f * s );
    } else if ( m00 > m11 && m00 > m22 ) {
        const float s = std::sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
        q = Quaternionf( 0.25f * s, ( y.x + x.y ) / s, ( z.x + x.z ) / s, ( y.z - z.y ) / s );
    } else if ( m11 > m22 ) {
        const float s = std::sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
        q = Quaternionf( ( y.x + x.y ) / s, 0.25f * s, ( z.y + y.z ) / s, ( z.x - x.z ) / s );
    } else {
        const float s = std::sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
        q = Quaternionf( ( z.x + x.z ) / s, ( z.y + y.z ) / s, 0.25f * s, ( x.y - y.x ) / s );
    }
    return q;
}

float Lerp1( float a, float b, float t ) { return a + ( b - a ) * t; }
} // namespace

Quaternionf ChaseCamera::LookRotation( const Vector3f &eye, const Vector3f &target ) {
    // Eden camera: EdenRotationFromQuaternion(q) maps (0, 0, 1) to the view
    // direction, x to the left of the screen (cameraWorld = T R diag(-1, 1,
    // -1), EdenSceneView::setupScene).
    Vector3f z = ( target - eye ).normalized();
    if ( z.length() < 0.5f ) {
        z = Vector3f( 0.0f, 0.0f, 1.0f );
    }
    Vector3f x = Vector3f( 0.0f, 1.0f, 0.0f ).cross( z ).normalized();
    if ( x.length() < 0.5f ) {
        x = Vector3f( 1.0f, 0.0f, 0.0f );
    }
    const Vector3f y = z.cross( x );
    const Quaternionf r = QuaternionFromBasis( x, y, z );
    return Quaternionf( -r.x, -r.y, -r.z, r.w );
}

bool ChaseCamera::load( int32_t carCameraId ) {
    cameras.clear();
    current = 0;
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "database/camerasingame.xmb" ).c_str(), data ) ) {
        OTDU_LOG_WARN( "ChaseCamera: database/camerasingame.xmb not found\n" );
        cameras.push_back( Params() );
        return false;
    }
    XmbDocument document;
    if ( !document.load( data ) ) {
        cameras.push_back( Params() );
        return false;
    }
    XmbValue root = document.getRoot()["CameraInGame"];
    std::vector<int32_t> cycle;
    for ( uint32_t i = 0; i < root.getArraySize( "p_array_camera_switch" ); i++ ) {
        cycle.push_back( static_cast<int32_t>( root.getArrayElement( "p_array_camera_switch", i ).asInt() ) );
    }
    const int32_t numVehicles = static_cast<int32_t>( root["nb_camera_vehicles"].asInt( 0 ) );
    std::vector<Params> carCameras;
    for ( int32_t v = 0; v < numVehicles + 1 && carCameras.empty(); v++ ) {
        XmbValue car = root[( "Car " + std::to_string( v ) ).c_str()];
        if ( !car.isValid() || car["id_car"].asInt( -1 ) != carCameraId ) {
            continue;
        }
        const int32_t numCameras = static_cast<int32_t>( car["nb_cameras"].asInt( 0 ) );
        for ( int32_t c = 0; c < numCameras; c++ ) {
            XmbValue camera = car[( "Camera " + std::to_string( c ) ).c_str()];
            if ( !camera.isValid() ) {
                continue;
            }
            Params p;
            p.Mode = static_cast<int32_t>( camera["mode"].asInt( 20 ) );
            p.FovDeg = camera["fov_deg"].asFloat( p.FovDeg );
            p.bFovLerp = camera["fov_is_lerp"].asInt( 1 ) != 0;
            p.FovMin = camera["fov_min"].asFloat( p.FovMin );
            p.FovMax = camera["fov_max"].asFloat( p.FovMax );
            p.FovSpeedMin = camera["fov_speed_min"].asFloat( p.FovSpeedMin );
            p.FovSpeedMax = camera["fov_speed_max"].asFloat( p.FovSpeedMax );
            p.Near = camera["nearProjPlane"].asFloat( p.Near );
            p.Far = camera["farProjPlane"].asFloat( p.Far );
            p.PosOffset = camera["follow_pos_offset"].asFloat( p.PosOffset );
            p.PosOffsetH = camera["follow_pos_offset_H"].asFloat( p.PosOffsetH );
            p.TargOffset = camera["follow_targ_offset"].asFloat( p.TargOffset );
            p.TargOffsetH = camera["follow_targ_offset_H"].asFloat( p.TargOffsetH );
            p.SpeedOffset = camera["follow_speed_offset"].asFloat( p.SpeedOffset );
            p.PosMaxOffset = camera["follow_pos_max_offset"].asFloat( p.PosMaxOffset );
            p.PosMaxOffsetH = camera["follow_pos_max_offset_h"].asFloat( p.PosMaxOffsetH );
            p.TargMaxOffset = camera["follow_targ_max_offset"].asFloat( p.TargMaxOffset );
            p.TargMaxOffsetH = camera["follow_targ_max_offset_h"].asFloat( p.TargMaxOffsetH );
            p.SpeedMaxOffset = camera["follow_speed_max_offset"].asFloat( p.SpeedMaxOffset );
            p.DelayOffset = camera["follow_delay_offset"].asFloat( p.DelayOffset );
            p.DelayMaxOffset = camera["follow_delay_max_offset"].asFloat( p.DelayMaxOffset );
            p.LeanFactorOffset = camera["follow_lean_factor_offset"].asFloat( p.LeanFactorOffset );
            p.LeanFactorMaxOffset = camera["follow_lean_factor_max_offset"].asFloat( p.LeanFactorMaxOffset );
            p.FixedPos = Vector3f( camera["fixed_pos_x"].asFloat( 0.0f ), camera["fixed_pos_y"].asFloat( 1.1f ),
                                   camera["fixed_pos_z"].asFloat( 0.0f ) );
            p.FixedTarget = Vector3f( camera["fixed_targ_x"].asFloat( 0.0f ), camera["fixed_targ_y"].asFloat( 0.5f ),
                                      camera["fixed_targ_z"].asFloat( 1.0f ) );
            carCameras.push_back( p );
        }
    }
    // Switch order, modes of the car only.
    for ( int32_t mode : cycle ) {
        for ( const Params &p : carCameras ) {
            if ( p.Mode == mode ) {
                cameras.push_back( p );
                break;
            }
        }
    }
    if ( cameras.empty() ) {
        cameras.push_back( Params() );
    }
    OTDU_LOG_INFO( "ChaseCamera: car camera %d: %zu cameras of the car, %zu in the switch cycle (first mode %d, fov %.0f, "
                   "pos %.2f / %.2f, target %.2f / %.2f)\n",
                   carCameraId, carCameras.size(), cameras.size(), cameras[0].Mode, cameras[0].FovDeg,
                   cameras[0].PosOffset, cameras[0].PosOffsetH, cameras[0].TargOffset, cameras[0].TargOffsetH );
    return !carCameras.empty();
}

void ChaseCamera::nextMode() {
    if ( !cameras.empty() ) {
        current = ( current + 1 ) % cameras.size();
    }
}

void ChaseCamera::reset( const Vector3f &carPosition, const Vector3f &carForward ) {
    (void)carPosition;
    heading = Vector3f( carForward.x, 0.0f, carForward.z ).normalized();
    bInitialized = heading.length() > 0.5f;
}

EdenSceneView::Camera ChaseCamera::update( float deltaTime, const Matrix4x4f &carWorld, float speedKmh,
                                           const std::function<bool( float x, float z, float &outHeight )> &ground ) {
    const Params &p = cameras.empty() ? Params() : cameras[current];
    const Vector3f carPosition = carWorld.getTranslation();
    const Vector3f carForward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    Vector3f flat = Vector3f( carForward.x, 0.0f, carForward.z ).normalized();
    if ( flat.length() < 0.5f ) {
        flat = heading;
    }
    const float kmh = std::abs( speedKmh );
    const float t = Clamp( ( kmh - p.SpeedOffset ) / Max( p.SpeedMaxOffset - p.SpeedOffset, 1.0f ), 0.0f, 1.0f );
    if ( !bInitialized ) {
        heading = flat;
        bInitialized = true;
    }
    // Heading follow: time constant follow_delay / 60 s [INF].
    const float delay = Lerp1( p.DelayOffset, p.DelayMaxOffset, t ) / 60.0f;
    const float blend = delay > 0.0f ? 1.0f - std::exp( -deltaTime / delay ) : 1.0f;
    heading = ( heading + ( flat - heading ) * blend ).normalized();
    if ( heading.length() < 0.5f ) {
        heading = flat;
    }

    EdenSceneView::Camera camera;
    camera.Near = p.Near;
    camera.Far = p.Far;
    float fov = p.FovDeg;
    if ( p.bFovLerp ) {
        const float ft = Clamp( ( kmh - p.FovSpeedMin ) / Max( p.FovSpeedMax - p.FovSpeedMin, 1.0f ), 0.0f, 1.0f );
        fov = Lerp1( p.FovMin, p.FovMax, ft );
    }
    camera.Fov = fov * kDegToRad;

    Vector3f eye, target;
    const bool bInside = p.Near < 0.5f; // inside views (near 0.05): fixed pose in the car frame
    if ( bInside ) {
        // fixed_pos / fixed_targ (car frame, x left).
        eye = carWorld.transformPoint( p.FixedPos );
        target = carWorld.transformPoint( p.FixedTarget );
    } else {
        const float posOffset = Lerp1( p.PosOffset, p.PosMaxOffset, t );
        const float posH = Lerp1( p.PosOffsetH, p.PosMaxOffsetH, t );
        const float targOffset = Lerp1( p.TargOffset, p.TargMaxOffset, t );
        const float targH = Lerp1( p.TargOffsetH, p.TargMaxOffsetH, t );
        eye = carPosition + heading * posOffset + Vector3f( 0.0f, posH, 0.0f );
        target = carPosition + heading * targOffset + Vector3f( 0.0f, targH, 0.0f );
        // Keep the eye above the ground [INF].
        float height = 0.0f;
        if ( ground && ground( eye.x, eye.z, height ) && eye.y < height + 0.5f ) {
            eye.y = height + 0.5f;
        }
    }
    camera.Position = eye;
    camera.Rotation = LookRotation( eye, target );
    return camera;
}
