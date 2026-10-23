#include "shared.h"
#include "vehicle_data.h"

#include "config/gs_config.h"
#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "physics/havok_collision.h"
#include "system/cmd_line.h"

float VehicleCurve::sample( float x ) const {
    if ( Points.empty() ) {
        return 0.0f;
    }
    if ( x <= Points.front().first ) {
        return Points.front().second;
    }
    for ( size_t i = 1; i < Points.size(); i++ ) {
        if ( x <= Points[i].first ) {
            const float x0 = Points[i - 1].first, x1 = Points[i].first;
            const float t = x1 > x0 ? ( x - x0 ) / ( x1 - x0 ) : 0.0f;
            return Points[i - 1].second + ( Points[i].second - Points[i - 1].second ) * t;
        }
    }
    return Points.back().second;
}

float VehicleCurve::maxY() const {
    float value = Points.empty() ? 0.0f : Points.front().second;
    for ( const auto &point : Points ) {
        value = Max( value, point.second );
    }
    return value;
}

float VehicleCurve::minY() const {
    float value = Points.empty() ? 0.0f : Points.front().second;
    for ( const auto &point : Points ) {
        value = Min( value, point.second );
    }
    return value;
}

namespace {
constexpr float kKmhToMs = 0.2777777910232544f;   // DAT_00f4a010
constexpr float kRpmToRads = 0.10471975803375244f; // DAT_00f4a858
constexpr float kDegToRadF = 0.01745329238474369f; // DAT_00f49d30
constexpr float kAirDensity = 1.184000015258789f;  // DAT_00f4a578
constexpr float kGravityG = 9.81f;                 // |world gravity| (DAT_01459f7c + 0x2b0 [INF value])

// Physics.cpr "key = x0 y0 x1 y1 ..." (5 pairs, loader FUN_00d67xxx) with the
// factory scales (x: km/h -> m/s, y: yScale).
VehicleCurve ReadIniCurve( const IniFile &ini, const char *pKey, const VehicleCurve &fallback, float yScale ) {
    const IniFile::Entry *pEntry = ini.find( "Car", pKey );
    if ( pEntry == nullptr || pEntry->Values.size() < 2 ) {
        return fallback;
    }
    VehicleCurve curve;
    for ( size_t i = 0; i + 1 < pEntry->Values.size(); i += 2 ) {
        curve.Points.emplace_back( static_cast<float>( atof( pEntry->Values[i].c_str() ) ) * kKmhToMs,
                                   static_cast<float>( atof( pEntry->Values[i + 1].c_str() ) ) * yScale );
    }
    return curve;
}

// <car>_max.xmb: root { Curve { NbPoints; PointArray { Vector2 float[2] (one
// field per point, all named "Vector2"); Abscissa[2]; MaxPoint[2] } }
// (edVehicle Curve reader FUN_00892800) [DATA+CODE]; x rpm -> rad/s
// (x DAT_00f4a858, FUN_00bdb970).
VehicleCurve ReadXmbCurve( const Bank &bank, const std::string &fileName ) {
    VehicleCurve curve;
    const BankEntry *pEntry = bank.findEntry( fileName.c_str() );
    if ( pEntry == nullptr ) {
        return curve;
    }
    XmbDocument document;
    if ( !document.load( pEntry->pData, pEntry->Size ) ) {
        return curve;
    }
    XmbValue points = document.getRoot()["Curve"]["PointArray"];
    const XmbType *pType = points.getType();
    if ( pType == nullptr ) {
        return curve;
    }
    for ( const XmbType::Field &field : pType->Fields ) {
        if ( !field.bArray || field.Name != "Vector2" ) {
            continue;
        }
        const uint32_t count = document.readU32( points.getOffset() + field.Offset );
        const uint32_t offset = document.readU32( points.getOffset() + field.Offset + 4 );
        if ( count < 2 ) {
            continue;
        }
        const XmbValue x( &document, field.TypeIndex, offset );
        const uint32_t elementSize = document.getType( field.TypeIndex ).Size;
        const XmbValue y( &document, field.TypeIndex, offset + elementSize );
        curve.Points.emplace_back( x.asFloat() * kRpmToRads, y.asFloat() );
    }
    std::sort( curve.Points.begin(), curve.Points.end() );
    return curve;
}

// Vehicules/Tires/Tires.bnk (group 11 sub 3 entries, loaded by FUN_00be0010):
// root { PacejkaConfig { Name; LateralCoefficientArray float[15];
// LongitudinalCoefficientArray float[11]; AligningMomentCoefficientArray
// float[18]; OptimalSlipRatio; OptimalSlipAngle } } [DATA].
void ReadTyreConfigs( std::vector<PacejkaConfig> &out ) {
    Bank bank;
    if ( !bank.load( ( gpFile->getBankFolder() + "vehicules/tires/tires.bnk" ).c_str() ) ) {
        OTDU_LOG_WARN( "Vehicle: vehicules/tires/tires.bnk not found (built-in Default tyre)\n" );
        return;
    }
    for ( const BankEntry *pEntry : bank.findEntriesByExtension( "xmb" ) ) {
        XmbDocument document;
        if ( !document.load( pEntry->pData, pEntry->Size ) ) {
            continue;
        }
        XmbValue config = document.getRoot()["PacejkaConfig"];
        PacejkaConfig tyre;
        tyre.Name = config["Name"].asString();
        const auto readArray = [&]( const char *pName, float *pDst, uint32_t capacity ) {
            const uint32_t n = Min( config.getArraySize( pName ), capacity );
            for ( uint32_t i = 0; i < n; i++ ) {
                pDst[i] = config.getArrayElement( pName, i ).asFloat( pDst[i] );
            }
        };
        readArray( "LateralCoefficientArray", tyre.Lat, 15 );
        readArray( "LongitudinalCoefficientArray", tyre.Long, 11 );
        readArray( "AligningMomentCoefficientArray", tyre.Align, 18 );
        tyre.OptimalSlipRatio = config["OptimalSlipRatio"].asFloat( tyre.OptimalSlipRatio );
        tyre.OptimalSlipAngle = config["OptimalSlipAngle"].asFloat( tyre.OptimalSlipAngle );
        out.push_back( tyre );
    }
}

// FUN_0088a920: by name, else "Default" (the built-in FUN_008684b0 entry).
PacejkaConfig FindTyreConfig( const std::vector<PacejkaConfig> &configs, const std::string &name ) {
    for ( const PacejkaConfig &config : configs ) {
        if ( config.Name == name ) {
            return config;
        }
    }
    return PacejkaConfig();
}

// <car>.dhk (docs/re/havok_collision_spec.md 1): after the Eden entries,
// { u32 0; u32 1; 0xcd x8; u32 numBodies; 0xcd x4; 0 x8 } then numBodies
// records of 0x80 bytes: rotation 3 x vec4, centre of mass vec4, inertia
// 3 x vec4, f32 mass, u32 motion type [DATA]. Havok rigid body of the
// collision only (the driving model is edVehicle).
bool ReadDhkChassis( const BankEntry &entry, VehicleSetup &out ) {
    const uint8_t *p = entry.pData;
    if ( entry.Size < 0x40 || p[0] != 2 || p[1] != 2 ) {
        return false;
    }
    uint16_t numEntries = 0;
    memcpy( &numEntries, p + 2, 2 );
    uint32_t extraSize = 0;
    memcpy( &extraSize, p + 8, 4 );
    const size_t blockStart = 0x20 + static_cast<size_t>( numEntries ) * 0x20;
    if ( extraSize == 0 || blockStart + 0x20 + 0x80 > entry.Size ) {
        return false;
    }
    uint32_t numBodies = 0;
    memcpy( &numBodies, p + blockStart + 0x10, 4 );
    if ( numBodies == 0 || numBodies > 64 ) {
        return false;
    }
    float values[32];
    memcpy( values, p + blockStart + 0x20, sizeof( values ) );
    out.HavokCenterOfMass = Vector3f( values[12], values[13], values[14] );
    out.HavokInertia = Vector3f( values[16], values[21], values[26] );
    out.HavokMass = values[28];
    return out.HavokMass > 0.0f;
}
} // namespace

bool LoadVehicleSetup( const std::string &carFileName, VehicleSetup &out ) {
    out = VehicleSetup();
    if ( gpDatabase == nullptr || !gpDatabase->findCarPhysics( carFileName.c_str(), out.Db ) ) {
        OTDU_LOG_WARN( "Vehicle: no DBCarPhysicsData for '%s'\n", carFileName.c_str() );
        return false;
    }
    std::vector<PacejkaConfig> tyreConfigs;
    ReadTyreConfigs( tyreConfigs );

    // Physics.cpr [Car] block.
    const IniFile *pIni = gpConfig != nullptr ? &gpConfig->getPhysicsConfig() : nullptr;
    const auto cprFloat = [&]( const char *pKey, float fallback, uint32_t column = 0 ) {
        return pIni != nullptr ? pIni->getFloat( "Car", pKey, fallback, nullptr, column ) : fallback;
    };

    XmbValue root = gpDatabase->getDocument().getRoot();
    XmbValue car;
    bool bFound = false;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( car["_car_file_name"].asString().c_str(), carFileName.c_str() ) == 0 ) {
            bFound = true;
            break;
        }
    }
    if ( !bFound ) {
        return false;
    }
    const auto dbF = [&]( const char *pName, float fallback ) { return car[pName].asFloat( fallback ); };
    const auto dbI = [&]( const char *pName, int64_t fallback ) {
        return static_cast<float>( car[pName].asInt( fallback ) );
    };

    const int32_t drive = static_cast<int32_t>( car["_drive"].asInt( 4 ) );
    out.Drive = drive == 1 ? VehicleSetup::eDrive::AllWheels : ( drive == 3 ? VehicleSetup::eDrive::Front : VehicleSetup::eDrive::Rear );
    out.CameraId = static_cast<int32_t>( car["_camera"].asInt( out.Db.CarId ) );
    out.Accel0To100 = dbF( "_acceleration_0_100_kmh", 0.0f );
    out.QuarterMile = dbF( "_quartermile_sec", 0.0f );
    out.MaxTheoricalSpeed = dbF( "_max_theorical_speed", 0.0f );
    out.Braking100To0 = dbF( "_braking_100_0", 0.0f );
    out.BrakingDistance = dbF( "_brakingdist", 0.0f );

    // --- Geometry block FUN_00df3b30 [CODE] ---------------------------------
    const float length = dbI( "_length", 4500 ) / 1000.0f;
    const float rideHeightFront = dbI( "_ride_height_front", 110 ) / 1000.0f;
    const float rideHeightRear = dbI( "_ride_height_rear", 110 ) / 1000.0f;
    const float rideHeightMaxFront = dbI( "_ride_height_max_front", 60 ) / 1000.0f;
    const float rideHeightMaxRear = dbI( "_ride_height_max_rear", 60 ) / 1000.0f;
    const float suspLengthFront = dbI( "_suspension_length_front", 100 ) / 1000.0f;
    const float suspLengthRear = dbI( "_suspension_lenght_rear", 100 ) / 1000.0f;
    out.Size = Vector3f( dbI( "_width", 1900 ) / 1000.0f, dbI( "_height", 1300 ) / 1000.0f - rideHeightFront - 0.1f, length );
    out.WheelBase = dbI( "_wheelbase", 2600 ) * 0.001f;
    const float overhangFront = dbI( "_overhang_front", 0 );
    const float overhangRear = dbI( "_overhang_rear", 0 );
    if ( overhangFront != 0.0f ) {
        out.FrontAxleLocalPos = length * 0.5f - overhangFront / 1000.0f;
    } else if ( overhangRear == 0.0f ) {
        out.FrontAxleLocalPos = length * 0.5f - ( length * 1000.0f - out.WheelBase * 1000.0f ) / 2000.0f;
    } else {
        out.FrontAxleLocalPos = length * 0.5f - ( length * 1000.0f - out.WheelBase * 1000.0f - overhangRear ) / 1000.0f;
    }
    out.TrackFront = dbI( "_track_front", 1600 ) / 1000.0f;
    out.TrackRear = dbI( "_track_back", 1600 ) / 1000.0f;
    out.LoadDistribution = dbI( "_gravity_center_length", 50 ) / 100.0f;
    out.CgHeight = dbI( "_gravity_center_height", 30 ) / 100.0f;
    out.Mass = Max( dbI( "_mass", 1500 ), 100.0f );
    out.MaxVelocity = out.MaxTheoricalSpeed * kKmhToMs;
    out.ToeFront = dbF( "_toe_front", 0.0f );
    out.ToeRear = dbF( "_toe_rear", 0.0f );
    // Solid box of Size [INF: the edVehicle inertia from (mass, size) is vfunc 0x9c = 0x544400, ENC].
    {
        const float w = out.Size.x, h = out.Size.y, l = out.Size.z, m = out.Mass;
        out.Inertia = Vector3f( m / 12.0f * ( h * h + l * l ), m / 12.0f * ( w * w + l * l ), m / 12.0f * ( w * w + h * h ) );
    }

    // Suspensions (GEO +0x44 .. +0x80).
    const auto setupSuspension = [&]( SuspensionSetup &s, float rh, float rhMax, float length, const char *pRate,
                                      const char *pBias, const char *pDamper, const char *pAntiRoll, const char *pAntiRollDamping ) {
        const float travelUp = Max( rh - rhMax, 0.0f );
        const float lower = Min( travelUp - length, 0.0f );
        s.LowerStop = lower;
        s.UpperStop = travelUp;
        s.StiffnessAtRest = dbF( pRate, 80.0f ) * 1000.0f;
        s.StiffnessBias = dbF( pBias, 0.0f ) * 0.01f;
        s.DampingBump = s.DampingRebound = dbF( pDamper, 8.0f ) * 1000.0f;
        s.StabilizerBarStiffness = dbI( pAntiRoll, 0 ) * 1000.0f;
        s.StabilizerBarDamping = dbI( pAntiRollDamping, 0 ) * 1000.0f;
    };
    setupSuspension( out.SuspFront, rideHeightFront, rideHeightMaxFront, suspLengthFront, "_suspension_rate_front",
                     "_suspension_rate_front_bias", "_damper_rate_front", "_antirollbar_front", "_antirollbar_damping_front" );
    setupSuspension( out.SuspRear, rideHeightRear, rideHeightMaxRear, suspLengthRear, "_suspension_rate_rear",
                     "_suspension_rate_rear_bias", "_damper_rate_rear", "_antirollbar_rear", "_antirollbar_damping_rear" );

    // Tyres: DBRims _default_rims -> radius = largeur x hauteur% + diameter x 2.54 / 2 (front values on both axles) [CODE].
    float radius = out.Db.TireRadius > 0.0f ? out.Db.TireRadius : 0.33f, widthFront = 0.24f, widthRear = 0.24f;
    {
        const uint64_t rimHash = car["_default_rims"].asUInt64( 0 );
        for ( uint32_t i = 0; i < root.getArraySize( "DBRims" ); i++ ) {
            XmbValue rim = root.getArrayElement( "DBRims", i );
            if ( rim["_hashcode"].asUInt64( ~0ull ) != rimHash ) {
                continue;
            }
            const float largeurFront = rim["_largeur_front"].asFloat( 245.0f );
            radius = largeurFront * rim["_hauteur_front"].asFloat( 40.0f ) / 100.0f / 1000.0f +
                     rim["_diameter_front"].asFloat( 18.0f ) * 2.54f * 0.5f / 100.0f;
            widthFront = largeurFront / 1000.0f;
            widthRear = rim["_largeur_rear"].asFloat( largeurFront ) / 1000.0f;
            break;
        }
    }
    const PacejkaConfig tarmac = FindTyreConfig( tyreConfigs, car["_tires_type"].asString() );
    const PacejkaConfig dirt = FindTyreConfig( tyreConfigs, car["_offroad_dirt_tires_type"].asString() );
    const PacejkaConfig grass = FindTyreConfig( tyreConfigs, car["_offroad_grass_tires_type"].asString() );
    const auto setupTyre = [&]( TyreSetup &tyre, bool bFront ) {
        const float lat = dbF( bFront ? "_max_lateral_friction_front" : "_max_lateral_friction_rear", 1.0f );
        const float lon = dbF( bFront ? "_max_longitudinal_friction_front" : "_max_longitudinal_friction_rear", 1.0f );
        const float nor = dbF( bFront ? "_max_normal_friction_front" : "_max_normal_friction_rear", 1.0f );
        tyre.MaxLateralFriction[TyreSetup::Tarmac] = lat * 1000.0f;
        tyre.MaxLongitudinalFriction[TyreSetup::Tarmac] = lon * 1000.0f;
        tyre.MaxNormalFriction[TyreSetup::Tarmac] = nor * 1000.0f;
        tyre.MaxLateralFriction[TyreSetup::Dirt] = lat * dbF( "_offroad_dirt_lateral_friction_percent", 100.0f ) / 100.0f * 1000.0f;
        tyre.MaxLongitudinalFriction[TyreSetup::Dirt] =
            lon * dbF( "_offroad_dirt_longitudinal_friction_percent", 100.0f ) / 100.0f * 1000.0f;
        tyre.MaxNormalFriction[TyreSetup::Dirt] = nor * dbF( "_offroad_dirt_normal_friction_percent", 100.0f ) / 100.0f * 1000.0f;
        tyre.MaxLateralFriction[TyreSetup::Grass] = lat * dbF( "_offroad_grass_lateral_friction_percent", 100.0f ) / 100.0f * 1000.0f;
        tyre.MaxLongitudinalFriction[TyreSetup::Grass] =
            lon * dbF( "_offroad_grass_longitudinal_friction_percent", 100.0f ) / 100.0f * 1000.0f;
        tyre.MaxNormalFriction[TyreSetup::Grass] = nor * dbF( "_offroad_grass_normal_friction_percent", 100.0f ) / 100.0f * 1000.0f;
        tyre.WetFrictionScale = dbF( "_wet_friction_percent", 80.0f ) / 100.0f;
        tyre.BrakingFrictionScale = dbF( bFront ? "_braking_friction_percent_front" : "_braking_friction_percent_rear", 100.0f ) / 100.0f;
        tyre.FrictionScale = 1.0f;
        tyre.Radius = radius;
        tyre.Width = bFront ? widthFront : widthRear;
        tyre.Mass = dbI( bFront ? "_wheel_mass_front" : "_wheel_mass_rear", 20 );
        tyre.Config[TyreSetup::Tarmac] = tarmac;
        tyre.Config[TyreSetup::Dirt] = dirt;
        tyre.Config[TyreSetup::Grass] = grass;
    };
    setupTyre( out.TyreFront, true );
    setupTyre( out.TyreRear, false );

    // Engine.
    out.EngineInertia = dbI( "_engine_inertia", 20 ) * 0.01f;
    out.EngineDamping = dbF( "_engine_damping", 0.05f );
    out.IdleVelocity = dbI( "_rpm_min", 900 ) * kRpmToRads;
    out.BlueLine = dbI( "_blue_line", 6000 ) * kRpmToRads;
    out.RedLine = dbI( "_red_line", 7000 ) * kRpmToRads;
    out.EngineMaxVelocity = dbI( "_rpm_max", 7500 ) * kRpmToRads;
    out.LimiterTime = dbF( "_rupteur_time", 0.09f );
    out.StartingTime = dbF( "_time_ignition", 0.65f );
    out.PowerRpm = dbI( "_power_rpm", 6000 );
    // Game car object (FUN_00baddd0): +0x44/+0x48 (normal / sport lines),
    // +0x58/+0x5c red lines, x 0.10471976; throttle ratios x 0.01 [CODE].
    out.BlueLineSport = dbI( "_blue_line_sport", dbI( "_blue_line", 6000 ) ) * kRpmToRads;
    out.RedLineSport = dbI( "_red_line_sport", dbI( "_red_line", 7000 ) ) * kRpmToRads;
    out.ThrottleRatioNormal = dbI( "_throttle_ratio_normal", 50 ) * 0.01f;
    out.ThrottleRatioSport = dbI( "_throttle_ratio_sport", 90 ) * 0.01f;

    // Gearbox and differential.
    out.Gears.clear();
    const int32_t numGears = Clamp( static_cast<int32_t>( car["_nb_gears"].asInt( 6 ) ), 1, 8 );
    const char *gearNames[] = { "_gear_ratio1", "_gear_ratio2", "_gear_ratio3", "_gear_ratio4",
                                "_gear_ratio5", "_gear_ratio6", "_gear_ratio7", "_gear_ratio8" };
    for ( int32_t i = 0; i < numGears; i++ ) {
        const float ratio = dbF( gearNames[i], 0.0f );
        if ( ratio > 0.0f ) {
            out.Gears.push_back( ratio );
        }
    }
    if ( out.Gears.empty() ) {
        out.Gears = out.Db.GearRatios;
    }
    out.ReverseRatio = out.Gears.empty() ? 3.0f : out.Gears[0];
    out.FinalDrive = dbF( "_final_drive_ratio", 4.0f );
    const char *pDiffValue = out.Drive == VehicleSetup::eDrive::Front ? "_differential_value_front" : "_differential_value_rear";
    const char *pDiffType = out.Drive == VehicleSetup::eDrive::Front ? "_differential_type_front" : "_differential_type_rear";
    out.DifferentialMaxTorqueBias = dbI( pDiffValue, 0 ) * 0.01f;
    out.bDifferentialLocked = car[pDiffType].asInt( 1 ) == 3;
    out.ShiftTimeUp = dbF( "_shifttime_up", 0.1f );
    out.ShiftTimeDown = dbF( "_shifttime_down", 0.1f );
    out.GearboxInertia = dbI( "_gearbox_inertia", 0 ) * 0.01f;
    out.GearboxDamping = dbF( "_gearbox_damping", 0.0f );
    out.ReverseTime = cprFloat( "ReverseTime", 0.6f );

    // Brakes.
    out.MaxBrakingAction = dbI( "_brakes_power_front", 10000 );
    out.BrakingBalance = dbI( "_brake_balance", 60 ) * 0.01f;
    out.HandBrakeSlipRatio = dbF( "_hand_brake_slip_ratio", -0.2f );

    // Aerodynamics: Coefficient = |2 x load x (-|g|) / (1.184 x (velocity / 3.6)^2)|.
    const auto liftCoefficient = [&]( float loadKg, float velocityKmh ) {
        const float v = velocityKmh * kKmhToMs;
        return v != 0.0f ? std::abs( 2.0f * loadKg * -kGravityG / ( v * v * kAirDensity ) ) : 0.0f;
    };
    out.LiftDragRatio = dbF( "_lift_drag_ratio", 0.0f );
    out.SpoilerFrontCoefficient = liftCoefficient( dbF( "_lift_load_front", 0.0f ), dbF( "_lift_velocity", 0.0f ) );
    out.SpoilerRearCoefficient = liftCoefficient( dbF( "_lift_load_rear", 0.0f ), dbF( "_lift_velocity", 0.0f ) );
    out.GroundEffectCoefficient = liftCoefficient( dbF( "_ground_effect_load", 0.0f ), dbF( "_ground_effect_velocity", 0.0f ) );

    // Driving aids: SECURE column (cpr) + _xx_secure (DB), as set by the factory [CODE];
    // -aids sport|hypersport selects the other columns [INF: the runtime aid switch is not read].
    if ( pIni != nullptr ) {
        const char *pAid = CmdLine::GetArgValue( "-aids" );
        const std::string aid = pAid != nullptr ? pAid : "";
        out.AidLevel = StringCmpI( aid.c_str(), "hypersport" ) == 0 ? 2 : ( StringCmpI( aid.c_str(), "sport" ) == 0 ? 1 : 0 );
    }
    const char *absDb[] = { "_abs_slip_ratio_secure", "_abs_slip_ratio_sport", "_abs_slip_ratio_hypersport" };
    const char *tcDb[] = { "_traction_control_slip_ratio_secure", "_traction_control_slip_ratio_sport",
                           "_traction_control_slip_ratio_hypersport" };
    out.bAbs = car["_abs"].asInt( 1 ) != 0;
    out.bTractionControl = car["_traction_control"].asInt( 1 ) != 0;
    out.AbsActivation = cprFloat( "AbsControlActivation", 0.0f ) * kKmhToMs;
    out.HandBrakeActivation = cprFloat( "HandBrakeActivation", 0.0f ) * kKmhToMs;
    out.TractionActivation = cprFloat( "TractionControlActivation", 10.0f ) * kKmhToMs;
    out.AbsSlipRatio.Points = { { cprFloat( "AbsControlVelocityMin", 1.0f ) * kKmhToMs, cprFloat( "AbsControlSlipRatio", -0.2f, out.AidLevel ) },
                                { cprFloat( "AbsControlVelocityMax", 50.0f ) * kKmhToMs, dbF( absDb[out.AidLevel], -0.08f ) } };
    out.HandBrakeSlipRatio2.Points = { { cprFloat( "HandBrakeVelocityMin", 1.0f ) * kKmhToMs, cprFloat( "HandBrakeSlipRatio", -0.4f, out.AidLevel ) },
                                       { cprFloat( "HandBrakeVelocityMax", 50.0f ) * kKmhToMs, out.HandBrakeSlipRatio } };
    out.TractionSlipRatio.Points = {
        { cprFloat( "TractionControlVelocityMin", 10.0f ) * kKmhToMs, cprFloat( "TractionControlSlipRatio", 0.5f, out.AidLevel ) },
        { cprFloat( "TractionControlVelocityMax", 70.0f ) * kKmhToMs, dbF( tcDb[out.AidLevel], 0.1f ) } };
    std::sort( out.AbsSlipRatio.Points.begin(), out.AbsSlipRatio.Points.end() );
    std::sort( out.HandBrakeSlipRatio2.Points.begin(), out.HandBrakeSlipRatio2.Points.end() );
    std::sort( out.TractionSlipRatio.Points.begin(), out.TractionSlipRatio.Points.end() );

    // Steering, damping and world parameters.
    VehicleCurve fallback;
    fallback.Points = { { 0.0f, 380.0f * kDegToRadF }, { 400.0f * kKmhToMs, 340.0f * kDegToRadF } };
    if ( pIni != nullptr ) {
        out.SteeringVelocity = ReadIniCurve( *pIni, "SteeringVelocity", fallback, kDegToRadF );
        fallback.Points = { { 0.0f, 30.0f * kDegToRadF }, { 400.0f * kKmhToMs, 6.5f * kDegToRadF } };
        out.SteeringMaxAngle = ReadIniCurve( *pIni, "SteeringMaxAngle", fallback, kDegToRadF );
        fallback.Points = { { 0.0f, 1.0f }, { 400.0f * kKmhToMs, 1.0f } };
        out.SteeringSensitivity = ReadIniCurve( *pIni, "SteeringSensitivity", fallback, 1.0f );
        // CurveAngularDamping = SecureAngularDamping for every car at build time [CODE].
        const char *pDamping[] = { "SecureAngularDamping", "SportAngularDamping", "HyperSportAngularDamping" };
        fallback.Points = { { 0.0f, 0.4f }, { 400.0f * kKmhToMs, 2.0f } };
        out.AngularDamping = ReadIniCurve( *pIni, pDamping[out.AidLevel], fallback, 1.0f );
        const bool bOffRoad = car["_isoffroad"].asInt( 0 ) != 0;
        // FUN_00bdb970: off-road cars OffRoadAngularDamping, others SecureAngularDamping again [CODE].
        out.OffRoadAngularDamping = bOffRoad ? ReadIniCurve( *pIni, "OffRoadAngularDamping", out.AngularDamping, 1.0f ) : out.AngularDamping;
        fallback.Points = { { 0.0f, 0.0f } };
        out.DirtLinearDamping = ReadIniCurve( *pIni, bOffRoad ? "OffRoadDirtLinearDamping" : "NotOffRoadDirtLinearDamping", fallback, 1.0f );
        out.GrassLinearDamping =
            ReadIniCurve( *pIni, bOffRoad ? "OffRoadGrassLinearDamping" : "NotOffRoadGrassLinearDamping", fallback, 1.0f );
        out.Substeps = Clamp( pIni->getInt( "Car", "NbLoopForMiddlePrecision", 3 ), 1, 8 );
        out.ExtraGravityMin = cprFloat( "ExtraGravityMin", out.ExtraGravityMin );
        out.ExtraGravityMax = cprFloat( "ExtraGravityMax", out.ExtraGravityMax );
        out.ExtraGravityAccelDuration = cprFloat( "ExtraGravityAccelDuration", out.ExtraGravityAccelDuration );
        out.ExtraGravityAccelDelay = cprFloat( "ExtraGravityAccelDelay", out.ExtraGravityAccelDelay );
        out.CollisionOffset = cprFloat( "CollisionOffset", out.CollisionOffset );
    } else {
        out.SteeringVelocity = fallback;
    }

    // Car bank: torque curves and the Havok chassis.
    const std::string carName = ToLower( out.Db.FileName );
    Bank bank;
    if ( bank.load( ( gpFile->getBankFolder() + "vehicules/high/" + carName + ".bnk" ).c_str() ) ) {
        out.TorqueMax = ReadXmbCurve( bank, carName + "_max.xmb" );
        out.TorqueMin = ReadXmbCurve( bank, carName + "_min.xmb" );
        if ( const BankEntry *pDhk = bank.findEntry( ( carName + ".dhk" ).c_str() ) ) {
            ReadDhkChassis( *pDhk, out );
            HavokCollision collision;
            if ( LoadHavokCollision( pDhk->pData, pDhk->Size, collision, false ) ) {
                for ( const HavokCollision::Box &box : collision.Boxes ) {
                    if ( box.BodyIndex == 0 ) {
                        out.ChassisBoxes.push_back( { box.Transform.getTranslation(), box.HalfExtents } );
                    }
                }
            }
        }
    }
    if ( out.TorqueMax.empty() ) {
        // No curve: flat peak torque [INF].
        out.TorqueMax.Points = { { 0.0f, 0.0f }, { out.IdleVelocity, out.Db.TorqueNm * 0.7f },
                                 { out.Db.TorqueRPM * kRpmToRads, out.Db.TorqueNm }, { out.EngineMaxVelocity, out.Db.TorqueNm * 0.8f } };
    }
    if ( out.TorqueMin.empty() ) {
        out.TorqueMin.Points = { { 0.0f, 0.0f }, { out.EngineMaxVelocity, -0.3f * out.Db.TorqueNm } };
    }
    // FUN_00888870: CurveActionMaxThrottle scaled to _torque_nm (factor torque / MaxPoint.y).
    const float torqueNm = dbI( "_torque_nm", 0 );
    if ( torqueNm != 0.0f && out.TorqueMax.maxY() > 0.0f ) {
        const float scale = torqueNm / out.TorqueMax.maxY();
        for ( auto &point : out.TorqueMax.Points ) {
            point.second *= scale;
        }
    }
    // _engine_brake_nm: CurveActionNoThrottle y x (engine_brake_nm / MinPoint.y) (FUN_00bd8140 / bdb970 path) [CODE].
    const float engineBrake = dbI( "_engine_brake_nm", 0 );
    if ( engineBrake != 0.0f && out.TorqueMin.minY() != 0.0f ) {
        const float scale = engineBrake / out.TorqueMin.minY();
        for ( auto &point : out.TorqueMin.Points ) {
            point.second *= scale;
        }
    }
    // IdlePosition = a / (a - b), a = min(Tno(idle), -1e-5), b = max(Tmax(idle), Tno(idle), 1e-5) (FUN_00888200) [CODE].
    {
        const float a = Min( out.TorqueMin.sample( out.IdleVelocity ), -1e-5f );
        const float b = Max( Max( out.TorqueMax.sample( out.IdleVelocity ), out.TorqueMin.sample( out.IdleVelocity ) ), 1e-5f );
        out.IdlePosition = a / ( a - b );
        out.MaxPosition = Max( out.MaxPosition, out.IdlePosition + 1e-5f );
    }

    OTDU_LOG_INFO( "Vehicle '%s': lines blue %.0f / %.0f red %.0f / %.0f rpm (normal / sport), throttle ratios %.2f / %.2f, "
                   "shift %.2f / %.2f s, limiter %.2f s, rpm idle %.0f max %.0f, ABS %d TC %d act %.1f / %.1f m/s, diff bias %.2f\n",
                   out.Db.FileName.c_str(), out.BlueLine * 9.549297f, out.BlueLineSport * 9.549297f, out.RedLine * 9.549297f,
                   out.RedLineSport * 9.549297f, out.ThrottleRatioNormal, out.ThrottleRatioSport, out.ShiftTimeUp, out.ShiftTimeDown,
                   out.LimiterTime, out.IdleVelocity * 9.549297f, out.EngineMaxVelocity * 9.549297f, out.bAbs ? 1 : 0,
                   out.bTractionControl ? 1 : 0, out.AbsActivation, out.TractionActivation, out.DifferentialMaxTorqueBias );
    OTDU_LOG_INFO( "Vehicle '%s': %.0f kg, size %.3f %.3f %.3f, I %.0f %.0f %.0f, CG h %.2f front %.2f, axle %.3f wb %.3f, "
                   "tyre r %.4f (%s), mu %.2f/%.2f, k %.0f/%.0f, c %.0f/%.0f, stops %.3f..%.3f, %zu gears final %.2f, "
                   "idle pos %.3f, aero %.3f/%.3f/%.3f LDR %.2f, aids %u\n",
                   out.Db.FileName.c_str(), out.Mass, out.Size.x, out.Size.y, out.Size.z, out.Inertia.x, out.Inertia.y,
                   out.Inertia.z, out.CgHeight, out.LoadDistribution, out.FrontAxleLocalPos, out.WheelBase, radius,
                   tarmac.Name.c_str(), out.TyreFront.MaxLateralFriction[0] / 1000.0f, out.TyreRear.MaxLateralFriction[0] / 1000.0f,
                   out.SuspFront.StiffnessAtRest, out.SuspRear.StiffnessAtRest, out.SuspFront.DampingBump, out.SuspRear.DampingBump,
                   out.SuspFront.LowerStop, out.SuspFront.UpperStop, out.Gears.size(), out.FinalDrive, out.IdlePosition,
                   out.SpoilerFrontCoefficient, out.SpoilerRearCoefficient, out.GroundEffectCoefficient, out.LiftDragRatio,
                   out.AidLevel );
    return true;
}
