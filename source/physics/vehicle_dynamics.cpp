#include "shared.h"
#include "vehicle_dynamics.h"

#include "physics/vehicle_collision.h"

// Tags: [CODE] read in the exe (addresses in docs/re/vehicle_physics_spec.md
// section 6), [INF] chosen here where the edVehicle code is in the SecuROM
// range or not read (reason given next to each value).
namespace {
constexpr float kGravity = 9.8100004196167f;     // -DAT_00f4b4d0 (world gravity set by FUN_00be0010 at 0xbe03cb) [CODE]
constexpr float kTickRate = 90.0f;               // DAT_00ede78c: PhysThread period (FUN_00bd07c0) [CODE]
constexpr float kMaxTickDt = 0.01111111f;        // DAT_00f4a0b0: car dt clamp (FUN_00bd03a0) [CODE]
constexpr float kAeroHalfDensity = -0.592f;      // DAT_00f4adc0 = -1.184 / 2 [CODE FUN_00883b20]
constexpr float kRollingResistance = 0.015f;     // Material RollingResistance (value not found) [INF]
constexpr float kMinSlipVelocity = 1.0f;         // slip denominators at rest (FUN_0046e760 ENC) [INF]
constexpr float kBumpStopRate = 10.0f;           // x spring rate past the upper stop (suspension ENC) [INF]
constexpr float kRadToDegD = 57.295780181884766f; // DAT_00f49ab0
constexpr float kPacejkaEps = 9.999999747378752e-06f; // DAT_00ecd6ec
constexpr float kDefaultB = 99999.0f;            // DAT_00f4a6cc (B when C / D / a4 vanish)
constexpr float kMaxNormalForce = 100000.0f;     // DAT_00f49edc clamp of the tyre NormalForce (FUN_008739c0) [CODE]
constexpr float kGrassSlopeNormalY = 0.8f;       // manager +0x2c4 (ctor FUN_0088abe0, DAT_00edda90) [CODE]
constexpr float kGrassSlopeFriction = 0.2f;      // DAT_00ede428 (FUN_0086a990) [CODE]
constexpr float kClutchMaxImpulse = 100.0f;      // DAT_00edf3d8 clamp (FUN_00877e50) [CODE]
constexpr float kLowSpeed2 = 100.0f;             // |v|^2 <= 100 -> vehicle flag 0x8000 (FUN_0087ebf0) [CODE]
constexpr float kShaftDampingThreshold = 1.0e6f; // shaft end DampingArray[2] (DAT_00f90d68): linear damping below [CODE]
constexpr float kWheelBrakeTargetMinSpeed = 1.0f; // AbsLongitudinalVelocity < 1 -> target 0 (FUN_00865460 / FUN_00865750) [CODE]

// The tyre functions' inline atan (FUN_00869090 0x869261..): |x| < 1:
// x / (1 + 0.28 x^2), else pi/2 - x / (x^2 + 0.28), minus pi for x < 0
// (DAT_00f49e58 0.28, DAT_00f49e50 pi/2, DAT_00f498a0 pi) [CODE].
float TduAtan( float x ) {
    const double xd = x;
    if ( std::abs( xd ) < 1.0 ) {
        return static_cast<float>( xd / ( xd * 0.2800000011920929 * xd + 1.0 ) );
    }
    float r = static_cast<float>( 1.5707963705062866 - xd / ( xd * xd + 0.2800000011920929 ) );
    if ( x < 0.0f ) {
        r = static_cast<float>( static_cast<double>( r ) - 3.1415927410125732 );
    }
    return r;
}

// y = D sin(C atan(B x - E (B x - atan(B x)))) (no Sh / Sv) [CODE].
float MagicFormula( float b, float c, float d, float e, float x ) {
    const float bx = b * x;
    const float inner = ( TduAtan( bx ) - bx ) * e + bx;
    return std::sin( TduAtan( inner ) * c ) * d; // 0x6c8d38 = sin [INF: CRT stub, ENC]
}

Vector3f Rotate( const Quaternionf &q, const Vector3f &v ) { return q.rotate( v ); }
Vector3f InverseRotate( const Quaternionf &q, const Vector3f &v ) {
    return Quaternionf( -q.x, -q.y, -q.z, q.w ).rotate( v );
}
Quaternionf Normalize( const Quaternionf &q ) {
    const float length = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
    return length > 1e-8f ? Quaternionf( q.x / length, q.y / length, q.z / length, q.w / length ) : Quaternionf();
}
float Sign( float x ) { return x < 0.0f ? -1.0f : 1.0f; }

// Tyre surface index (Tire +0xa8). The game maps the ground material id
// of each wheel with FUN_00ba81a0: 1 = dirt for {1, 0x3d, 0x3e}, 2 = grass
// for {2, 4, 5, 0xb..0xd, 0x2b..0x2e, 0x42}, 0 = tarmac otherwise [CODE].
// OpenTDU2's collision has no material ids yet: road / static meshes =
// tarmac, heightfield terrain = grass [INF].
uint32_t SurfaceIndex( VehicleWorldCollision::Hit::eSurface surface ) {
    return surface == VehicleWorldCollision::Hit::eSurface::Terrain ? TyreSetup::Grass : TyreSetup::Tarmac;
}

struct TyreResult {
    float Fx = 0.0f, Fy = 0.0f;       // LongitudinalForce / LateralForce (combined)
    float Dx = 0.0f, Dy = 0.0f;       // MaxLongitudinalForce / MaxLateralForce
    float SlipRatio = 0.0f, SlipAngle = 0.0f;
    float Slipping = 0.0f;            // SlippingCoefficient
};

// Pure-slip Pacejka + combination of one tyre (FUN_00869090 lateral,
// FUN_00868dc0 longitudinal, FUN_00868a60 combination) [CODE]. vx / vy:
// contact point velocity in the wheel frame, omega: wheel spin.
TyreResult EvaluateTyre( const TyreSetup &tyre, uint32_t surface, float frictionScale, float fz, float vx, float vy,
                         float omega, float radius ) {
    TyreResult out;
    const PacejkaConfig &cfg = tyre.Config[surface];
    const float fzKn = static_cast<float>( static_cast<double>( fz ) * 0.0010000000474974513 );
    // Slip definitions [INF: FUN_0046e760 (slip ratio from the spin) is
    // ENC]: slip ratio (wR - v) / max(|v|, 1 m/s), slip angle atan(vy / max(|vx|, 1)).
    const float absVx = Max( std::abs( vx ), kMinSlipVelocity );
    const float contactVx = vx - omega * radius; // ContactLongitudinalVelocity = LongVel - R w (FUN_0086fd50) [CODE]
    out.SlipRatio = -contactVx / absVx;
    out.SlipAngle = std::atan2( vy, absVx );

    // Lateral: C = a0, D = MaxLatFriction x Fz(kN) x FrictionScale, BCD = a3
    // sin(2 atan(Fz / a4)) (1 - |camber| a5) (camber 0), E = a6 Fz + a7, x = slip angle (deg).
    const float dY = tyre.MaxLateralFriction[surface] * fzKn * frictionScale;
    const float cY = cfg.Lat[0];
    float bY = kDefaultB;
    if ( std::abs( cY ) > kPacejkaEps && std::abs( dY ) > kPacejkaEps && std::abs( cfg.Lat[4] ) > kPacejkaEps ) {
        const float bcd = std::sin( 2.0f * std::atan( fzKn / cfg.Lat[4] ) ) * cfg.Lat[3]; // 0x541080 = atan [INF: ENC]
        bY = bcd / ( dY * cY );
    }
    const float eY = cfg.Lat[6] * fzKn + cfg.Lat[7];
    const float fy0 = MagicFormula( bY, cY, dY, eY, static_cast<float>( out.SlipAngle * kRadToDegD ) );

    // Longitudinal: C = b0, D = MaxLongFriction x Fz x FrictionScale, BCD =
    // (b3 Fz^2 + b4 Fz) exp(-b5 Fz), E = b6 Fz^2 + b7 Fz + b8, x = slip ratio x 100.
    const float dX = tyre.MaxLongitudinalFriction[surface] * fzKn * frictionScale;
    const float cX = cfg.Long[0];
    float bX = kDefaultB;
    if ( std::abs( cX ) > kPacejkaEps && std::abs( dX ) > kPacejkaEps ) {
        const float bcd = ( cfg.Long[3] * fzKn * fzKn + cfg.Long[4] * fzKn ) * std::exp( -cfg.Long[5] * fzKn );
        bX = bcd / ( dX * cX );
    }
    const float eX = cfg.Long[6] * fzKn * fzKn + cfg.Long[7] * fzKn + cfg.Long[8];
    const float fx0 = MagicFormula( bX, cX, dX, eX, static_cast<float>( out.SlipRatio * 100.0 ) );
    out.Dx = dX;
    out.Dy = dY;

    // Combination (FUN_00868a60): direction = (muLat Fz FS (-vy), muLong Fz FS (-vcx))
    // normalised, magnitude = min(|Fy0 / dy|, |Fx0 / dx|) capped by the
    // ellipse sqrt((MaxLatForce dy)^2 + (MaxLongForce dx)^2).
    const float wy = dY * -vy;
    const float wx = dX * -contactVx;
    const float norm = std::sqrt( wy * wy + wx * wx );
    if ( norm <= kPacejkaEps ) {
        return out;
    }
    const float dirY = Clamp( wy / norm, -1.0f, 1.0f ), dirX = Clamp( wx / norm, -1.0f, 1.0f );
    const float ay = std::abs( dirY ), ax = std::abs( dirX );
    float magnitude;
    if ( ax < kPacejkaEps ) {
        magnitude = std::abs( fy0 );
    } else if ( ay < kPacejkaEps ) {
        magnitude = std::abs( fx0 );
    } else {
        magnitude = Min( std::abs( fx0 / ax ), std::abs( fy0 / ay ) );
    }
    const float ellipse = std::sqrt( ( dY * ay ) * ( dY * ay ) + ( dX * ax ) * ( dX * ax ) );
    magnitude = Min( magnitude, ellipse );
    out.Fy = dirY * magnitude;
    out.Fx = dirX * magnitude;
    out.Slipping = ellipse > 0.0f ? magnitude / ellipse : 0.0f;
    return out;
}
} // namespace

VehicleDynamics::VehicleDynamics() {}

// GearRatioArray (gearbox +0x14c): [0] = -_gear_ratio1 (reverse), [1] = 0
// (neutral), [2..8] = 1st..7th, [9] = 1.0 (FUN_00bdb970 / FUN_00884220)
// [CODE]; times the differential GearRatio (_final_drive_ratio).
float VehicleDynamics::gearRatio( int32_t index ) const {
    if ( index < 0 || index > 9 ) {
        return 0.0f;
    }
    return gearbox.Ratio[index] * gearbox.Ratio[9] * params.FinalDrive;
}

int32_t VehicleDynamics::getGear() const {
    return gearbox.Gear == 0 ? -1 : ( gearbox.Gear == 1 ? 0 : gearbox.Gear - 1 );
}

void VehicleDynamics::setup( const VehicleSetup &newSetup, const std::vector<WheelDesc> &newWheels ) {
    params = newSetup;
    wheels = newWheels;
    wheelStates.assign( wheels.size(), WheelState() );
    brakes.assign( wheels.size(), BrakeController() );
    contactFrames.assign( wheels.size(), ContactFrame() );
    mass = Max( params.Mass, 100.0f );
    inertia = params.Inertia;
    wheelPartInertia.assign( wheels.size(), 1.0f );
    wheelDriveImpulse.assign( wheels.size(), 0.0f );
    wheelEffInertia.assign( wheels.size(), 1.0f );
    stageDriveImpulse.assign( wheels.size(), 0.0f );
    wheelCarInertia.assign( wheels.size(), 0.0f );
    drivenWheels.assign( wheels.size(), false );
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        WheelDesc &wheel = wheels[i];
        const TyreSetup &tyre = wheel.bFront ? params.TyreFront : params.TyreRear;
        // DBRims radius (FUN_00df3b30 GEO +0x1c / +0x20) [CODE].
        wheel.Radius = tyre.Radius;
        // Axle PartInertia: the wheel body about its axle, from _wheel_mass_xx
        // and the radius by wheel vfunc 0x9c / FUN_0046e8f0 [ENC]: solid disc 1/2 m r^2 [INF].
        wheelPartInertia[i] = 0.5f * tyre.Mass * wheel.Radius * wheel.Radius;
        // Car inertia seen by the axle: mass x LoadDistributionAtRest (front
        // LoadDistribution / 2, rear (1 - LoadDistribution) / 2, FUN_0086c0d0)
        // x LoadedRadius^2 (FUN_0086fea0) [CODE].
        const float share = 0.5f * ( wheel.bFront ? params.LoadDistribution : 1.0f - params.LoadDistribution );
        wheelCarInertia[i] = mass * share * wheel.Radius * wheel.Radius;
        drivenWheels[i] = wheel.bFront ? params.Drive != VehicleSetup::eDrive::Rear : params.Drive != VehicleSetup::eDrive::Front;
        // Brake controller Flags: bit0 brakes, bit2 hand brake (rear) [INF:
        // the setter of axle +0x64 was not found].
        brakes[i].Flags = 1u | ( wheel.bFront ? 0u : 4u );
    }

    // Gearbox (FUN_00bdb970 + FUN_00884220): GearRatioArray, MaxGearRatio = _nb_gears + 1 [CODE].
    for ( float &ratio : gearbox.Ratio ) {
        ratio = 0.0f;
    }
    gearbox.Ratio[0] = -params.ReverseRatio;
    gearbox.Ratio[1] = 0.0f;
    for ( size_t i = 0; i < params.Gears.size() && i < 7; i++ ) {
        gearbox.Ratio[2 + i] = params.Gears[i];
    }
    gearbox.Ratio[9] = 1.0f;
    gearbox.MinGear = 0;
    gearbox.MaxGear = static_cast<int32_t>( params.Gears.size() ) + 1;
    blueLine = params.BlueLine;
    redLine = params.RedLine;

    // Centre of mass: z = FrontAxleLocalPos - (1 - LoadDistribution) x WheelBase
    // (vfunc 0xf4 calls of FUN_00bdb970) [CODE]; y = _gravity_center_height / 100
    // above the hub line [INF: the frame of GEO +0x88 is not read].
    float hubY = 0.0f, zFront = params.FrontAxleLocalPos, zRear = params.FrontAxleLocalPos - params.WheelBase;
    if ( !wheels.empty() ) {
        float sumFront = 0.0f, sumRear = 0.0f;
        int32_t numFront = 0, numRear = 0;
        for ( const WheelDesc &wheel : wheels ) {
            hubY += wheel.HubLocal.y / static_cast<float>( wheels.size() );
            ( wheel.bFront ? sumFront : sumRear ) += wheel.HubLocal.z;
            ( wheel.bFront ? numFront : numRear )++;
        }
        if ( numFront > 0 && numRear > 0 ) {
            zFront = sumFront / numFront;
            zRear = sumRear / numRear;
        }
    }
    const float wheelBase = Max( zFront - zRear, 0.5f );
    centerOfMass = Vector3f( 0.0f, hubY + params.CgHeight, zFront - ( 1.0f - params.LoadDistribution ) * wheelBase );

    // Ray start: hub at the upper stop (full bump); the hub may drop to the
    // lower stop [INF: the wheel contact of edVehicle (FUN_008720e0) uses
    // ENC vector helpers].
    rayStarts.clear();
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        const WheelDesc &wheel = wheels[i];
        const SuspensionSetup &s = wheel.bFront ? params.SuspFront : params.SuspRear;
        rayStarts.push_back( wheel.HubLocal + Vector3f( 0.0f, s.UpperStop, 0.0f ) );
        // Static compression from ForceAtRest = share x m x |g| (FUN_0086c0d0) [CODE value, INF spring law].
        const float share = 0.5f * ( wheel.bFront ? params.LoadDistribution : 1.0f - params.LoadDistribution );
        const float rest = Clamp( share * mass * kGravity / Max( s.StiffnessAtRest, 1000.0f ) + s.LowerStop, s.LowerStop, s.UpperStop );
        wheelStates[i].Compression = rest;
        wheelStates[i].PreviousCompression = rest;
    }

    // Chassis spheres: corners of the .dhk chassis boxes (body 0), raised
    // by Physics.cpr CollisionOffset [INF].
    Vector3f boundsMin( -0.9f, 0.2f, -2.2f ), boundsMax( 0.9f, 1.2f, 2.2f );
    if ( !params.ChassisBoxes.empty() ) {
        boundsMin = Vector3f( 1e9f, 1e9f, 1e9f );
        boundsMax = Vector3f( -1e9f, -1e9f, -1e9f );
        for ( const VehicleSetup::Box &box : params.ChassisBoxes ) {
            boundsMin = Vector3f( Min( boundsMin.x, box.Center.x - box.HalfExtents.x ), Min( boundsMin.y, box.Center.y - box.HalfExtents.y ),
                                  Min( boundsMin.z, box.Center.z - box.HalfExtents.z ) );
            boundsMax = Vector3f( Max( boundsMax.x, box.Center.x + box.HalfExtents.x ), Max( boundsMax.y, box.Center.y + box.HalfExtents.y ),
                                  Max( boundsMax.z, box.Center.z + box.HalfExtents.z ) );
        }
    }
    collisionRadius = 0.3f;
    const float r = collisionRadius;
    const float yLow = boundsMin.y + params.CollisionOffset + r;
    const float yHigh = Max( boundsMax.y - r, yLow );
    collisionSpheres.clear();
    for ( float z : { boundsMin.z + r, 0.5f * ( boundsMin.z + boundsMax.z ), boundsMax.z - r } ) {
        for ( float x : { boundsMin.x + r, boundsMax.x - r } ) {
            for ( float y : { yLow, yHigh } ) {
                collisionSpheres.emplace_back( x, y, z );
            }
        }
    }
}

void VehicleDynamics::reset( const Vector3f &newPosition, const Quaternionf &newOrientation, const Vector3f &newVelocity ) {
    position = newPosition;
    orientation = Normalize( newOrientation );
    velocity = newVelocity;
    angularVelocity = Vector3f();
    previousSphereCenters.clear();
    tickAccumulator = 0.0f;
    const Vector3f forward = Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) );
    const float forwardSpeed = velocity.dot( forward );
    for ( size_t i = 0; i < wheelStates.size(); i++ ) {
        wheelStates[i].AngularVelocity = forwardSpeed / wheels[i].Radius;
        wheelStates[i].SlipRatio = 0.0f;
        brakes[i].Position = 0.0f;
        brakes[i].bHasTarget = false;
    }
    // Engine controller / clutch reset (FUN_00888b90, FUN_00880ee0): running,
    // clutch engaged [CODE ctor state].
    engine = EngineState();
    engine.State = 1;
    clutch = ClutchState();
    // Gear for the speed: first index from MinGearRatio whose top speed x 0.9
    // exceeds the speed (FUN_00885730 called by FUN_008866a0 with the
    // controller reset flag) [CODE]; top speed of a gear = MaxVelocity /
    // ratio x radius [INF: vehicle vfunc 0x124 ENC]. At rest: neutral.
    gearbox.CtrlFlags = 2;
    gearbox.Flags = 0;
    gearbox.Elapsed = 0.0f;
    const float radius = wheels.empty() ? 0.34f : wheels.back().Radius;
    int32_t gear = 1;
    if ( forwardSpeed > 1.0f ) {
        gear = 2;
        while ( gear < gearbox.MaxGear ) {
            const float ratio = gearRatio( gear );
            const float top = ratio != 0.0f ? params.EngineMaxVelocity / ratio * radius : 0.0f;
            if ( forwardSpeed <= top * 0.9f ) {
                break;
            }
            gear++;
        }
    }
    gearbox.Gear = gearbox.Shifted = gear;
    gearbox.State = gear == 0 ? 2 : ( gear == 1 ? 3 : 4 );
    const float ratio = gearRatio( gear );
    engine.Velocity = Clamp( std::abs( forwardSpeed / radius * ratio ), params.IdleVelocity, params.EngineMaxVelocity );
    clutch.Position = gear == 1 ? 1.0f : 0.0f;
    clutch.State = gear == 1 ? 3 : 1;
    steerAngle = 0.0f;
    reverseTimer = 0.0f;
    airTime = 0.0f;
}

Matrix4x4f VehicleDynamics::getWorldMatrix() const {
    return Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( orientation );
}

float VehicleDynamics::getSpeedKmh() const {
    return velocity.dot( Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) ) ) * 3.6f;
}

Vector3f VehicleDynamics::worldInverseInertia( const Vector3f &torque ) const {
    const Vector3f local = InverseRotate( orientation, torque );
    const Vector3f scaled( local.x / Max( inertia.x, 1.0f ), local.y / Max( inertia.y, 1.0f ), local.z / Max( inertia.z, 1.0f ) );
    return Rotate( orientation, scaled );
}

void VehicleDynamics::applyImpulse( const Vector3f &impulse, const Vector3f &point ) {
    const Vector3f com = position + Rotate( orientation, centerOfMass );
    velocity += impulse * ( 1.0f / mass );
    angularVelocity += worldInverseInertia( ( point - com ).cross( impulse ) );
}

// The physics thread runs every car at 90 Hz with dt <= 1/90 s
// (FUN_00bd07c0 / FUN_00bd03a0) [CODE]: the frame time is consumed in
// 1/90 s ticks.
void VehicleDynamics::step( float deltaTime, const Input &input, const VehicleWorldCollision &collision ) {
    if ( wheels.empty() || deltaTime <= 0.0f ) {
        return;
    }
    tickAccumulator = Min( tickAccumulator + deltaTime, 0.25f );
    const float h = 1.0f / kTickRate;
    while ( tickAccumulator >= h * 0.999f ) {
        tick( Min( h, kMaxTickDt ), input, collision );
        tickAccumulator -= h;
    }
}

// Engine torque FUN_008880f0: EngineState 0: CurveActionStarter(w); 1 / 3:
// P Tmax(w) + (1 - P) Tno(w); otherwise Tno(w), with Tno = min(No(w),
// -1e-5) (w >= 0, FUN_008880a0) and Tmax = max(Max(Idle), Max(w), 1e-5)
// below MaxVelocity, 1e-5 above (FUN_00888020) [CODE].
float VehicleDynamics::engineTorque( float w ) const {
    const auto noThrottle = [&]( float x ) {
        if ( x >= 0.0f ) {
            const float y = params.TorqueMin.sample( x );
            if ( y < -1e-5f ) {
                return y;
            }
        }
        return -1e-5f;
    };
    const auto maxThrottle = [&]( float x ) {
        if ( params.EngineMaxVelocity < x ) {
            return 1e-5f;
        }
        const float m = Max( params.TorqueMax.sample( params.IdleVelocity ), params.TorqueMax.sample( x ) );
        return m < 1e-5f ? 1e-5f : m;
    };
    switch ( engine.State ) {
    case 0:
        return 0.0f; // CurveActionStarter: not set by the car factory [CODE: no write], empty curve
    case 1:
    case 3: {
        const double p = engine.Position;
        const double t = static_cast<double>( maxThrottle( w ) ) * p + static_cast<double>( noThrottle( w ) ) * ( 1.0 - p );
        return static_cast<float>( t );
    }
    default:
        return noThrottle( w );
    }
}

// Tyre torque of a wheel at spin omega: Torque = -LoadedRadius x
// LongitudinalForce (FUN_00868a60), the contact frame being the one of the
// tick (FUN_00870630 calls the tyre update at every drive train stage) [CODE].
float VehicleDynamics::tyreTorque( size_t i, float omega, bool bUpdateState ) {
    WheelState &state = wheelStates[i];
    if ( !state.bContact || state.Load <= 0.0f ) {
        return 0.0f;
    }
    const ContactFrame &frame = contactFrames[i];
    const TyreSetup &tyre = wheels[i].bFront ? params.TyreFront : params.TyreRear;
    const TyreResult result = EvaluateTyre( tyre, state.Surface, frame.FrictionScale, state.Load, frame.LongVelocity,
                                            frame.LatVelocity, omega, wheels[i].Radius );
    if ( bUpdateState ) {
        state.SlipRatio = result.SlipRatio;
        state.SlipAngle = result.SlipAngle;
        state.LateralForce = result.Fy;
        state.LongitudinalForce = result.Fx;
        state.SlippingCoefficient = result.Slipping;
    }
    return -wheels[i].Radius * result.Fx;
}

// --- Controls (game FUN_00bacd50) -------------------------------------------
void VehicleDynamics::applyControls( const Input &input, float forwardSpeed ) {
    const float absSpeed = std::abs( forwardSpeed );
    // Steering (edVehicle +0x109c: CurveMaxVelocity = SteeringVelocity,
    // CurveMaxAngle, CurveSensitivity; m/s -> rad/s, rad) [CODE curves; the
    // steering column integration of FUN_0087b590 uses feedback impulses not
    // decoded: rate-limited angle INF]. Right input turns the wheels towards -x.
    const float maxAngle = params.SteeringMaxAngle.sample( absSpeed );
    const float sensitivity = params.SteeringSensitivity.sample( absSpeed );
    const float target = -Clamp( input.Steering * sensitivity, -1.0f, 1.0f ) * maxAngle;
    const float rate = params.SteeringVelocity.sample( absSpeed ) / kTickRate;
    steerAngle += Clamp( target - steerAngle, -rate, rate );

    // Blue / red lines: normal or sport by a throttle hysteresis, evaluated
    // when the throttle rises above the previous engine pedal (game object
    // +0x44..+0x5c, +0x4c) [CODE].
    const float throttle = Clamp( input.Throttle, 0.0f, 1.0f );
    if ( throttle > engine.Pedal ) {
        bSportLines = bSportLines ? throttle > params.ThrottleRatioNormal : throttle >= params.ThrottleRatioSport;
        blueLine = bSportLines ? params.BlueLineSport : params.BlueLine;
        redLine = bSportLines ? params.RedLineSport : params.RedLine;
    }
    // Engine controller PedalPosition = throttle, aids PedalPosition = brake
    // (+0xd0, previous +0xcc), hand brake = aids flag bit3 [CODE].
    engine.Pedal = throttle;
    aidBrakePreviousPosition = aidBrakePosition;
    aidBrakePosition = Clamp( input.Brake, 0.0f, 1.0f );
    aidFlags = input.bHandBrake ? ( aidFlags | 8u ) : ( aidFlags & ~8u );
}

// --- Clutch controller (clutch +0x90, FUN_008810c0 / FUN_00880fe0) ---------
void VehicleDynamics::clutchSetPosition( float p ) {
    if ( clutch.Position == p ) {
        return;
    }
    if ( p == 0.0f ) {
        if ( clutch.State != 1 ) {
            clutch.State = 1;
            clutch.Elapsed = 0.0f;
        }
    } else if ( p == 1.0f ) {
        if ( clutch.State != 3 ) {
            clutch.State = 3;
            clutch.Elapsed = 0.0f;
        }
    } else if ( p > clutch.Position ) {
        if ( clutch.State != 2 ) {
            clutch.State = 2;
            clutch.Elapsed = 0.0f;
        }
    } else if ( clutch.State != 0 ) {
        clutch.State = 0;
        clutch.Elapsed = 0.0f;
    }
    clutch.Position = p;
}

void VehicleDynamics::updateClutchController( float dt ) {
    if ( clutch.State == 0 ) {
        // Engaging: Position = max(1 - t / EngagingTime, 0) [CODE].
        if ( params.ClutchEngagingTime == 0.0f ) {
            clutchSetPosition( 0.0f );
            return;
        }
        clutch.Elapsed += dt;
        const float p = 1.0f - clutch.Elapsed / params.ClutchEngagingTime;
        clutchSetPosition( p < 0.0f ? 0.0f : p );
    } else if ( clutch.State == 2 ) {
        // Disengaging: Position = min(t / DisengagingTime, 1) [CODE].
        float p = 1.0f;
        if ( params.ClutchDisengagingTime != 0.0f ) {
            clutch.Elapsed += dt;
            p = clutch.Elapsed / params.ClutchDisengagingTime;
            if ( p > 1.0f ) {
                p = 1.0f;
            }
        }
        clutchSetPosition( p );
    }
}

// Gear request (FUN_008856b0): clamp to [MinGearRatio, MaxGearRatio], new
// target -> ElapsedTime 0, State = (old <= new) (1 up / 0 down) [CODE].
void VehicleDynamics::requestGear( int32_t gear ) {
    gear = Clamp( gear, gearbox.MinGear, gearbox.MaxGear );
    if ( gearbox.Shifted != gear ) {
        gearbox.State = gearbox.Shifted <= gear ? 1 : 0;
        gearbox.Shifted = gear;
        gearbox.Elapsed = 0.0f;
    }
}

// --- Gearbox controller (gearbox +0x1f0, FUN_008866a0) ----------------------
void VehicleDynamics::updateGearboxController( float dt, float forwardSpeed ) {
    const bool bLowSpeed = velocity.dot( velocity ) <= kLowSpeed2; // vehicle flag 0x8000
    gearbox.CtrlFlags &= ~0x18u;
    // aids Position = PedalPosition, engine controller Position = PedalPosition.
    float aidPosition = aidBrakePosition;
    engine.CtrlPosition = engine.Pedal;
    // "both pedals" (flag 8): low speed, brake pressed, throttle above the idle position (Mode 0).
    const bool bBothPedals = bLowSpeed && aidBrakePosition != 0.0f && engine.Pedal > params.IdlePosition;
    if ( gearbox.Mode == 0 ) {
        gearbox.CtrlFlags = ( gearbox.Type == 0 && bBothPedals ) ? ( gearbox.CtrlFlags | 8u ) : ( gearbox.CtrlFlags & ~8u );
    }
    const float idlePosition = params.IdlePosition;
    // Shift sequence (FUN_008858e0 with ShiftTimeDown / ShiftTimeUp).
    const auto shift = [&]( float shiftTime ) {
        gearbox.CtrlFlags &= ~0x20u;
        const auto complete = [&]() {
            // FUN_00546f00 [ENC]: the gear is engaged and the clutch
            // re-engaged by the drive state [INF, as the neutral target branch].
            gearbox.Gear = gearbox.Shifted;
            gearbox.State = gearbox.Gear == 0 ? 2 : ( gearbox.Gear == 1 ? 3 : 4 );
        };
        if ( gearbox.Shifted == 1 ) {
            gearbox.CtrlFlags &= ~0x40u;
            complete();
            return;
        }
        if ( gearbox.Shifted == gearbox.Gear ) {
            gearbox.CtrlFlags &= ~0x40u;
        }
        if ( clutch.Type == 0 ) {
            if ( clutch.State != 3 ) {
                if ( clutch.State != 2 ) {
                    clutch.State = 2;
                    clutch.Elapsed = 0.0f;
                }
                updateClutchController( dt );
                return;
            }
            if ( gearbox.Type != 1 ) {
                gearbox.Elapsed += dt;
                if ( gearbox.Elapsed <= shiftTime ) {
                    return;
                }
            }
            complete();
            return;
        }
        complete();
    };
    switch ( gearbox.State ) {
    case 0: // shifting down
    case 1: // shifting up
        if ( clutch.Type == 0 && gearbox.Gear != 1 ) {
            engine.CtrlPosition = 0.0f; // FUN_00885ae0 / FUN_00885a50: engine Position(+0x38) = 0 while shifting [CODE]
        }
        shift( gearbox.State == 0 ? params.ShiftTimeDown : params.ShiftTimeUp );
        break;
    case 2: { // reverse (FUN_00885e20): the pedals swap [CODE]
        if ( clutch.Type == 0 && clutch.State != 1 && gearbox.Shifted == gearbox.Gear ) {
            if ( clutch.State != 0 ) {
                clutch.State = 0;
                clutch.Elapsed = 0.0f;
            }
            updateClutchController( dt );
        }
        engine.CtrlPosition = aidBrakePosition;
        aidPosition = engine.Pedal;
        // Back to neutral at low speed when the reverse pedal (brake) is
        // released [INF: the exit test of FUN_00885e20 was not fully read].
        if ( gearbox.Type == 0 && bLowSpeed && aidBrakePosition == 0.0f && forwardSpeed > -0.5f ) {
            requestGear( 1 );
        }
        break;
    }
    case 3: { // neutral (FUN_00886440)
        if ( clutch.Type == 0 ) {
            clutchSetPosition( 1.0f );
        }
        if ( gearbox.Type == 0 && engine.State >= 1 && engine.State <= 3 ) {
            if ( gearbox.CtrlFlags & 8u ) {
                aidPosition = Max( aidBrakePosition, engine.Pedal );
                gearbox.Elapsed = 0.0f;
            } else if ( bLowSpeed ) {
                // FUN_0053fbd0(vehicle, 2) [ENC]: forward speed [INF].
                if ( forwardSpeed < -0.1f ) {
                    if ( aidBrakePosition == 1.0f ) {
                        requestGear( 0 );
                    } else if ( engine.Pedal > idlePosition && forwardSpeed >= -0.3f ) {
                        requestGear( 2 );
                    }
                    gearbox.Elapsed = 0.0f;
                } else if ( engine.Pedal <= idlePosition ) { // FUN_00546ec0(engine) [ENC: INF throttle released]
                    if ( aidBrakePosition == 1.0f ) {
                        gearbox.Elapsed += dt;
                        if ( gearbox.Elapsed > params.ReverseTime ) {
                            requestGear( 0 );
                        }
                    } else {
                        gearbox.Elapsed = 0.0f;
                    }
                } else {
                    requestGear( 2 );
                }
            } else if ( engine.Pedal > idlePosition && forwardSpeed > 0.0f ) {
                // Rolling forward above 10 m/s in neutral with throttle: the
                // gear for the speed (FUN_00885730) [CODE rule, INF trigger].
                int32_t gear = 2;
                const float radius = wheels.back().Radius;
                while ( gear < gearbox.MaxGear && forwardSpeed > 0.9f * params.EngineMaxVelocity / gearRatio( gear ) * radius ) {
                    gear++;
                }
                requestGear( gear );
            }
        }
        break;
    }
    case 4: { // drive (FUN_00886230)
        if ( clutch.Type == 0 && clutch.State != 1 && gearbox.Gear == gearbox.Shifted ) {
            if ( gearbox.Gear != 2 && !bLowSpeed ) {
                engine.Pedal = 0.0f;
            }
            if ( clutch.State != 0 ) {
                clutch.State = 0;
                clutch.Elapsed = 0.0f;
            }
            updateClutchController( dt );
        }
        if ( gearbox.Type == 0 ) {
            if ( gearbox.CtrlFlags & 8u ) {
                requestGear( 1 );
            } else if ( ( gearbox.Flags & 8u ) != 0 ) {
                if ( engine.Pedal <= idlePosition ) {
                    requestGear( 1 );
                }
            }
            if ( gearbox.Flags & 0x40u ) {
                requestGear( gearbox.Gear + 1 ); // FUN_00885840
            } else if ( gearbox.Flags & 0x20u ) {
                requestGear( gearbox.Gear - 1 ); // FUN_00885890
            }
        }
        break;
    }
    }
    // Clutch disengage request (flag 0x10): both pedals / hand brake [CODE].
    bool bDisengage = ( gearbox.CtrlFlags & 0x10u ) != 0;
    if ( clutch.Type == 0 ) {
        if ( gearbox.Mode == 0 ) {
            bDisengage |= ( gearbox.CtrlFlags & 8u ) != 0;
        }
        bDisengage |= ( aidFlags & 8u ) != 0;
    }
    gearbox.CtrlFlags = bDisengage ? ( gearbox.CtrlFlags | 0x10u ) : ( gearbox.CtrlFlags & ~0x10u );
    // aids flag bit0 (wheels held by the brakes): both pedals (automatic).
    aidFlags = ( gearbox.Type == 0 && ( gearbox.CtrlFlags & 8u ) ) ? ( aidFlags | 1u ) : ( aidFlags & ~1u );
    if ( clutch.Type == 0 && bDisengage ) {
        clutchSetPosition( 1.0f );
    }
    aidBrakePositionApplied = aidPosition;
}

// --- Engine controller (engine +0x3b0, FUN_0088a410) ------------------------
void VehicleDynamics::updateEngineController( float dt ) {
    switch ( engine.State ) {
    case 0: // starting
        engine.Elapsed += dt;
        if ( engine.Elapsed > params.StartingTime && ( engine.CtrlFlags & 1u ) == 0 ) {
            engine.State = 1;
            engine.Elapsed = 0.0f;
        }
        break;
    case 1: { // running: Position = min(Position, MaxPosition)
        float position = Min( engine.CtrlPosition, params.MaxPosition );
        // FUN_00546910(engine) [ENC: INF engine at / under the idle velocity]:
        // Position clamped to [IdlePosition, MaxPosition] (idle governor);
        // stalling only with the controller flag bit1 (not set) [CODE].
        if ( engine.Velocity <= params.IdleVelocity * 1.0001f ) {
            position = Clamp( position, params.IdlePosition, params.MaxPosition );
            if ( ( engine.CtrlFlags & 2u ) != 0 && clutch.State != 3 && gearbox.Gear != 1 ) {
                engine.StarterState = 2;
                if ( ( engine.CtrlFlags & 1u ) == 0 ) {
                    engine.State = 3;
                    engine.Elapsed = 0.0f;
                }
            }
        } else if ( engine.Velocity >= params.EngineMaxVelocity * 0.9999f && params.LimiterTime > 0.0f &&
                    ( engine.CtrlFlags & 1u ) == 0 ) {
            // FUN_00546810(engine) [ENC: INF at MaxVelocity, where the part
            // clamps the speed (FUN_00887f10)]: limiter, no-throttle torque
            // for LimiterTime (_rupteur_time) [CODE].
            engine.State = 2;
            engine.Elapsed = 0.0f;
        }
        engine.CtrlPosition = position;
        break;
    }
    case 2: // limiter
        engine.Elapsed += dt;
        if ( engine.Elapsed > params.LimiterTime && ( engine.CtrlFlags & 1u ) == 0 ) {
            engine.State = 1;
            engine.Elapsed = 0.0f;
        }
        break;
    case 3: // stalling
        engine.Elapsed += dt;
        if ( engine.Elapsed > params.StallingTime && ( engine.CtrlFlags & 1u ) == 0 ) {
            engine.State = 4;
            engine.Elapsed = 0.0f;
        }
        break;
    default:
        break;
    }
    // Engine vfunc 0x2c: engine Position (+0x390) = controller Position (+0x3e8) [CODE].
    engine.Position = engine.CtrlPosition;
}

// --- Brake controllers (axle +0x50, FUN_00865750) ---------------------------
void VehicleDynamics::updateBrakeControllers( float dt, const std::vector<float> &wheelOmega, float engineOmega ) {
    (void)engineOmega;
    const float speed = std::abs( velocity.dot( Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) ) ) );
    aidFlags &= ~( 4u | 0x20u );
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        BrakeController &brake = brakes[i];
        WheelState &state = wheelStates[i];
        const float radius = wheels[i].Radius;
        const float longVelocity = contactFrames[i].LongVelocity;
        const float absLongVelocity = std::abs( longVelocity ); // Tire AbsLongitudinalVelocity (+0x60)
        const float omega = wheelOmega[i];
        const float maxBraking = params.MaxBrakingAction * ( wheels[i].bFront ? params.BrakingBalance : 1.0f - params.BrakingBalance );
        // Axle shaft inertia (combined end inertia; own axle inertia here [INF]).
        const float shaftInertia = wheelPartInertia[i] + wheelCarInertia[i] * ( 1.0f - state.AxleSlipCoefficient );
        // FUN_0086f950: brake position needed to reach a target velocity in
        // this stage: |J| / (dt x MaxBrakingAction) (<= 1), 0 when J does
        // not oppose the spin [CODE].
        const auto fraction = [&]( float target ) {
            const float j = ( target - omega ) * shaftInertia;
            if ( Sign( j ) == Sign( omega ) ) {
                return 0.0f;
            }
            const float f = std::abs( j / ( dt * maxBraking ) );
            return f < 1.0f ? f : 1.0f;
        };
        const auto slipTarget = [&]( float slipRatio ) {
            return absLongVelocity < kWheelBrakeTargetMinSpeed ? 0.0f : ( slipRatio + 1.0f ) * longVelocity / radius;
        };
        brake.bHasTarget = false;
        state.bAbs = state.bTcs = false;
        if ( ( brake.Flags & 4u ) != 0 && ( aidFlags & 8u ) != 0 ) {
            // Hand brake: drives the wheel to (1 + HandBrakeSlipRatio) v / R
            // with the full brake (FUN_00865750) [CODE].
            if ( absLongVelocity < kWheelBrakeTargetMinSpeed ) {
                brake.Velocity = 0.0f;
                brake.bHasTarget = true;
                brake.Position = 1.0f;
                continue;
            }
            const float target = ( aidSlipRatios[1] + 1.0f ) * longVelocity / radius;
            if ( target != 0.0f && Sign( target ) != ( omega >= 0.0f ? 1.0f : -1.0f ) ) {
                continue;
            }
            brake.Velocity = target;
            brake.bHasTarget = true;
            brake.Position = 1.0f;
            continue;
        }
        if ( ( aidFlags & 1u ) != 0 ) {
            // Wheels held (both pedals, automatic box): full brake to 0 [CODE].
            brake.Velocity = 0.0f;
            brake.bHasTarget = true;
            brake.Position = 1.0f;
            continue;
        }
        brake.Position = 0.0f;
        const float pedal = aidBrakePositionApplied;
        if ( ( brake.Flags & 1u ) == 0 || pedal == 0.0f ) {
            // No brake (non driven wheels: free rolling target FUN_0054ebe0 [ENC]).
        } else {
            brake.Position = pedal;
            // ABS (FUN_00865460) when active (FUN_0054eae0 [ENC: INF _abs and
            // the speed above AbsControlActivation]) [CODE law].
            if ( params.bAbs && speed >= params.AbsActivation ) {
                const float target = slipTarget( aidSlipRatios[0] );
                if ( Sign( target ) == Sign( omega ) ) {
                    const float f = fraction( target );
                    if ( f < brake.Position ) {
                        aidFlags |= 4u;
                        state.bAbs = true;
                        brake.Velocity = target;
                        brake.bHasTarget = true;
                        brake.Position = f;
                    }
                }
            }
        }
        // Traction control (FUN_008652b0) when active (FUN_0054eaa0 [ENC: INF
        // _traction_control, driven wheel, speed above TractionControlActivation]) [CODE law].
        if ( params.bTractionControl && drivenWheels[i] && speed >= params.TractionActivation ) {
            const float target = slipTarget( aidSlipRatios[2] );
            if ( Sign( target ) == Sign( omega ) ) {
                const float f = fraction( target );
                if ( brake.Position < f ) {
                    aidFlags |= 0x20u;
                    state.bTcs = true;
                    brake.Velocity = target;
                    brake.bHasTarget = true;
                    brake.Position = f;
                }
            }
        }
    }
}

// --- Gearbox part flags (FUN_00884d80, after the velocity propagation) -------
void VehicleDynamics::updateGearboxFlags( float forwardSpeed, const std::vector<float> &wheelOmega ) {
    const float vE = engine.Velocity;
    uint32_t flags = 0;
    if ( vE > redLine * 0.5f ) {
        flags |= 4u;
    }
    if ( !( params.IdlePosition < engine.CtrlPosition ) && !( params.IdleVelocity + 1.0f < vE ) ) {
        flags |= 8u;
    }
    if ( vE > params.IdleVelocity ) {
        flags |= 0x10u;
    }
    // Gearbox output (differential input) velocity = carrier x final drive.
    float carrier = 0.0f;
    int32_t numDriven = 0;
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        if ( drivenWheels[i] ) {
            carrier += wheelOmega[i];
            numDriven++;
        }
    }
    carrier = numDriven > 0 ? carrier / numDriven : 0.0f;
    const float outputVelocity = carrier * params.FinalDrive;
    if ( gearbox.Gear > 2 && gearbox.Ratio[9] * gearbox.Ratio[gearbox.Gear - 1] * outputVelocity < blueLine ) {
        flags |= 0x20u; // shift down: the lower gear stays under the blue line
    }
    // Shift up: engine over the red line and the car speed (vehicle vfunc
    // 0xcc [ENC: INF forward speed]) above 0.8 x the speed of the red line in
    // this gear (vfunc 0x120 [ENC: INF red line / ratio x radius]) [CODE].
    const float ratio = gearRatio( gearbox.Gear );
    const float radius = wheels.empty() ? 0.34f : wheels.back().Radius;
    const float redLineSpeed = ratio != 0.0f ? redLine / ratio * radius : 0.0f;
    if ( vE > redLine && forwardSpeed > redLineSpeed * 0.8f ) {
        flags |= 0x40u;
    }
    gearbox.Flags = flags;
}

// --- Drive train ---------------------------------------------------------------
// One stage of a drive train sub step: impulses over dt from the state
// (engineOmega, wheelOmega) -> velocity increments. Laws [CODE] (spec 6.4);
// the shaft network is reduced to the engine shaft, the open differential
// carrier and the wheel axles [INF], the clutch / gearbox / differential
// keeping the decoded impulse laws.
void VehicleDynamics::driveTrainStage( float dt, float forwardSpeed, std::vector<float> &wheelOmega, float &engineOmega,
                                       std::vector<float> &wheelRate, float &engineRate ) {
    (void)forwardSpeed;
    const size_t n = wheels.size();
    std::vector<float> wheelInertia( n ), freeOmega( n );
    const float r = gearRatio( gearbox.Gear );
    const bool bConnected = r != 0.0f;
    // Engine side inertia: _engine_inertia x 0.01 + clutch PartInertia[0]; the
    // clutch output adds PartInertia[1] and the gearbox GearInertiaArray[0]
    // (_gearbox_inertia x 0.01) (FUN_008785e0 / FUN_008852d0) [CODE values].
    const float inertiaIn = params.EngineInertia + params.ClutchPartInertia;
    int32_t numDriven = 0;
    for ( size_t i = 0; i < n; i++ ) {
        numDriven += drivenWheels[i] ? 1 : 0;
    }
    // Upstream inertia seen by a driven axle (engine side reflected) for the
    // slip coefficient (FUN_0086fea0 adds the connected shaft inertia) [INF share].
    const float upstreamShare = bConnected && clutch.Dof == 0.0f && numDriven > 0
                                    ? ( inertiaIn + params.ClutchPartInertia + params.GearboxInertia ) * r * r / numDriven
                                    : 0.0f;
    for ( size_t i = 0; i < n; i++ ) {
        WheelState &state = wheelStates[i];
        const float radius = wheels[i].Radius;
        // Tyre torque impulse (FUN_00870630: Torque x dt x Direction) [CODE].
        const float torque = tyreTorque( i, wheelOmega[i], false );
        const float tyreImpulse = torque * dt;
        // Axle slip coefficient (FUN_0086fea0) [CODE]: Imax = MaxLongitudinalForce
        // x R x dt, need = |(w - v / R) x Icar Itot / (Icar + Itot)|.
        const TyreSetup &tyre = wheels[i].bFront ? params.TyreFront : params.TyreRear;
        const float maxLongForce = state.bContact ? tyre.MaxLongitudinalFriction[state.Surface] * state.Load * 0.001f *
                                                        contactFrames[i].FrictionScale
                                                  : 0.0f;
        const float maxImpulse = maxLongForce * radius * dt;
        const float part = wheelPartInertia[i];
        const float total = part + ( drivenWheels[i] ? upstreamShare : 0.0f );
        const float car = wheelCarInertia[i];
        const float need = std::abs( ( wheelOmega[i] - ( contactFrames[i].LongVelocity - 0.0f ) / radius ) * ( car * total / ( car + total ) ) );
        float c;
        if ( maxImpulse == 0.0f ) {
            c = 1.0f;
        } else if ( maxImpulse < need - 9.999999747378752e-06f ) {
            c = Clamp( 1.0f - maxImpulse / need, 0.0f, 1.0f );
        } else {
            c = 0.0f;
        }
        state.AxleSlipCoefficient = c;
        // Effective axle inertia (FUN_008701f0) [CODE].
        wheelInertia[i] = static_cast<float>( ( ( 1.0 - c ) * total * car ) / ( c * car + total ) + part );
        freeOmega[i] = wheelOmega[i] + tyreImpulse / wheelInertia[i];
    }
    // Engine torque and damping (FUN_00888550): damping impulse only when the
    // torque is positive, coefficient DampingArray[0] (_engine_damping) under
    // DampingArray[2], stop clamped (FUN_008877e0 / FUN_00887400) [CODE].
    const float torque = engineTorque( engineOmega );
    float engineImpulse = torque * dt;
    if ( torque > 0.0f ) {
        const float coefficient = std::abs( engineOmega ) > kShaftDampingThreshold ? 0.0f : params.EngineDamping;
        float damping = -coefficient * engineOmega * dt;
        const float stop = std::abs( engineOmega * inertiaIn );
        damping = Clamp( damping, -stop, stop );
        engineImpulse += damping;
    }
    engine.PreviousAction = torque;
    float engineNew = engineOmega + engineImpulse / inertiaIn;
    std::vector<float> wheelNew = freeOmega;

    if ( bConnected && numDriven > 0 ) {
        float meanFree = 0.0f, meanInvInertia = 0.0f, inertiaDriven = 0.0f, carrier = 0.0f;
        for ( size_t i = 0; i < n; i++ ) {
            if ( drivenWheels[i] ) {
                meanFree += freeOmega[i] / numDriven;
                meanInvInertia += 1.0f / wheelInertia[i] / numDriven;
                inertiaDriven += wheelInertia[i];
                carrier += wheelOmega[i] / numDriven;
            }
        }
        // Clutch slip coefficient (FUN_00877e50) [CODE]: maxImp = clamp((1 -
        // Position) x MaxAction x dt, 0, 100), need = |(vin - vout) Iin Iout /
        // (Iin + Iout)|; a locked clutch stays locked while maxImp > 0.
        const float inertiaOut = params.ClutchPartInertia + params.GearboxInertia + inertiaDriven / ( r * r );
        float maxClutch = ( 1.0f - clutch.Position ) * params.ClutchMaxAction * dt;
        maxClutch = maxClutch < 0.0f ? 0.0f : ( maxClutch > kClutchMaxImpulse ? kClutchMaxImpulse : maxClutch );
        const float needClutch = std::abs( ( engineOmega - r * carrier ) * ( ( inertiaIn * inertiaOut ) / ( inertiaIn + inertiaOut ) ) );
        if ( maxClutch == 0.0f ) {
            clutch.Dof = 1.0f;
        } else if ( maxClutch < needClutch - 9.999999747378752e-06f && clutch.Dof != 0.0f ) {
            clutch.Dof = Clamp( 1.0f - maxClutch / needClutch, 0.0f, 1.0f );
        } else {
            clutch.Dof = 0.0f;
        }
        float carrierImpulse = 0.0f; // wheel side, split equally (open differential)
        if ( clutch.Dof == 0.0f ) {
            // Locked chain: engine and carrier tied by the ratio.
            carrierImpulse = ( engineNew - r * meanFree ) / ( 1.0f / ( r * inertiaIn ) + r * meanInvInertia / numDriven );
            engineNew -= carrierImpulse / r / inertiaIn;
        } else if ( clutch.Dof < 1.0f ) {
            // Slipping: the transmitted impulse (1 - c) need = maxImp.
            const float clutchImpulse = Sign( engineOmega - r * carrier ) * ( 1.0f - clutch.Dof ) * needClutch;
            engineNew -= clutchImpulse / inertiaIn;
            carrierImpulse = clutchImpulse * r;
        }
        for ( size_t i = 0; i < n; i++ ) {
            if ( drivenWheels[i] ) {
                wheelNew[i] += carrierImpulse / numDriven / wheelInertia[i];
            }
        }
    }
    // Brakes: J = -sign(w) x Position x MaxBrakingAction x dt, stop clamped
    // (FUN_00870300); with a controller target (ABS / traction control /
    // hand brake) the wheel is driven towards the target velocity [CODE law,
    // INF application in the ENC axle step]. Rolling resistance [INF].
    for ( size_t i = 0; i < n; i++ ) {
        const BrakeController &brake = brakes[i];
        const float maxBraking = params.MaxBrakingAction * ( wheels[i].bFront ? params.BrakingBalance : 1.0f - params.BrakingBalance );
        const float limit = brake.Position * maxBraking * dt / wheelInertia[i];
        const float target = brake.bHasTarget ? brake.Velocity : 0.0f;
        if ( limit > 0.0f ) {
            const float delta = target - wheelNew[i];
            wheelNew[i] += Clamp( delta, -limit, limit );
        }
    }
    // Differential speed limit (FUN_00882420): MaxTorqueBias 0 = spool,
    // under 1 the half difference of the outputs is kept within |bias m /
    // (2 - bias)| [CODE].
    if ( numDriven == 2 ) {
        size_t a = n, b = n;
        for ( size_t i = 0; i < n; i++ ) {
            if ( drivenWheels[i] ) {
                ( a == n ? a : b ) = i;
            }
        }
        const float bias = params.DifferentialMaxTorqueBias;
        const float mean = 0.5f * ( wheelNew[a] + wheelNew[b] );
        float half = 0.5f * ( wheelNew[a] - wheelNew[b] );
        if ( bias == 0.0f ) {
            half = 0.0f;
        } else if ( bias < 1.0f ) {
            const float limit = std::abs( bias * mean / ( 2.0f - bias ) );
            half = Clamp( half, -limit, limit );
        }
        wheelNew[a] = mean + half;
        wheelNew[b] = mean - half;
    }
    for ( size_t i = 0; i < n; i++ ) {
        wheelRate[i] = wheelNew[i] - wheelOmega[i];
        stageDriveImpulse[i] = ( wheelNew[i] - freeOmega[i] ) * wheelInertia[i];
        wheelEffInertia[i] = wheelInertia[i];
    }
    engineRate = engineNew - engineOmega;
}

// Drive train sub steps (FUN_0087aa70): DriveTrainSubStepCount sub steps of
// 4 stages with the time steps [h, h/2, h/2, h] (FUN_0087ac80) [CODE]; the
// stage combination (vehicle vfunc 0x134 / 0x138, ENC) is the classic RK4
// one [INF]. The controllers run once per sub step with the sub step time
// (the RK4 step index stays at 3 after the stage loop) [INF].
void VehicleDynamics::updateDriveTrain( float h, float forwardSpeed ) {
    const size_t n = wheels.size();
    const int32_t numSub = Max( params.Substeps, 1 );
    const float hd = h / static_cast<float>( numSub );
    const float stageDt[4] = { hd, hd * 0.5f, hd * 0.5f, hd };
    const float stageAdvance[3] = { 0.5f, 1.0f, 2.0f }; // y(s+1) = y0 + k Delta(s)
    const float weights[4] = { 1.0f / 6.0f, 4.0f / 6.0f, 4.0f / 6.0f, 1.0f / 6.0f };
    std::vector<float> omega0( n ), omega( n ), rate( n ), sum( n );
    for ( int32_t sub = 0; sub < numSub; sub++ ) {
        updateGearboxController( hd, forwardSpeed );
        updateClutchController( hd );
        updateEngineController( hd );
        for ( size_t i = 0; i < n; i++ ) {
            omega0[i] = wheelStates[i].AngularVelocity;
        }
        updateBrakeControllers( hd, omega0, engine.Velocity );
        const float engine0 = engine.Velocity;
        float engineSum = 0.0f;
        std::fill( sum.begin(), sum.end(), 0.0f );
        omega = omega0;
        float engineOmega = engine0;
        for ( int32_t stage = 0; stage < 4; stage++ ) {
            float engineRate = 0.0f;
            driveTrainStage( stageDt[stage], forwardSpeed, omega, engineOmega, rate, engineRate );
            for ( size_t i = 0; i < n; i++ ) {
                sum[i] += weights[stage] * rate[i];
                wheelDriveImpulse[i] += weights[stage] * stageDriveImpulse[i];
            }
            engineSum += weights[stage] * engineRate;
            if ( stage < 3 ) {
                for ( size_t i = 0; i < n; i++ ) {
                    omega[i] = omega0[i] + stageAdvance[stage] * rate[i];
                }
                engineOmega = engine0 + stageAdvance[stage] * engineRate;
            }
        }
        for ( size_t i = 0; i < n; i++ ) {
            wheelStates[i].AngularVelocity = omega0[i] + sum[i];
        }
        engine.Velocity = engine0 + engineSum;
        // Engine part vfunc 0x58 (FUN_008884c0 / FUN_00887f10): running engine
        // speed clamped to [IdleVelocity, MaxVelocity] [CODE].
        if ( engine.State == 1 && ( engine.CtrlFlags & 2u ) == 0 ) {
            engine.Velocity = Clamp( engine.Velocity, params.IdleVelocity, params.EngineMaxVelocity );
        } else {
            engine.Velocity = Max( engine.Velocity, 0.0f );
        }
        std::vector<float> omegaNow( n );
        for ( size_t i = 0; i < n; i++ ) {
            omegaNow[i] = wheelStates[i].AngularVelocity;
        }
        updateGearboxFlags( forwardSpeed, omegaNow );
    }
}

// --- One 90 Hz car step ----------------------------------------------------------
void VehicleDynamics::tick( float h, const Input &input, const VehicleWorldCollision &collision ) {
    const Vector3f up = Rotate( orientation, Vector3f( 0.0f, 1.0f, 0.0f ) );
    const Vector3f forward = Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f com = position + Rotate( orientation, centerOfMass );
    const float forwardSpeed = velocity.dot( forward ); // vehicle vfunc 0xcc [ENC: INF forward speed]
    const float absSpeed = std::abs( forwardSpeed );
    const size_t n = wheels.size();

    applyControls( input, forwardSpeed );
    // WorldConfig update (FUN_0087ebf0): aids SlipRatios = the three aid
    // curves at |speed| [CODE].
    aidSlipRatios[0] = params.AbsSlipRatio.sample( absSpeed );
    aidSlipRatios[1] = params.HandBrakeSlipRatio2.sample( absSpeed );
    aidSlipRatios[2] = params.TractionSlipRatio.sample( absSpeed );

    // Suspension rays and spring law [INF: the WheelSpringJoint / contact
    // constraint step (FUN_008720e0, wheel vfuncs 0x28 / 0x2c) works with ENC
    // vector helpers and the ENC suspension vfunc 0x24].
    int32_t numContacts = 0;
    std::vector<float> offsets( n, 0.0f );
    std::vector<VehicleWorldCollision::Hit> hits( n );
    std::vector<bool> contacts( n, false );
    for ( size_t i = 0; i < n; i++ ) {
        const WheelDesc &wheel = wheels[i];
        const SuspensionSetup &s = wheel.bFront ? params.SuspFront : params.SuspRear;
        const float travel = s.UpperStop - s.LowerStop;
        const Vector3f start = position + Rotate( orientation, rayStarts[i] );
        VehicleWorldCollision::Hit hit;
        if ( collision.raycast( start, -up, travel + wheel.Radius, hit ) ) {
            contacts[i] = true;
            offsets[i] = s.UpperStop - ( hit.Distance - wheel.Radius );
            hits[i] = hit;
            numContacts++;
        }
    }
    // Stabilizer bars (StabilizerBarStiffness / Damping, N/m) [CODE values, INF law].
    std::vector<float> antiRoll( n, 0.0f );
    for ( size_t i = 0; i < n; i++ ) {
        for ( size_t j = i + 1; j < n; j++ ) {
            if ( wheels[i].bFront == wheels[j].bFront && wheels[i].bLeft != wheels[j].bLeft && contacts[i] && contacts[j] ) {
                const SuspensionSetup &s = wheels[i].bFront ? params.SuspFront : params.SuspRear;
                const float xi = offsets[i], xj = offsets[j];
                const float vi = ( xi - wheelStates[i].Compression ) / h, vj = ( xj - wheelStates[j].Compression ) / h;
                const float delta = ( xi - xj ) * s.StabilizerBarStiffness + ( vi - vj ) * s.StabilizerBarDamping;
                antiRoll[i] += delta;
                antiRoll[j] -= delta;
            }
        }
    }
    Vector3f force( 0.0f, -kGravity * mass, 0.0f );
    Vector3f torque;
    std::vector<float> omegaStart( n );
    for ( size_t i = 0; i < n; i++ ) {
        const WheelDesc &wheel = wheels[i];
        WheelState &state = wheelStates[i];
        const SuspensionSetup &s = wheel.bFront ? params.SuspFront : params.SuspRear;
        const TyreSetup &tyre = wheel.bFront ? params.TyreFront : params.TyreRear;
        ContactFrame &frame = contactFrames[i];
        omegaStart[i] = state.AngularVelocity;
        state.bContact = contacts[i];
        state.PreviousCompression = state.Compression;
        if ( !contacts[i] ) {
            state.Compression = s.LowerStop;
            state.Load = 0.0f;
            state.LateralForce = state.LongitudinalForce = state.SlippingCoefficient = 0.0f;
            frame = ContactFrame();
            continue;
        }
        const VehicleWorldCollision::Hit &hit = hits[i];
        const float x = offsets[i];
        const float compressionRate = ( x - state.Compression ) / h;
        state.Compression = Min( x, s.UpperStop );
        // Spring k (x - LowerStop) (1 + bias (x - LowerStop)), damper bump /
        // rebound (DampingArray[0] / [1]) [CODE values, INF law].
        const float springX = Min( x, s.UpperStop ) - s.LowerStop;
        float suspension = s.StiffnessAtRest * springX * ( 1.0f + s.StiffnessBias * springX ) +
                           ( compressionRate > 0.0f ? s.DampingBump : s.DampingRebound ) * compressionRate + antiRoll[i];
        if ( x > s.UpperStop ) {
            suspension += Min( s.StiffnessAtRest * kBumpStopRate * ( x - s.UpperStop ), 2.0f * mass * kGravity ); // [INF]
        }
        suspension = Max( suspension, 0.0f );
        // Tyre NormalForce clamped to 1e5 N (FUN_008739c0) [CODE].
        state.Load = Min( suspension, kMaxNormalForce );
        state.ContactPoint = hit.Position;
        state.ContactNormal = hit.Normal;
        state.SurfaceFriction = hit.Friction;
        state.Surface = SurfaceIndex( hit.Surface );
        const Vector3f arm = hit.Position - com;
        const Vector3f pointVelocity = velocity + angularVelocity.cross( arm );
        const float angle = wheel.bFront ? steerAngle : 0.0f;
        const Vector3f wheelForward = Rotate( orientation, Vector3f( std::sin( angle ), 0.0f, std::cos( angle ) ) );
        frame.Longitudinal = ( wheelForward - hit.Normal * wheelForward.dot( hit.Normal ) ).normalized();
        frame.Lateral = hit.Normal.cross( frame.Longitudinal ); // towards +x (left) of the wheel
        frame.Point = hit.Position;
        frame.LongVelocity = pointVelocity.dot( frame.Longitudinal );
        frame.LatVelocity = pointVelocity.dot( frame.Lateral );
        // Dynamic FrictionScale (FUN_0086a990) [CODE]: grass with a contact
        // normal y <= 0.8 -> 0.2; braking (BrakingPosition != 0) with a
        // negative slip ratio -> BrakingFrictionScale; wet (tyre flag bit3,
        // weather > 0.33 in FUN_00bacd50: no weather here, dry [INF]) ->
        // WetFrictionScale; otherwise 1.
        if ( hit.Normal.y <= kGrassSlopeNormalY && state.Surface == TyreSetup::Grass ) {
            frame.FrictionScale = kGrassSlopeFriction;
        } else if ( brakes[i].Position != 0.0f && state.SlipRatio < 0.0f ) {
            frame.FrictionScale = tyre.BrakingFrictionScale;
        } else {
            frame.FrictionScale = 1.0f;
        }
        const Vector3f suspensionForce = up * suspension;
        force += suspensionForce;
        torque += arm.cross( suspensionForce );
        // Rolling resistance [INF: Material RollingResistance not read].
        if ( std::abs( frame.LongVelocity ) > 0.05f ) {
            const Vector3f rolling = frame.Longitudinal * ( -Sign( frame.LongVelocity ) * kRollingResistance * state.Load );
            force += rolling;
            torque += arm.cross( rolling );
        }
    }
    bOnGround = numContacts > 0;

    // Off-road context (FUN_0087ab80): 1 when every wheel is on dirt, 2 on
    // grass, 0 without an off-road wheel [CODE; vfunc 0x13c = 4 wheels INF].
    int32_t numGround = 0, numDirt = 0, numGrass = 0;
    for ( size_t i = 0; i < n; i++ ) {
        if ( wheelStates[i].bContact ) {
            numGround++;
            numDirt += wheelStates[i].Surface == TyreSetup::Dirt ? 1 : 0;
            numGrass += wheelStates[i].Surface == TyreSetup::Grass ? 1 : 0;
        }
    }
    if ( numDirt + numGrass == 0 ) {
        surfaceContext = 0;
    } else if ( numDirt == static_cast<int32_t>( n ) ) {
        surfaceContext = 1;
    } else if ( numGrass == static_cast<int32_t>( n ) ) {
        surfaceContext = 2;
    }

    // Drive train (engine, clutch, gearbox, differential, axles with the tyre torque).
    std::fill( wheelDriveImpulse.begin(), wheelDriveImpulse.end(), 0.0f );
    updateDriveTrain( h, forwardSpeed );

    // Tyre forces on the chassis [INF coupling, spec 6.9]: the tyre state at
    // the final spin (lateral force of FUN_00868a60); longitudinal impulse =
    // (drive + brake impulse - own axle inertia x spin change) / R, i.e. the
    // contact transmits what the axle did not keep, limited by the tyre
    // (MaxLongitudinalForce within the friction ellipse); what the contact
    // cannot transmit stays in the wheel spin.
    std::vector<bool> gripping( n, false );
    for ( size_t i = 0; i < n; i++ ) {
        WheelState &state = wheelStates[i];
        if ( !state.bContact ) {
            continue;
        }
        const ContactFrame &frame = contactFrames[i];
        const float radius = wheels[i].Radius;
        tyreTorque( i, state.AngularVelocity, true );
        const TyreSetup &tyre = wheels[i].bFront ? params.TyreFront : params.TyreRear;
        const float dX = tyre.MaxLongitudinalFriction[state.Surface] * state.Load * 0.001f * frame.FrictionScale;
        const float dY = tyre.MaxLateralFriction[state.Surface] * state.Load * 0.001f * frame.FrictionScale;
        const float deltaOmega = state.AngularVelocity - omegaStart[i];
        float longImpulse = ( wheelDriveImpulse[i] - wheelPartInertia[i] * deltaOmega ) / radius;
        const float latShare = dY > 0.0f ? Min( std::abs( state.LateralForce ) / dY, 1.0f ) : 1.0f;
        const float cap = dX * h * std::sqrt( Max( 1.0f - latShare * latShare, 0.0f ) );
        gripping[i] = state.AxleSlipCoefficient < 1e-3f;
        if ( std::abs( longImpulse ) > cap ) {
            const float excess = longImpulse - Sign( longImpulse ) * cap;
            longImpulse -= excess;
            state.AngularVelocity += excess * radius / wheelPartInertia[i];
            gripping[i] = false;
        }
        // Low speed lateral: no more than the impulse stopping the contact
        // point sideways (tangential constraint, TangentialConstraintSolverThreshold ENC) [INF].
        float latImpulse = state.LateralForce * h;
        const float stopLat = std::abs( frame.LatVelocity ) * mass * 0.25f;
        latImpulse = Clamp( latImpulse, -stopLat, stopLat );
        state.LongitudinalForce = longImpulse / h;
        const Vector3f tyreForce = frame.Longitudinal * ( longImpulse / h ) + frame.Lateral * ( latImpulse / h );
        force += tyreForce;
        torque += ( frame.Point - com ).cross( tyreForce );
    }

    // Aerodynamics (FUN_00883b20): spoilers at the axles at the centre of
    // mass height (local anchors (0, 0, FrontAxleLocalPos - COM z) and
    // (0, 0, FrontAxleLocalPos - WheelBase - COM z), FUN_0086c2c0) and the
    // ground effect at the centre of mass, along the body up axis [CODE
    // anchors, INF normals]. Lift = -0.592 C |v_t| |v|, drag = |lift| /
    // LiftDragRatio against v_t (DragCoefficient 1 [INF]).
    const float speedAbs = velocity.length();
    if ( speedAbs > 0.1f ) {
        const Vector3f vt = velocity - up * velocity.dot( up );
        const float vtLength = vt.length();
        const auto spoiler = [&]( float coefficient, const Vector3f &anchorLocal, bool bDrag ) {
            if ( coefficient == 0.0f ) {
                return;
            }
            const float lift = kAeroHalfDensity * coefficient * vtLength * speedAbs;
            Vector3f f = up * lift;
            if ( bDrag && params.LiftDragRatio != 0.0f && vtLength > 1e-3f ) {
                f += vt * ( lift / ( params.LiftDragRatio * vtLength ) );
            }
            const Vector3f p = position + Rotate( orientation, anchorLocal );
            force += f;
            torque += ( p - com ).cross( f );
        };
        const float frontZ = params.FrontAxleLocalPos - ( params.FrontAxleLocalPos - ( 1.0f - params.LoadDistribution ) * params.WheelBase );
        const float rearZ = frontZ - params.WheelBase;
        spoiler( params.SpoilerFrontCoefficient, centerOfMass + Vector3f( 0.0f, 0.0f, frontZ ), true );
        spoiler( params.SpoilerRearCoefficient, centerOfMass + Vector3f( 0.0f, 0.0f, rearZ ), true );
        spoiler( params.GroundEffectCoefficient, centerOfMass, false );
    }
    // Extra gravity while no wheel touches (FUN_0087ebf0): factor = Min
    // before AccelDelay, then linear to Max over AccelDuration [CODE ramp];
    // applied as factor x g [INF: FUN_00548370 ENC].
    if ( !bOnGround ) {
        airTime += h;
        float factor;
        if ( airTime < params.ExtraGravityAccelDelay ) {
            factor = params.ExtraGravityMin;
        } else {
            const float t = airTime - params.ExtraGravityAccelDelay;
            factor = t < params.ExtraGravityAccelDuration
                         ? ( params.ExtraGravityMax - params.ExtraGravityMin ) / params.ExtraGravityAccelDuration * t + params.ExtraGravityMin
                         : params.ExtraGravityMax;
        }
        force.y -= factor * kGravity * mass;
    } else {
        airTime = 0.0f;
    }

    // Integration (semi-implicit Euler, SubStepCount 1) [INF: Motion ENC].
    velocity += force * ( h / mass );
    angularVelocity += worldInverseInertia( torque ) * h;
    // Angular damping: CurveOffRoadAngularDamping when every grounded wheel
    // is off road, else CurveAngularDamping (FUN_0087af20); linear damping =
    // LinearDampingOffset + dirt / grass curve by context (FUN_0087af70)
    // [CODE curves, INF exponential application].
    const int32_t numOffRoad = numDirt + numGrass;
    const float angularDamping =
        ( numGround > 0 && numOffRoad == numGround ) ? params.OffRoadAngularDamping.sample( absSpeed ) : params.AngularDamping.sample( absSpeed );
    angularVelocity *= 1.0f / ( 1.0f + angularDamping * h );
    if ( bOnGround ) {
        float linearDamping = 0.0f;
        if ( surfaceContext == 1 ) {
            linearDamping = params.DirtLinearDamping.sample( absSpeed );
        } else if ( surfaceContext == 2 ) {
            linearDamping = params.GrassLinearDamping.sample( absSpeed );
        }
        velocity *= 1.0f / ( 1.0f + linearDamping * h );
    }
    const Vector3f newCom = com + velocity * h;
    const float angleStep = angularVelocity.length() * h;
    if ( angleStep > 1e-7f ) {
        orientation = Normalize( Quaternionf::FromAxisAngle( angularVelocity.normalized(), angleStep ) * orientation );
    }
    position = newCom - Rotate( orientation, centerOfMass );
    resolveChassisCollision( h, collision );

    // Gripping axles follow the contact (velocity propagation of the parts
    // vfunc 0x54, momentum weighted with the car inertia share) [INF].
    const Vector3f comNew = position + Rotate( orientation, centerOfMass );
    for ( size_t i = 0; i < n; i++ ) {
        WheelState &state = wheelStates[i];
        if ( state.bContact && gripping[i] ) {
            const Vector3f pointVelocity = velocity + angularVelocity.cross( contactFrames[i].Point - comNew );
            const float rolling = pointVelocity.dot( contactFrames[i].Longitudinal ) / wheels[i].Radius;
            const float car = Max( wheelEffInertia[i] - wheelPartInertia[i], 0.0f );
            state.AngularVelocity = ( wheelPartInertia[i] * state.AngularVelocity + car * rolling ) / ( wheelPartInertia[i] + car );
        }
        state.SpinAngle += state.AngularVelocity * h;
        if ( state.SpinAngle > 100.0f || state.SpinAngle < -100.0f ) {
            state.SpinAngle = std::fmod( state.SpinAngle, 2.0f * kPI );
        }
        state.BrakingPosition = brakes[i].Position;
    }
    // Locked chain: the engine follows the carrier (clutch vfunc 0x54) [INF].
    const float ratio = gearRatio( gearbox.Gear );
    if ( ratio != 0.0f && clutch.Dof == 0.0f ) {
        float carrier = 0.0f;
        int32_t numDriven = 0;
        for ( size_t i = 0; i < n; i++ ) {
            if ( drivenWheels[i] ) {
                carrier += wheelStates[i].AngularVelocity;
                numDriven++;
            }
        }
        if ( numDriven > 0 ) {
            engine.Velocity = carrier / numDriven * ratio;
            if ( engine.State == 1 ) {
                engine.Velocity = Clamp( engine.Velocity, params.IdleVelocity, params.EngineMaxVelocity );
            }
        }
    }
    odometer += velocity.length() * h;
}

void VehicleDynamics::resolveChassisCollision( float dt, const VehicleWorldCollision &collision ) {
    for ( int32_t iteration = 0; iteration < 2; iteration++ ) {
        bool bAny = false;
        if ( previousSphereCenters.size() != collisionSpheres.size() ) {
            previousSphereCenters.clear();
            for ( const Vector3f &local : collisionSpheres ) {
                previousSphereCenters.push_back( position + Rotate( orientation, local ) );
            }
        }
        for ( size_t s = 0; s < collisionSpheres.size(); s++ ) {
            const Vector3f &local = collisionSpheres[s];
            const Vector3f center = position + Rotate( orientation, local );
            VehicleWorldCollision::Contact contact;
            if ( !collision.sphereContact( center, collisionRadius, contact, &previousSphereCenters[s] ) ) {
                continue;
            }
            bAny = true;
            if ( bLogContacts ) {
                OTDU_LOG_INFO( "Vehicle contact: sphere (%.2f %.2f %.2f) local (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) depth %.3f "
                               "point (%.2f %.2f %.2f)\n",
                               center.x, center.y, center.z, local.x, local.y, local.z, contact.Normal.x, contact.Normal.y,
                               contact.Normal.z, contact.Depth, contact.Point.x, contact.Point.y, contact.Point.z );
            }
            // Position correction (whole body), then a non elastic impulse
            // with some friction [INF: restitution 0.05, friction 0.3; the car
            // body collision is the Havok chassis rigid body].
            position += contact.Normal * Min( contact.Depth * 0.8f, 0.05f );
            const Vector3f com = position + Rotate( orientation, centerOfMass );
            const Vector3f arm = contact.Point - com;
            const Vector3f pointVelocity = velocity + angularVelocity.cross( arm );
            const float approach = pointVelocity.dot( contact.Normal );
            if ( approach >= 0.0f ) {
                continue;
            }
            const Vector3f armCrossNormal = arm.cross( contact.Normal );
            const float effective = 1.0f / mass + contact.Normal.dot( worldInverseInertia( armCrossNormal ).cross( arm ) );
            const float j = -( 1.0f + 0.05f ) * approach / Max( effective, 1e-6f );
            applyImpulse( contact.Normal * j, contact.Point );
            Vector3f tangent = pointVelocity - contact.Normal * approach;
            const float tangentSpeed = tangent.length();
            if ( tangentSpeed > 1e-3f ) {
                tangent = tangent * ( 1.0f / tangentSpeed );
                const float jt = Min( tangentSpeed / Max( effective, 1e-6f ), 0.3f * j );
                applyImpulse( tangent * -jt, contact.Point );
            }
        }
        if ( !bAny ) {
            break;
        }
    }
    for ( size_t s = 0; s < collisionSpheres.size() && s < previousSphereCenters.size(); s++ ) {
        previousSphereCenters[s] = position + Rotate( orientation, collisionSpheres[s] );
    }
    (void)dt;
}

std::string VehicleDynamics::getDebugState() const {
    char buffer[512];
    std::string wheelText;
    for ( size_t i = 0; i < wheelStates.size(); i++ ) {
        const WheelState &w = wheelStates[i];
        char tmp[96];
        snprintf( tmp, sizeof( tmp ), " [w%zu %.1f s%.3f c%.2f b%.2f%s%s]", i, w.AngularVelocity, w.SlipRatio, w.AxleSlipCoefficient,
                  w.BrakingPosition, w.bAbs ? "A" : "", w.bTcs ? "T" : "" );
        wheelText += tmp;
    }
    snprintf( buffer, sizeof( buffer ), "eng %.1f st%d pos %.3f | clutch st%d pos %.2f dof %.2f | gb g%d sh%d st%d fl%x cfl%x %s%s",
              engine.Velocity, engine.State, engine.Position, clutch.State, clutch.Position, clutch.Dof, gearbox.Gear,
              gearbox.Shifted, gearbox.State, gearbox.Flags, gearbox.CtrlFlags, bSportLines ? "sport" : "normal", wheelText.c_str() );
    return buffer;
}
