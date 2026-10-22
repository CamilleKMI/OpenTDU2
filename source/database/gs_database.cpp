#include "shared.h"
#include "gs_database.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"

#include "imgui.h"

GSDatabase *gpDatabase = nullptr;

GSDatabase::GSDatabase() : bLoaded( false ) { gpDatabase = this; }

GSDatabase::~GSDatabase() { gpDatabase = nullptr; }

bool GSDatabase::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    const std::string folder = gpFile->getBankFolder() + "Database/";
    if ( !LoadXmbFromFile( ( folder + "db_data.cpr" ).c_str(), database ) ) {
        OTDU_LOG_WARN( "Failed to load the game database (db_data.cpr)\n" );
    } else {
        bLoaded = true;
        OTDU_LOG_INFO( "Database loaded: %u car physics entries\n",
                       database.getRoot().getArraySize( "DBCarPhysicsData" ) );
    }

    std::string language = gpConfig != nullptr ? gpConfig->getLanguage() : "us";
    std::string localizationPath = folder + "db_" + language + ".cpr";
    for ( const char *pFallback : { "us", "fr", "ge", "sp", "it" } ) {
        if ( gpFile->fileExists( localizationPath.c_str() ) ) {
            break;
        }
        localizationPath = folder + "db_" + pFallback + ".cpr";
    }
    if ( LoadXmbFromFile( localizationPath.c_str(), localization ) ) {
        XmbValue root = localization.getRoot();
        const uint32_t numLines = root.getArraySize( "db_data" );
        for ( uint32_t i = 0; i < numLines; i++ ) {
            XmbValue line = root.getArrayElement( "db_data", i );
            strings[static_cast<int32_t>( line["HashCode"].asInt() )] = line["Text"].asString();
        }
        OTDU_LOG_INFO( "Localization '%s': %zu strings\n", localizationPath.c_str(),
                       strings.size() );
    } else {
        OTDU_LOG_WARN( "Failed to load localization '%s'\n", localizationPath.c_str() );
    }
    return true;
}

void GSDatabase::terminate() { strings.clear(); }

const char *GSDatabase::getString( int32_t hashcode ) const {
    auto it = strings.find( hashcode );
    return it != strings.end() ? it->second.c_str() : nullptr;
}

std::vector<std::string> GSDatabase::getCarFileNames() const {
    std::vector<std::string> names;
    if ( !bLoaded ) {
        return names;
    }
    XmbValue root = database.getRoot();
    const uint32_t numCars = root.getArraySize( "DBCarPhysicsData" );
    for ( uint32_t i = 0; i < numCars; i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_isvalidpc"].asInt() != 0 ) {
            names.push_back( car["_car_file_name"].asString() );
        }
    }
    return names;
}

bool GSDatabase::findCarPhysics( const char *pFileName, CarPhysicsData &out ) const {
    if ( !bLoaded ) {
        return false;
    }
    XmbValue root = database.getRoot();
    const uint32_t numCars = root.getArraySize( "DBCarPhysicsData" );
    for ( uint32_t i = 0; i < numCars; i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( car["_car_file_name"].asString().c_str(), pFileName ) != 0 ) {
            continue;
        }

        out.FileName = car["_car_file_name"].asString();
        out.ModelName = car["_model_name"].asString();
        out.BrandHashcode = car["_car_brand"].asUInt64();
        out.CarId = static_cast<int32_t>( car["_id_car"].asInt() );
        out.Mass = car["_mass"].asFloat( out.Mass );
        out.PowerBHP = car["_power_bhp"].asFloat( out.PowerBHP );
        out.PowerRPM = car["_power_rpm"].asFloat( out.PowerRPM );
        out.TorqueNm = car["_torque_nm"].asFloat( out.TorqueNm );
        out.TorqueRPM = car["_torque_rpm"].asFloat( out.TorqueRPM );
        out.RPMMin = car["_rpm_min"].asFloat( out.RPMMin );
        out.RPMMax = car["_rpm_max"].asFloat( out.RPMMax );
        out.RedLine = car["_red_line"].asFloat( out.RedLine );
        out.MaxSpeed = car["_max_theorical_speed"].asFloat( out.MaxSpeed );
        out.FinalDriveRatio = car["_final_drive_ratio"].asFloat( out.FinalDriveRatio );
        out.Drive = static_cast<int32_t>( car["_drive"].asInt( out.Drive ) );

        const int32_t numGears = static_cast<int32_t>( car["_nb_gears"].asInt( 5 ) );
        out.GearRatios.clear();
        for ( int32_t gear = 1; gear <= Min( numGears, 11 ); gear++ ) {
            std::string field = "_gear_ratio" + std::to_string( gear );
            float ratio = car[field.c_str()].asFloat( 0.0f );
            if ( ratio > 0.0f ) {
                out.GearRatios.push_back( ratio );
            }
        }

        out.GravityCenterHeight = car["_gravity_center_height"].asFloat( 50.0f ) / 100.0f;
        out.GravityCenterLength = car["_gravity_center_length"].asFloat( 50.0f ) / 100.0f;
        out.TrackFront = car["_track_front"].asFloat( 1600.0f ) / 1000.0f;
        out.TrackRear = car["_track_back"].asFloat( 1600.0f ) / 1000.0f;
        out.Length = car["_length"].asFloat( 4500.0f ) / 1000.0f;
        out.Height = car["_height"].asFloat( 1300.0f ) / 1000.0f;
        out.TireRadius = car["_tire_radius"].asFloat( 32.0f ) / 200.0f;
        out.SuspensionLengthFront = car["_suspension_length_front"].asFloat( 100.0f ) / 1000.0f;
        out.SuspensionLengthRear = car["_suspension_lenght_rear"].asFloat( 100.0f ) / 1000.0f;
        out.SuspensionRateFront = car["_suspension_rate_front"].asFloat( out.SuspensionRateFront );
        out.SuspensionRateRear = car["_suspension_rate_rear"].asFloat( out.SuspensionRateRear );
        out.DamperRateFront = car["_damper_rate_front"].asFloat( out.DamperRateFront );
        out.DamperRateRear = car["_damper_rate_rear"].asFloat( out.DamperRateRear );
        out.AntiRollFront = car["_antirollbar_front"].asFloat( out.AntiRollFront );
        out.AntiRollRear = car["_antirollbar_rear"].asFloat( out.AntiRollRear );
        out.LateralFrictionFront = car["_max_lateral_friction_front"].asFloat( out.LateralFrictionFront );
        out.LateralFrictionRear = car["_max_lateral_friction_rear"].asFloat( out.LateralFrictionRear );
        out.LongitudinalFrictionFront = car["_max_longitudinal_friction_front"].asFloat( out.LongitudinalFrictionFront );
        out.LongitudinalFrictionRear = car["_max_longitudinal_friction_rear"].asFloat( out.LongitudinalFrictionRear );
        out.BrakePowerFront = car["_brakes_power_front"].asFloat( out.BrakePowerFront );
        out.BrakeBalance = car["_brake_balance"].asFloat( out.BrakeBalance );
        out.Cx = car["_cx"].asFloat( out.Cx );
        out.ShiftTimeUp = car["_shifttime_up"].asFloat( out.ShiftTimeUp );
        out.EngineInertia = car["_engine_inertia"].asFloat( out.EngineInertia );
        return true;
    }
    return false;
}

void GSDatabase::drawDebug() {
    if ( !ImGui::Begin( "Database" ) ) {
        ImGui::End();
        return;
    }
    ImGui::Text( "Localized strings: %zu", strings.size() );
    static char filter[64] = "";
    ImGui::InputText( "Filter", filter, sizeof( filter ) );
    if ( ImGui::BeginChild( "strings", ImVec2( 0, 300 ) ) ) {
        uint32_t shown = 0;
        for ( const auto &entry : strings ) {
            if ( filter[0] != '\0' && entry.second.find( filter ) == std::string::npos ) {
                continue;
            }
            ImGui::Text( "%6d  %s", entry.first, entry.second.c_str() );
            if ( ++shown > 500 ) {
                break;
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
