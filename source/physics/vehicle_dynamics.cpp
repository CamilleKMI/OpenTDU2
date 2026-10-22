#include "shared.h"
#include "vehicle_dynamics.h"

#include "physics/vehicle_collision.h"

// Tags: [CODE] read in the exe (addresses in docs/re/vehicle_physics_spec.md),
// [INF] chosen here because the edVehicle solver step is not reimplemented.
namespace {
constexpr float kGravity = 9.81f;          // [INF] |DAT_01459f7c + 0x2b0|
constexpr float kAeroHalfDensity = -0.592f; // DAT_00f4adc0 = -1.184 / 2 [CODE FUN_00883b20]
constexpr float kRollingResistance = 0.015f; // Material RollingResistance (value not found) [INF]
constexpr float kMinSlipVelocity = 1.0f;   // m/s, slip denominators at rest [INF]
constexpr float kBumpStopRate = 10.0f;     // x spring rate past the upper stop [INF]
constexpr float kRadToDegD = 57.295780181884766f; // DAT_00f49ab0
constexpr float kPacejkaEps = 9.999999747378752e-06f; // DAT_00ecd6ec
constexpr float kDefaultB = 99999.0f;      // DAT_00f4a6cc (B when C / D / a4 vanish)

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

uint32_t SurfaceIndex( VehicleWorldCollision::Hit::eSurface surface ) {
    // Road / static meshes = tarmac, heightfield terrain = grass [INF: the
    // edVehicle Material of the world is not decoded].
    return surface == VehicleWorldCollision::Hit::eSurface::Terrain ? TyreSetup::Grass : TyreSetup::Tarmac;
}
} // namespace

VehicleDynamics::VehicleDynamics() {}

// Pure-slip Pacejka + combination of one tyre (FUN_00869090, FUN_00868dc0,
// FUN_00868a60) [CODE]. Inputs: normal force (N), wheel frame velocities
// (vx forward, vy lateral), wheel spin (rad/s). Returns the longitudinal
// force, outFy the lateral force (both opposing the contact slip).
float VehicleDynamics::evaluateTyre( size_t wheelIndex, float fz, float vx, float vy, float omega, uint32_t surface,
                                     bool bBraking, float &outFy ) const {
    const TyreSetup &tyre = wheels[wheelIndex].bFront ? params.TyreFront : params.TyreRear;
    const PacejkaConfig &cfg = tyre.Config[surface];
    const float radius = wheels[wheelIndex].Radius;
    // FrictionScale (1.0) x BrakingFrictionScale while braking [INF use of +0x04].
    const float frictionScale = tyre.FrictionScale * ( bBraking ? tyre.BrakingFrictionScale : 1.0f );
    const float fzKn = static_cast<float>( static_cast<double>( fz ) * 0.0010000000474974513 );

    // Slip definitions [INF: the contact code that fills Tire +0x28 / +0x50
    // is in the solver, not read]: slip ratio (wR - v) / |v|, slip angle
    // atan(vy / |vx|), both with a 1 m/s floor.
    const float absVx = Max( std::abs( vx ), kMinSlipVelocity );
    const float contactVx = vx - omega * radius; // ContactLongitudinalVelocity
    const float slipRatio = -contactVx / absVx;
    const float slipAngle = std::atan2( vy, absVx );

    // Lateral (FUN_00869090): C = a0, D = MaxLatFriction x Fz(kN) x scale,
    // BCD = a3 sin(2 atan(Fz / a4)) (1 - |camber| a5) (camber 0), E = a6 Fz + a7,
    // x = slip angle in degrees.
    const float dY = tyre.MaxLateralFriction[surface] * fzKn * frictionScale;
    const float cY = cfg.Lat[0];
    float bY = kDefaultB;
    if ( std::abs( cY ) > kPacejkaEps && std::abs( dY ) > kPacejkaEps && std::abs( cfg.Lat[4] ) > kPacejkaEps ) {
        const float bcd = std::sin( 2.0f * std::atan( fzKn / cfg.Lat[4] ) ) * cfg.Lat[3]; // 0x541080 = atan [INF: ENC]
        bY = bcd / ( dY * cY );
    }
    const float eY = cfg.Lat[6] * fzKn + cfg.Lat[7];
    const float fy0 = MagicFormula( bY, cY, dY, eY, static_cast<float>( slipAngle * kRadToDegD ) );

    // Longitudinal (FUN_00868dc0): C = b0, D = MaxLongFriction x Fz x scale,
    // BCD = (b3 Fz^2 + b4 Fz) exp(-b5 Fz), E = b6 Fz^2 + b7 Fz + b8, x = slip ratio x 100.
    const float dX = tyre.MaxLongitudinalFriction[surface] * fzKn * frictionScale;
    const float cX = cfg.Long[0];
    float bX = kDefaultB;
    if ( std::abs( cX ) > kPacejkaEps && std::abs( dX ) > kPacejkaEps ) {
        const float bcd = ( cfg.Long[3] * fzKn * fzKn + cfg.Long[4] * fzKn ) * std::exp( -cfg.Long[5] * fzKn );
        bX = bcd / ( dX * cX );
    }
    const float eX = cfg.Long[6] * fzKn * fzKn + cfg.Long[7] * fzKn + cfg.Long[8];
    const float fx0 = MagicFormula( bX, cX, dX, eX, static_cast<float>( slipRatio * 100.0 ) );

    // Combination (FUN_00868a60): direction = (muLat Fz (-vy), muLong Fz (-vcx))
    // normalised, magnitude = min(|Fy0 / dy|, |Fx0 / dx|) capped by the
    // ellipse sqrt((MaxLatForce dy)^2 + (MaxLongForce dx)^2).
    const float wy = tyre.MaxLateralFriction[surface] * fzKn * frictionScale * -vy;
    const float wx = tyre.MaxLongitudinalFriction[surface] * fzKn * frictionScale * -contactVx;
    const float norm = std::sqrt( wy * wy + wx * wx );
    if ( norm <= kPacejkaEps ) {
        outFy = 0.0f;
        return 0.0f;
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
    outFy = dirY * magnitude;
    return dirX * magnitude;
}

void VehicleDynamics::setup( const VehicleSetup &newSetup, const std::vector<WheelDesc> &newWheels ) {
    params = newSetup;
    wheels = newWheels;
    wheelStates.assign( wheels.size(), WheelState() );
    mass = Max( params.Mass, 100.0f );
    inertia = params.Inertia;
    for ( WheelDesc &wheel : wheels ) {
        // DBRims radius (FUN_00df3b30 GEO +0x1c / +0x20) [CODE].
        wheel.Radius = ( wheel.bFront ? params.TyreFront.Radius : params.TyreRear.Radius );
    }

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
    // lower stop. Offset x (compression from the modelled hub) in
    // [LowerStop, UpperStop]; spring force k (x - LowerStop) (zero at full
    // droop: k |LowerStop| = the static wheel load for the DB cars, e.g.
    // California 80000 x 0.05 = 4000 N = 1735 g 0.47 / 2) [INF].
    rayStarts.clear();
    staticCompressions.clear();
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        const WheelDesc &wheel = wheels[i];
        const SuspensionSetup &s = wheel.bFront ? params.SuspFront : params.SuspRear;
        rayStarts.push_back( wheel.HubLocal + Vector3f( 0.0f, s.UpperStop, 0.0f ) );
        const float share = wheel.bFront ? params.LoadDistribution : 1.0f - params.LoadDistribution;
        const float load = mass * kGravity * share * 0.5f;
        const float rest = Clamp( load / Max( s.StiffnessAtRest, 1000.0f ) + s.LowerStop, s.LowerStop, s.UpperStop );
        staticCompressions.push_back( rest );
        wheelStates[i].Compression = rest;
        wheelStates[i].PreviousCompression = rest;
    }

    // Chassis spheres: corners of the .dhk chassis boxes (body 0), raised
    // by Physics.cpr CollisionOffset.
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
    const Vector3f forward = Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) );
    const float forwardSpeed = velocity.dot( forward );
    for ( size_t i = 0; i < wheelStates.size(); i++ ) {
        wheelStates[i].AngularVelocity = forwardSpeed / wheels[i].Radius;
        wheelStates[i].SlipRatio = 0.0f;
    }
    // Gear for the speed (automatic box).
    gear = 1;
    const float wheelOmega = std::abs( forwardSpeed ) / ( wheels.empty() ? 0.34f : wheels[0].Radius );
    const float upVelocity = Min( params.PowerRpm * 0.10471975803375244f, params.RedLine - 10.0f );
    while ( gear < static_cast<int32_t>( params.Gears.size() ) && wheelOmega * gearRatio( gear ) * params.FinalDrive > upVelocity * 0.8f ) {
        gear++;
    }
    engineVelocity = Max( params.IdleVelocity, wheelOmega * gearRatio( gear ) * params.FinalDrive );
    engineRpm = engineVelocity * 30.0f / kPI;
    steerAngle = 0.0f;
    shiftTimer = 0.0f;
    limiterTimer = 0.0f;
}

Matrix4x4f VehicleDynamics::getWorldMatrix() const {
    return Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( orientation );
}

float VehicleDynamics::getSpeedKmh() const {
    return velocity.dot( Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) ) ) * 3.6f;
}

// GearRatioArray: [0] = -_gear_ratio1 (reverse), [1] = 0 (neutral), [2..] = 1st.. (FUN_00884220 calls) [CODE].
float VehicleDynamics::gearRatio( int32_t g ) const {
    if ( params.Gears.empty() ) {
        return 1.0f;
    }
    if ( g < 0 ) {
        return -params.ReverseRatio;
    }
    if ( g == 0 ) {
        return 0.0f;
    }
    return params.Gears[Min( static_cast<size_t>( g ), params.Gears.size() ) - 1];
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

void VehicleDynamics::step( float deltaTime, const Input &input, const VehicleWorldCollision &collision ) {
    if ( wheels.empty() || deltaTime <= 0.0f ) {
        return;
    }
    // Physics.cpr NbLoopForMiddlePrecision = 3 [INF: sub steps].
    const int32_t numSteps = Max( params.Substeps, 1 );
    const float dt = deltaTime / static_cast<float>( numSteps );
    for ( int32_t i = 0; i < numSteps; i++ ) {
        substep( dt, input, collision );
    }
    odometer += velocity.length() * deltaTime;
}

// Gearbox controller (+0x2c ShiftTimeDown, +0x30 ShiftTimeUp, +0x34
// ReverseTime) [CODE values]; the automatic shift rule is not read [INF]:
// up at the power peak on the ground speed, down when the lower gear stays
// under 0.85 x the up point at full throttle or under the blue line x 0.6.
void VehicleDynamics::updateGearbox( float dt, const Input &input, float speedKmh ) {
    shiftTimer = Max( shiftTimer - dt, 0.0f );
    const int32_t numGears = static_cast<int32_t>( params.Gears.size() );
    if ( gear >= 0 ) {
        if ( std::abs( speedKmh ) < 2.0f && input.Brake > 0.5f && input.Throttle < 0.1f ) {
            reverseTimer += dt;
            if ( reverseTimer >= params.ReverseTime ) {
                gear = -1;
                reverseTimer = 0.0f;
            }
        } else {
            reverseTimer = 0.0f;
        }
    } else if ( input.Throttle > 0.1f && speedKmh > -2.0f ) {
        gear = 1;
    }
    if ( gear < 1 || shiftTimer > 0.0f ) {
        return;
    }
    const float upVelocity = Min( params.PowerRpm * 0.10471975803375244f, params.RedLine - 10.0f );
    const float radius = wheels.empty() ? 0.34f : wheels[0].Radius;
    const float groundVelocity = std::abs( speedKmh / 3.6f ) / radius * gearRatio( gear ) * params.FinalDrive;
    float drivenSlip = 0.0f;
    int32_t numDrivenWheels = 0;
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        const bool bDriven = wheels[i].bFront ? params.Drive != VehicleSetup::eDrive::Rear : params.Drive != VehicleSetup::eDrive::Front;
        if ( bDriven ) {
            drivenSlip += std::abs( wheelStates[i].SlipRatio );
            numDrivenWheels++;
        }
    }
    drivenSlip = numDrivenWheels > 0 ? drivenSlip / numDrivenWheels : 0.0f;
    const float shiftVelocity = drivenSlip < 0.25f ? Max( groundVelocity, engineVelocity ) : groundVelocity;
    if ( gear < numGears && shiftVelocity > upVelocity && groundVelocity > upVelocity * 0.85f && input.Throttle > 0.05f ) {
        gear++;
        shiftTimer = params.ShiftTimeUp;
    } else if ( gear > 1 ) {
        const float lowerVelocity = groundVelocity * gearRatio( gear - 1 ) / gearRatio( gear );
        const float downVelocity = input.Throttle > 0.8f ? upVelocity * 0.85f : params.BlueLine * 0.6f;
        if ( lowerVelocity < downVelocity ) {
            gear--;
            shiftTimer = params.ShiftTimeDown;
        }
    }
}

void VehicleDynamics::substep( float dt, const Input &rawInput, const VehicleWorldCollision &collision ) {
    const Vector3f up = Rotate( orientation, Vector3f( 0.0f, 1.0f, 0.0f ) );
    const Vector3f forward = Rotate( orientation, Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f com = position + Rotate( orientation, centerOfMass );
    const float speed = velocity.dot( forward ); // m/s along the heading
    const float absSpeed = std::abs( speed );

    updateGearbox( dt, rawInput, speed * 3.6f );
    // In reverse the pedals swap (brake accelerates backwards) [INF].
    Input input = rawInput;
    if ( gear < 0 ) {
        std::swap( input.Throttle, input.Brake );
    }

    // Steering (edVehicle +0x109c: CurveMaxVelocity = SteeringVelocity,
    // CurveMaxAngle, CurveSensitivity; m/s -> rad/s, rad) [CODE curves].
    // Right input turns the wheels towards -x (car right).
    const float maxAngle = params.SteeringMaxAngle.sample( absSpeed );
    const float sensitivity = params.SteeringSensitivity.sample( absSpeed );
    const float target = -Clamp( rawInput.Steering * sensitivity, -1.0f, 1.0f ) * maxAngle;
    const float rate = params.SteeringVelocity.sample( absSpeed ) * dt;
    steerAngle += Clamp( target - steerAngle, -rate, rate );

    Vector3f force( 0.0f, -kGravity * mass, 0.0f );
    Vector3f torque;

    // Engine (edVehicle engine +0x8e0): torque = Tno(w) + position (Tmax(w) - Tno(w)),
    // position = IdlePosition + pedal (MaxPosition - IdlePosition) [CODE curves /
    // IdlePosition; INF law], damping -_engine_damping x w [INF unit],
    // limiter: no drive torque for LimiterTime past the red line [INF].
    const bool bRearDrive = params.Drive != VehicleSetup::eDrive::Front;
    const bool bFrontDrive = params.Drive != VehicleSetup::eDrive::Rear;
    float drivenSpeed = 0.0f;
    int32_t numDriven = 0;
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        if ( ( wheels[i].bFront && bFrontDrive ) || ( !wheels[i].bFront && bRearDrive ) ) {
            drivenSpeed += wheelStates[i].AngularVelocity;
            numDriven++;
        }
    }
    drivenSpeed = numDriven > 0 ? drivenSpeed / numDriven : 0.0f;
    const float ratio = gearRatio( gear ) * params.FinalDrive;
    const float idle = params.IdleVelocity;
    const float wheelSideVelocity = std::abs( drivenSpeed * ratio );
    const bool bClutched = gear != 0 && shiftTimer <= 0.0f;
    // Launch: clutch slipping below idle + 250 rad/s x throttle [INF].
    const float launchVelocity = idle + 250.0f * input.Throttle;
    const float targetVelocity = bClutched ? Max( wheelSideVelocity, wheelSideVelocity < launchVelocity ? launchVelocity : idle )
                                           : idle + input.Throttle * ( params.RedLine - idle );
    const float response = bClutched && wheelSideVelocity >= launchVelocity
                               ? 1.0f
                               : Clamp( dt * 0.12f / Max( params.EngineInertia, 0.01f ), 0.0f, 1.0f ); // [INF]
    engineVelocity += ( targetVelocity - engineVelocity ) * response;
    engineVelocity = Clamp( engineVelocity, idle * 0.8f, params.EngineMaxVelocity );
    engineRpm = engineVelocity * 30.0f / kPI;
    limiterTimer = Max( limiterTimer - dt, 0.0f );
    if ( engineVelocity >= params.RedLine && limiterTimer <= 0.0f ) {
        limiterTimer = params.LimiterTime;
    }
    const float pedal = limiterTimer > 0.0f ? 0.0f : Clamp( input.Throttle, 0.0f, 1.0f );
    const float throttlePosition = params.IdlePosition + pedal * ( params.MaxPosition - params.IdlePosition );
    const float tNo = params.TorqueMin.sample( engineVelocity );
    float engineTorque = tNo + throttlePosition * ( params.TorqueMax.sample( engineVelocity ) - tNo );
    engineTorque -= params.EngineDamping * engineVelocity;
    if ( engineVelocity <= idle * 1.02f && engineTorque < 0.0f ) {
        engineTorque = 0.0f; // the idle controller holds the engine [INF]
    }
    const float wheelDriveTorque = bClutched && numDriven > 0 ? engineTorque * ratio / static_cast<float>( numDriven ) : 0.0f;

    // Brakes: MaxBrakingAction (_brakes_power_front, N m for the car) x pedal,
    // BrakingBalance front share (edVehicle +0xe08 / +0xe0c) [CODE values],
    // half per wheel of the axle [INF].
    const float brakeTotal = params.MaxBrakingAction * Clamp( input.Brake, 0.0f, 1.0f );
    const float balance = Clamp( params.BrakingBalance, 0.0f, 1.0f );

    // Suspension rays.
    int32_t numContacts = 0;
    std::vector<float> offsets( wheels.size(), 0.0f );
    std::vector<VehicleWorldCollision::Hit> hits( wheels.size() );
    std::vector<bool> contacts( wheels.size(), false );
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        const WheelDesc &wheel = wheels[i];
        const SuspensionSetup &s = wheel.bFront ? params.SuspFront : params.SuspRear;
        const float travel = s.UpperStop - s.LowerStop;
        const Vector3f start = position + Rotate( orientation, rayStarts[i] );
        VehicleWorldCollision::Hit hit;
        const float rayLength = travel + wheel.Radius;
        if ( collision.raycast( start, -up, rayLength, hit ) ) {
            contacts[i] = true;
            offsets[i] = s.UpperStop - ( hit.Distance - wheel.Radius ); // compression from the modelled hub
            hits[i] = hit;
            numContacts++;
        }
    }
    // Stabilizer bars (StabilizerBarStiffness / Damping, N/m) [CODE values, INF law].
    std::vector<float> antiRoll( wheels.size(), 0.0f );
    for ( size_t i = 0; i < wheels.size(); i++ ) {
        for ( size_t j = i + 1; j < wheels.size(); j++ ) {
            if ( wheels[i].bFront == wheels[j].bFront && wheels[i].bLeft != wheels[j].bLeft && contacts[i] && contacts[j] ) {
                const SuspensionSetup &s = wheels[i].bFront ? params.SuspFront : params.SuspRear;
                const float xi = offsets[i], xj = offsets[j];
                const float vi = ( xi - wheelStates[i].Compression ) / dt, vj = ( xj - wheelStates[j].Compression ) / dt;
                const float delta = ( xi - xj ) * s.StabilizerBarStiffness + ( vi - vj ) * s.StabilizerBarDamping;
                antiRoll[i] += delta;
                antiRoll[j] -= delta;
            }
        }
    }

    for ( size_t i = 0; i < wheels.size(); i++ ) {
        const WheelDesc &wheel = wheels[i];
        WheelState &state = wheelStates[i];
        const SuspensionSetup &s = wheel.bFront ? params.SuspFront : params.SuspRear;
        const TyreSetup &tyre = wheel.bFront ? params.TyreFront : params.TyreRear;
        const bool bDriven = ( wheel.bFront && bFrontDrive ) || ( !wheel.bFront && bRearDrive );
        // Wheel inertia 1/2 m r^2 [INF]; clutched driven wheels also carry the
        // engine (_engine_inertia x 0.01) and gearbox (_gearbox_inertia x 0.01)
        // inertias through the ratio squared (SqrGearRatioArray) [CODE values, INF rigid coupling].
        float inertiaWheel = 0.5f * tyre.Mass * wheel.Radius * wheel.Radius;
        if ( bDriven && bClutched && numDriven > 0 ) {
            inertiaWheel += ( params.EngineInertia + params.GearboxInertia ) * ratio * ratio / static_cast<float>( numDriven );
        }
        float driveTorque = bDriven ? wheelDriveTorque : 0.0f;
        float brakeTorque = brakeTotal * ( wheel.bFront ? balance : 1.0f - balance ) * 0.5f;
        if ( rawInput.bHandBrake && !wheel.bFront ) {
            brakeTorque += params.MaxBrakingAction * 0.5f * ( 1.0f - balance ); // [INF]
        }
        state.bContact = contacts[i];
        state.bAbs = state.bTcs = false;
        if ( !contacts[i] ) {
            state.PreviousCompression = state.Compression;
            state.Compression = s.LowerStop;
            state.Load = 0.0f;
            state.LateralForce = state.LongitudinalForce = state.SlippingCoefficient = 0.0f;
            state.AngularVelocity += dt * driveTorque / Max( inertiaWheel, 0.1f );
            const float brakeDelta = dt * brakeTorque / Max( inertiaWheel, 0.1f );
            state.AngularVelocity = std::abs( state.AngularVelocity ) <= brakeDelta
                                        ? 0.0f
                                        : state.AngularVelocity - brakeDelta * ( state.AngularVelocity > 0 ? 1.0f : -1.0f );
            state.SpinAngle += state.AngularVelocity * dt;
            continue;
        }
        const VehicleWorldCollision::Hit &hit = hits[i];
        const float x = offsets[i];
        const float compressionRate = ( x - state.Compression ) / dt;
        state.PreviousCompression = state.Compression;
        state.Compression = Min( x, s.UpperStop );
        // Spring k (x - LowerStop) (1 + bias (x - LowerStop)) [INF bias law],
        // damper bump / rebound (DampingArray[0] / [1]).
        const float springX = Min( x, s.UpperStop ) - s.LowerStop;
        float suspension = s.StiffnessAtRest * springX * ( 1.0f + s.StiffnessBias * springX ) +
                           ( compressionRate > 0.0f ? s.DampingBump : s.DampingRebound ) * compressionRate + antiRoll[i];
        if ( x > s.UpperStop ) {
            suspension += Min( s.StiffnessAtRest * kBumpStopRate * ( x - s.UpperStop ), 2.0f * mass * kGravity ); // [INF]
        }
        suspension = Max( suspension, 0.0f );
        state.Load = suspension;
        state.ContactPoint = hit.Position;
        state.ContactNormal = hit.Normal;
        state.SurfaceFriction = hit.Friction;
        const uint32_t surface = SurfaceIndex( hit.Surface );

        const Vector3f point = hit.Position;
        const Vector3f arm = point - com;
        const Vector3f pointVelocity = velocity + angularVelocity.cross( arm );
        const float angle = wheel.bFront ? steerAngle : 0.0f;
        const Vector3f wheelForward = Rotate( orientation, Vector3f( std::sin( angle ), 0.0f, std::cos( angle ) ) );
        const Vector3f longitudinal = ( wheelForward - hit.Normal * wheelForward.dot( hit.Normal ) ).normalized();
        const Vector3f lateral = hit.Normal.cross( longitudinal ); // towards +x (left) of the wheel
        const float vx = pointVelocity.dot( longitudinal );
        const float vy = pointVelocity.dot( lateral );
        const float vEffective = Max( std::abs( vx ), kMinSlipVelocity );
        const float normal = suspension;
        const bool bBraking = brakeTorque > 0.0f;

        // Driving aids (edVehicle aids object): ABS / traction control slip
        // ratio limits vs speed, active above VelocityActivations [CODE
        // curves]; regulation by torque cut on the previous slip [INF].
        if ( params.bAbs && brakeTorque > 0.0f && !rawInput.bHandBrake && absSpeed >= params.AbsActivation &&
             state.SlipRatio < params.AbsSlipRatio.sample( absSpeed ) ) {
            brakeTorque *= 0.2f;
            state.bAbs = true;
        }
        if ( params.bTractionControl && driveTorque > 0.0f && absSpeed >= params.TractionActivation ) {
            const float limit = params.TractionSlipRatio.sample( absSpeed );
            if ( limit > 0.0f && state.SlipRatio > limit ) {
                driveTorque *= Clamp( limit / Max( state.SlipRatio, 1e-3f ), 0.1f, 1.0f );
                state.bTcs = true;
            }
        }
        if ( rawInput.bHandBrake && !wheel.bFront && absSpeed >= params.HandBrakeActivation &&
             state.SlipRatio < params.HandBrakeSlipRatio2.sample( absSpeed ) ) {
            brakeTorque *= 0.5f; // hand brake slip ratio limit [INF regulation]
        }

        // Wheel spin, implicit in the tyre slope (numerical dFx / domega).
        float fy = 0.0f, fyProbe = 0.0f;
        const float omega0 = state.AngularVelocity;
        const float fx0 = evaluateTyre( i, normal, vx, vy, omega0, surface, bBraking, fy );
        const float probe = 0.01f * vEffective / wheel.Radius + 1e-3f;
        const float fxProbe = evaluateTyre( i, normal, vx, vy, omega0 + probe, surface, bBraking, fyProbe );
        const float slope = Max( ( fxProbe - fx0 ) / probe, 0.0f ); // dFx / domega
        const float denominator = inertiaWheel + dt * wheel.Radius * slope;
        float omega = omega0 + dt * ( driveTorque - wheel.Radius * fx0 ) / denominator;
        const float rolling = kRollingResistance * normal * wheel.Radius;
        const float brakeDelta = dt * ( brakeTorque + rolling ) / denominator;
        if ( std::abs( omega ) <= brakeDelta ) {
            omega = 0.0f;
        } else {
            omega -= brakeDelta * ( omega > 0.0f ? 1.0f : -1.0f );
        }
        state.AngularVelocity = omega;
        state.SlipRatio = ( omega * wheel.Radius - vx ) / vEffective;
        state.SlipAngle = std::atan2( vy, vEffective );
        const float fx = evaluateTyre( i, normal, vx, vy, omega, surface, bBraking, fy );
        state.LateralForce = fy;
        state.LongitudinalForce = fx;
        {
            const float dY = tyre.MaxLateralFriction[surface] * normal * 0.001f, dX = tyre.MaxLongitudinalFriction[surface] * normal * 0.001f;
            const float e = std::sqrt( fx * fx / Max( dX * dX, 1e-6f ) + fy * fy / Max( dY * dY, 1e-6f ) );
            state.SlippingCoefficient = e;
        }
        state.SpinAngle += omega * dt;

        const Vector3f tyreForce = longitudinal * fx + lateral * fy;
        const Vector3f suspensionForce = up * suspension;
        force += tyreForce + suspensionForce;
        torque += arm.cross( tyreForce ) + arm.cross( suspensionForce );
    }
    bOnGround = numContacts > 0;

    // Aerodynamics: front / rear spoilers at the axles and the ground effect
    // at the centre of mass, along the body up axis (anchors / normals of
    // the spoiler objects are constructor values, not read [INF]).
    // Lift = -0.592 C |v_t| |v| (v_t = v without its normal component),
    // drag = |lift| / LiftDragRatio against v_t (DragCoefficient 1 [INF]) [CODE FUN_00883b20].
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
        float zFront = params.FrontAxleLocalPos, zRear = params.FrontAxleLocalPos - params.WheelBase;
        for ( const WheelDesc &wheel : wheels ) {
            ( wheel.bFront ? zFront : zRear ) = wheel.HubLocal.z;
        }
        spoiler( params.SpoilerFrontCoefficient, Vector3f( 0.0f, centerOfMass.y, zFront ), true );
        spoiler( params.SpoilerRearCoefficient, Vector3f( 0.0f, centerOfMass.y, zRear ), true );
        spoiler( params.GroundEffectCoefficient, centerOfMass, false );
    }
    // Extra gravity in the air (Physics.cpr ExtraGravityMin / Max /
    // AccelDuration / AccelDelay, in g; WorldConfig +0x14..+0x20) [INF law].
    if ( !bOnGround ) {
        airTime += dt;
        if ( airTime > params.ExtraGravityAccelDelay ) {
            const float t = Clamp( ( airTime - params.ExtraGravityAccelDelay ) / Max( params.ExtraGravityAccelDuration, 1e-3f ), 0.0f, 1.0f );
            const float extra = params.ExtraGravityMin + ( params.ExtraGravityMax - params.ExtraGravityMin ) * t;
            force.y -= extra * kGravity * mass;
        }
    } else {
        airTime = 0.0f;
    }

    // Integration (semi-implicit Euler).
    velocity += force * ( dt / mass );
    angularVelocity += worldInverseInertia( torque ) * dt;
    // CurveAngularDamping (SecureAngularDamping at build, m/s -> 1/s) [CODE curve, INF law].
    const float damping = params.AngularDamping.sample( absSpeed );
    angularVelocity *= 1.0f / ( 1.0f + damping * dt );
    // Off-road linear damping on grass (heightfield) [INF surface mapping].
    if ( bOnGround ) {
        bool bGrass = false;
        for ( size_t i = 0; i < wheels.size(); i++ ) {
            bGrass |= contacts[i] && SurfaceIndex( hits[i].Surface ) == TyreSetup::Grass;
        }
        if ( bGrass ) {
            velocity *= 1.0f / ( 1.0f + params.GrassLinearDamping.sample( absSpeed ) * dt );
        }
    }
    const Vector3f newCom = com + velocity * dt;
    const float angleStep = angularVelocity.length() * dt;
    if ( angleStep > 1e-7f ) {
        orientation = Normalize( Quaternionf::FromAxisAngle( angularVelocity.normalized(), angleStep ) * orientation );
    }
    position = newCom - Rotate( orientation, centerOfMass );
    resolveChassisCollision( dt, collision );
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
            // with some friction [INF: restitution 0.1, friction 0.3].
            // Bounded correction (deep contacts are resolved over a few steps).
            position += contact.Normal * Min( contact.Depth * 0.8f, 0.05f );
            const Vector3f com = position + Rotate( orientation, centerOfMass );
            const Vector3f arm = contact.Point - com;
            const Vector3f pointVelocity = velocity + angularVelocity.cross( arm );
            const float approach = pointVelocity.dot( contact.Normal );
            if ( approach >= 0.0f ) {
                continue;
            }
            const Vector3f armCrossNormal = arm.cross( contact.Normal );
            const float effective =
                1.0f / mass + contact.Normal.dot( worldInverseInertia( armCrossNormal ).cross( arm ) );
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
