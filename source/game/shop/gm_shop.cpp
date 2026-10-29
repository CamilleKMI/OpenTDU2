#include "shared.h"
#include "gm_shop.h"

#include "audio/gs_audio.h"
#include "config/gs_config.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"
#include "filesystem/gs_file.h"

namespace {
// FUN_00b10da0 case 8 [CODE]: spot 0x5f425f95_848b8576 = ESTOL_B_1275I,
// FUN_00aba280(0x146, -1, -1, -1).
constexpr const char *kDealerSpot = "ESTOL_B_1275I";
constexpr int32_t kStepLoanCar = 0x146; // 326 Dino_246_GTS
constexpr int32_t kLocBuyACar = 0xfc1;  // 4033 "Buy a car!" (pop_goal, TUTO_BUY_A_CAR)
constexpr int32_t kLocShopMessage = 0x9f3; // FUN_009f4ab0 /:Message
constexpr int32_t kLocMoneyFormat = 0x3a4; // FUN_0099d810 (FUN_0099a210 text 932)
// Gauge ranges (FUN_00adea90(value, min, max) = round((v - min) / (max - min)
// * 100), <= 100) [CODE + DATA .data 0x00f7d4f4..0x00f7d50c, 0x00edf3d8].
constexpr float kAccelGauge[2] = { 35.0f, 2.25f };   // _acceleration_0_100_kmh
constexpr float kSpeedGauge[2] = { 70.0f, 470.0f };  // _max_theorical_speed
constexpr float kBrakingGauge[2] = { 170.0f, 96.0f }; // _braking_100_0
constexpr float kHandlingGauge[2] = { 0.0f, 100.0f }; // _handling (DAT_00fd90f0: bss, no readable writer [INF 0])
constexpr double kKmToMiles = 0.6215040683746338;     // DAT_00f4a200
constexpr double kKgToLb = 0.45359237;                // DAT_00f4ae58 (divisor)
constexpr double kZeroToSixty = 60.0 / 62.15040588378906; // DAT_00f49b28 / DAT_00f4aac8
// FUN_00d90a00 / FUN_009f2ae0: DBCarShop _isbuyable_1..9 (row +0x10..+0x18).
constexpr int32_t kShopSlots = 9;

std::string LocString( int32_t id ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr ? pText : "";
}

XmbValue FindRow( const char *pTable, const char *pField, int64_t value, int32_t *pOutIndex = nullptr ) {
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( pTable ); i++ ) {
        XmbValue row = root.getArrayElement( pTable, i );
        if ( static_cast<int64_t>( row[pField].asUInt64() ) == value ) {
            if ( pOutIndex != nullptr ) {
                *pOutIndex = static_cast<int32_t>( i );
            }
            return row;
        }
    }
    if ( pOutIndex != nullptr ) {
        *pOutIndex = -1;
    }
    return XmbValue();
}

int32_t Gauge( float value, const float range[2] ) {
    // FUN_00adea90 [CODE].
    const int32_t percent = static_cast<int32_t>( std::lround( ( value - range[0] ) / ( range[1] - range[0] ) * 100.0f ) );
    return static_cast<int32_t>( Min( static_cast<uint32_t>( percent ), 100u ) );
}

// Compare record (DAT_01025608 + 0x9bb4, filled by FUN_00b95840 on the
// FSCommand comp; global: it survives the shop, FUN_009f2c90 writes it to
// SHOPSCAR at every scr_open through FUN_00b95e30) [CODE].
struct CompareRecord {
    bool bValid = false;       // +0
    std::string Name;          // +4 (car descriptor name)
    std::string Group;         // +8 FUN_00d8f5c0 (category name)
    std::string Upgrade = "level0"; // +0xc
    int32_t SpeedGauge = 0;    // +0x10 FUN_00adeea0
    int32_t AccelGauge = 0;    // +0x14 FUN_00adef30
    int32_t HandlingGauge = 0; // +0x18 FUN_00adea90(_handling, DAT_00fd90f0, 100)
    int32_t BrakingGauge = 0;  // +0x1c FUN_00adee20
    int32_t MaxSpeedMph = 0;   // +0x20 FUN_006c8f00 (ENC rounding) of the mph speed
    int32_t MaxSpeed = 0;      // +0x24 (int) FUN_00adec10
    float ZeroToSixty = 0.0f;  // +0x28 0-100 / 62.15 * 60
    float ZeroToHundred = 0.0f; // +0x2c FUN_00adec80
    int32_t Power = 0;         // +0x30 / +0x34 FUN_00adece0
    int32_t Difficulty = 1;    // +0x38 record +0x78 -> +0x140, clamped 1..5
};
CompareRecord &GetCompareRecord() {
    static CompareRecord record;
    return record;
}

std::string Format( const char *pFormat, ... ) {
    char buffer[256];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( buffer, sizeof( buffer ), pFormat, args );
    va_end( args );
    return buffer;
}
} // namespace

GMShop::GMShop() : GameMode( eGameMode::GM_Shop ) {}

GMShop::~GMShop() { exit(); }

std::string GMShop::formatMoney( int32_t value ) const {
    // FUN_0099d810: groups of three digits ("%s%03d%s") separated by ","
    // when the language is "us" (DAT_00f2bf70), else " " (DAT_00f35004),
    // then the DB text 932 format '$\xA71\xA7' (FUN_0099a210): FUN_00999d70
    // counts the "\xA7n\xA7" markers, FUN_00999fc0 puts argument n in place
    // of marker n [CODE].
    const bool bUs = gpConfig == nullptr || gpConfig->getLanguage() == "us";
    const char *pSeparator = bUs ? "," : " ";
    std::string digits;
    int32_t rest = value;
    while ( rest > 999 ) {
        digits = Format( "%s%03d%s", pSeparator, rest % 1000, digits.c_str() );
        rest /= 1000;
    }
    digits = Format( "%d%s", rest, digits.c_str() );
    const std::string format = LocString( kLocMoneyFormat );
    const std::string token = "\xC2\xA7" "1" "\xC2\xA7";
    const size_t marker = format.find( token );
    if ( marker == std::string::npos ) {
        return format.empty() ? digits : format;
    }
    return format.substr( 0, marker ) + digits + format.substr( marker + token.size() );
}

bool GMShop::loadCars() {
    // DBCarShop row of the spot (FUN_00d8b550: _spot_name hash at row
    // +0x98) [CODE + DATA].
    XmbValue root = gpDatabase->getDocument().getRoot();
    XmbValue shopRow;
    bool bRow = false;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarShop" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBCarShop", i );
        if ( row["_spot_name"].asString() == kDealerSpot ) {
            shopRow = row;
            bRow = true;
            OTDU_LOG_INFO( "SHOP: DBCarShop[%u] %s\n", i, kDealerSpot );
            break;
        }
    }
    if ( !bRow ) {
        return false;
    }
    // Title (FUN_009f31b0: /:Title = DB text of _libelle_hc, row +0x20).
    shopTitleId = static_cast<uint32_t>( shopRow["_libelle_hc"].asUInt64() );
    shopTitle = LocString( static_cast<int32_t>( shopTitleId ) );

    // Entity__ShopData D_ShopData PosCams[] of the scene bank -> PosCam names.
    std::vector<std::string> posCams;
    if ( const BankEntry *pEntities = scene.getSceneBank().findEntry( "entities.xmb" ) ) {
        XmbDocument entities;
        if ( entities.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
            std::unordered_map<uint64_t, std::string> posCamNames;
            for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__PosCam" ); i++ ) {
                XmbValue posCam = entityExport.getArrayElement( "ArrayOf_Entity__PosCam", i );
                posCamNames[posCam["__Hc"].asUInt64()] = posCam["__Name"].asString();
            }
            if ( entityExport.getArraySize( "ArrayOf_Entity__ShopData" ) > 0 ) {
                XmbValue shopData = entityExport.getArrayElement( "ArrayOf_Entity__ShopData", 0 );
                for ( uint32_t i = 0; i < shopData.getArraySize( "PosCams" ); i++ ) {
                    const uint64_t hash = shopData.getArrayElement( "PosCams", i )["PosCam"].asUInt64();
                    posCams.push_back( posCamNames.count( hash ) ? posCamNames[hash] : std::string() );
                }
            }
        }
    }

    // FUN_009f4080: the slots of the dealer (+0xd0, 8 cars); a DBCarShop slot
    // that FUN_009f2ae0 accepts (_isbuyable_N and the car checks of
    // FUN_00d90a00) gets colour 0, the DBCarsAndRims row equal to the car's
    // _default_rims and interior 0; the random colours / rims / interior
    // (LCG seed * 0x7ff8a3ed + 0x2aa01d31) are for the other cars [CODE].
    // The used-car stock (FUN_00de3460 / FUN_00dde990) replaces them only at
    // story steps 11 / -1 [CODE].
    for ( int32_t slot = 0; slot < kShopSlots && cars.size() < 8; slot++ ) {
        const std::string slotField = "_slot_" + std::to_string( slot + 1 );
        const std::string buyableField = "_isbuyable_" + std::to_string( slot + 1 );
        const int32_t idCar = static_cast<int32_t>( shopRow[slotField.c_str()].asUInt64() );
        if ( idCar == 0 || shopRow[buyableField.c_str()].asInt() == 0 ) {
            continue;
        }
        ShopCar car;
        car.Slot = slot;
        car.IdCar = idCar;
        XmbValue physics = FindRow( "DBCarPhysicsData", "_id_car", idCar, &car.PhysicsRow );
        if ( car.PhysicsRow < 0 ) {
            continue;
        }
        car.RimsHashcode = physics["_default_rims"].asUInt64();
        // DBCarsAndRims rows of the car in table order; the default one.
        int32_t rimsIndex = 0;
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarsAndRims" ); i++ ) {
            XmbValue rims = root.getArrayElement( "DBCarsAndRims", i );
            if ( static_cast<int32_t>( rims["_car"].asUInt64() ) != idCar ) {
                continue;
            }
            if ( rims["_rim"].asUInt64() == car.RimsHashcode ) {
                car.Rims = rimsIndex;
                break;
            }
            rimsIndex++;
        }
        // Colour 0: the first DBCarsAndColors row of the car (multimap in
        // table order [INF, docs/re/paint_spec.md 1]).
        car.Color = 0;
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarsAndColors" ); i++ ) {
            XmbValue color = root.getArrayElement( "DBCarsAndColors", i );
            if ( static_cast<int32_t>( color["_car"].asUInt64() ) == idCar ) {
                car.ColorRow = static_cast<int32_t>( i );
                car.ColorName = color["_color_name"].asString();
                break;
            }
        }
        car.Interior = 0;
        // Name: brand _manufacturer_name, _car_model, _car_version [INF: the
        // car descriptor name (record +0x44 -> +4) is built in encrypted code].
        XmbValue brand = FindRow( "DBBrands", "_hashcode", static_cast<int64_t>( physics["_car_brand"].asUInt64() ) );
        car.Name = brand["_manufacturer_name"].asString() + " " + physics["_car_model"].asString() + " " +
                   physics["_car_version"].asString();
        // Price: dealer vfunc 0x38 = FUN_00dde780(car, colour, rims, interior):
        // 0 for a DBRewardCar _carunlocked car (FUN_00b18370), else
        // FUN_00d8d140: _price + rims _price (DBRims +0x44) + colour _price
        // (+0xf8) + interior _price (DBInterior +0x94), minus the stock
        // discount % (FUN_00de3050: 0, the stock is empty before step 11 [INF:
        // new profile]), minus the collection level _percent (DBCollectionLevel
        // row of the collection points, FUN_00445780 [ENC]: 0 points -> row 0,
        // 0 %) [CODE + DATA].
        bool bReward = false;
        for ( uint32_t i = 0; i < root.getArraySize( "DBRewardCar" ); i++ ) {
            if ( static_cast<int32_t>( root.getArrayElement( "DBRewardCar", i )["_carunlocked"].asUInt64() ) == idCar ) {
                bReward = true;
            }
        }
        int32_t price = static_cast<int32_t>( physics["_price"].asInt() );
        price += static_cast<int32_t>( FindRow( "DBRims", "_hashcode", static_cast<int64_t>( car.RimsHashcode ) )["_price"].asInt() );
        if ( car.ColorRow >= 0 ) {
            XmbValue color = root.getArrayElement( "DBCarsAndColors", static_cast<uint32_t>( car.ColorRow ) );
            price += static_cast<int32_t>( color["_price"].asInt() );
            const uint64_t interior = color["_interior_1"].asUInt64();
            price += static_cast<int32_t>( FindRow( "DBInterior", "_hashcode", static_cast<int64_t>( interior ) )["_price"].asInt() );
        }
        car.Price = bReward ? 0 : price;
        car.PosCam = static_cast<size_t>( slot ) < posCams.size() ? posCams[slot] : std::string();
        CinematicCar::Desc desc;
        if ( !CinematicCar::FindDatabaseDesc( idCar, car.RimsHashcode, desc ) ) {
            continue;
        }
        car.ShowCar = scene.addShowCar( desc, car.PosCam );
        // FUN_00a50ca0(colour) -> FUN_00d8e6a0 on the slot car: the
        // _color_id_1 material of the car colour library (paint_spec 3.2).
        if ( car.ShowCar >= 0 && car.ColorRow >= 0 ) {
            const std::string colorId =
                root.getArrayElement( "DBCarsAndColors", static_cast<uint32_t>( car.ColorRow ) )["_color_id_1"].asString();
            const bool bPainted = scene.getShowCar( static_cast<size_t>( car.ShowCar ) ).applyColor( colorId );
            OTDU_LOG_INFO( "SHOP: %s colour %s %s\n", desc.FileName.c_str(), colorId.c_str(),
                           bPainted ? "applied" : "not found" );
        }
        OTDU_LOG_INFO( "SHOP: slot %d car %d %s '%s' colour '%s' rims %d (%s) price %d at %s (show car %d)\n", slot,
                       idCar, desc.FileName.c_str(), car.Name.c_str(), car.ColorName.c_str(), car.Rims,
                       desc.RimFront.c_str(), car.Price, car.PosCam.c_str(), car.ShowCar );
        cars.push_back( car );
    }
    // Walk-mode selectables of the cars (the walk manager car wrappers +0x6e0,
    // SelItemType "vehicle"): the body box of each car [INF: the wrapper hit
    // test vf04 is encrypted; FUN_00950860 uses the car bounds FUN_00c5c9e0].
    const Matrix4x4f toScene = scene.getSceneOrigin().inverse();
    for ( ShopCar &car : cars ) {
        Matrix4x4f world;
        Vector3f boundsMin, boundsMax;
        if ( car.ShowCar < 0 || !scene.getShowCarWorld( static_cast<size_t>( car.ShowCar ), world ) ||
             !scene.getShowCar( static_cast<size_t>( car.ShowCar ) ).getBodyBounds( boundsMin, boundsMax ) ) {
            continue;
        }
        const Matrix4x4f local = toScene * world;
        const Vector3f size = boundsMax - boundsMin;
        const Vector3f center = ( boundsMin + boundsMax ) * 0.5f;
        const Matrix4x4f toUnit = Matrix4x4f::Scale( Vector3f( 1.0f / size.x, 1.0f / size.y, 1.0f / size.z ) ) *
                                  Matrix4x4f::Translation( center * -1.0f ) * local.inverse();
        IntroCinematic::Volume volume;
        volume.Name = car.PosCam;
        volume.OfType = 2;
        volume.SelectableType = 57;
        volume.SelectableString = "vehicle";
        for ( int32_t r = 0; r < 4; r++ ) {
            for ( int32_t c = 0; c < 4; c++ ) {
                volume.Matrix[r][c] = toUnit.m[r][c];
            }
        }
        volume.Center = local.transformPoint( center );
        car.Selectable = scene.addSelectable( volume );
    }
    return !cars.empty();
}

bool GMShop::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "SHOP" );
    }
    // Story step 7 enter (FUN_00b10da0 case 8): profile+0x58 = 7,
    // FUN_00aba280(0x146, -1, -1, -1): the loaned car (garage slot 0x280)
    // becomes the Dino 246 GTS (FUN_00ab9be0 -> FUN_00abffd0), DAT_01025784
    // = index of ESTOL_B_1275I, mode 3 [CODE].
    PlayerProfile &profile = GetPlayerProfile();
    // Back from the test drive the story step is still 7 (its enter event
    // ran once): the current car stays the test car (FUN_00a90c70 restores
    // it only at steps 11 / -1) [CODE].
    const bool bReturning = pInstance->getPreviousGameMode() == eGameMode::GM_TestCar;
    profile.StoryStep = 7;
    if ( !bReturning ) {
        profile.LoanCar = kStepLoanCar;
        profile.CurrentGarageSlot = -1;
    }
    if ( CmdLine::HasArg( "-money" ) ) {
        profile.Money = CmdLine::GetArgInt( "-money", profile.Money );
    }
    if ( profile.AvatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        profile.AvatarConfig = GetStoryAvatarConfig( pAvatar != nullptr ? pAvatar : "AVT_Male_Intro" );
    }
    IntroCinematic::SpotRecord spot;
    if ( IntroCinematic::ReadSpot( kDealerSpot, spot ) ) {
        // FUN_009f3050: rt+0x79 (rec+0x100) 0x11 -> the used car dealer object
        // FUN_00dde970 (0x1fb0 bytes, vtable 0x00f26380), shop type +0xf0 = 3
        // (FUN_009ec8d0), SHOPSCAR page "scr_open" (FUN_009f23e0) [CODE + DATA].
        OTDU_LOG_INFO( "SHOP: spot %s type 0x%x at (%.2f, %.2f, %.2f) heading %.3f\n", kDealerSpot, spot.Type,
                       spot.Position.x, spot.Position.y, spot.Position.z, spot.Heading );
    }
    // FUN_00a91360: interior bank "%sInterior/%s.bnk" of the spot bank name
    // (rt+0x70) with its first letter replaced by 'I' (mode != 9) -> ISTOL_B;
    // the exterior is the spot bank estol_b (the island exterior set).
    IntroCinematic::Setup setup;
    setup.Bank = "interior/istol_b.bnk";
    setup.HouseSpot = kDealerSpot;
    setup.ExteriorBank = "islands/ibiza/exterior/estol_b.bnk";
    setup.ExteriorPrefix.clear();
    setup.bWorld = true;
    setup.GameModeId = 3;
    setup.bExteriorCollision = false;
    setup.UiMovie = "shopscar";
    scene.setCommandCallback(
        [this]( const std::string &command, const std::string &args ) { onCommand( command, args ); } );
    // GENERAL.vmf FSCommands (dialog box: wmof, wmon, dbxc) [INF: the
    // FSCommand handlers are global, any movie reaches them].
    scene.setGeneralCommandCallback(
        [this]( const std::string &command, const std::string &args ) { onCommand( command, args ); } );
    bLoaded = scene.load( setup );
    if ( !bLoaded ) {
        OTDU_LOG_WARN( "SHOP: interior/istol_b.bnk could not be loaded\n" );
        return true;
    }
    scene.setPlayerAvatar( profile.AvatarConfig );
    // The current (loaned) car: the .sce actor PLAYER_CAR of C_FIRST_INTRO's
    // exterior scene.
    CinematicCar::Desc loanDesc;
    if ( CinematicCar::FindDatabaseDesc( profile.LoanCar, bReturning ? GetTestDriveRequest().RimsHashcode : 0, loanDesc ) ) {
        scene.loadCar( loanDesc, "", "", "PLAYER_CAR" );
    }
    if ( !loadCars() ) {
        OTDU_LOG_WARN( "SHOP: no car in shop (DBCarShop %s)\n", kDealerSpot );
    }
    // FUN_009f4ab0 (mode init) on SHOPSCAR: /:num_car 1, /:nb_cars
    // (FUN_009f2c00), /:Message (DB text 0x9f3); story step 7: FUN_00950bf0,
    // mode+0x1ae9f8 = 1, /:IsTutorial true. FUN_009f31b0: /:Title. Used
    // dealer init [1] (FUN_00dde7e0 -> FUN_009ed1b0): /:MyMoneyStr,
    // /:LockScrList (no stock car), /:ShowSellVehicles [CODE].
    FlashPlayer &ui = scene.getUi();
    ui.setVariableNumber( "/:num_car", 1.0 );
    ui.setVariableNumber( "/:nb_cars", static_cast<double>( cars.size() ) );
    ui.setVariable( "/:Message", LocString( kLocShopMessage ) );
    ui.setVariableBool( "/:IsTutorial", true );
    ui.setVariable( "/:Title", shopTitle );
    ui.setVariableBool( "/:LockScrList", cars.empty() );
    ui.setVariableBool( "/:ShowSellVehicles", !profile.Garage.empty() );
    ui.setVariableBool( "/:IsWalkmode", true );
    // "/:state_Metric" = (unit option DAT_010017f0+0x14 != 1) [CODE 0xa920e7];
    // the unit option of the player settings is not implemented: 0 (metric,
    // also the value without the options object) [INF].
    ui.setVariableBool( "/:state_Metric", true );
    // FUN_00dbadb0 sets on every loaded movie "/:Version" = "W.I.P" and
    // "/:state_Demo" = false [CODE 0xdbae48 / 0xdbaeb1]: SHOPSCAR TestMoney
    // only evaluates /:EnoughMoney when state_Demo == 0.
    ui.setVariable( "/:Version", "W.I.P" );
    ui.setVariableBool( "/:state_Demo", false );
    updateMoneyVariables();
    walk.setSelectionCallback( [this]( int32_t index ) { onSelection( index ); } );
    walk.setCursorLabels( true );
    // Mode init: the intro cinematic +0xb43c = C_FIRST_INTRO (the previous
    // mode is not 7), CinProps |= 4 (skippable); the loop +0xb440 =
    // C_FIRST_LOOP; the buy cinematic +0x1ba334 = C_EXPLAIN_FROG at story
    // step 7 (else C_BUY_CAR_I) [CODE]. FUN_00a91ab0 clears +0xb43c when the
    // previous mode is CINE (0x1f) and FUN_00425d70() [ENC] is false; the
    // story needs Todd's welcome: played [INF].
    for ( int32_t id : { 0x3a4, 0x434, 0x443, 0x442, 0x2e8, 0x438, 0x446, 0x2e2, 0x3e1, 0x3e2, 0x2e7, 0x1342, 0x80a,
                         0x80c, 0x80d } ) {
        OTDU_LOG_INFO( "SHOP: DB text 0x%x = '%s'\n", id, LocString( id ).c_str() );
    }
    OTDU_LOG_INFO( "SHOP: '%s' (%u), %zu cars, money %d, title '%s', message '%s', loan car 0x%x\n",
                   shopTitle.c_str(), shopTitleId, cars.size(), profile.Money, shopTitle.c_str(),
                   LocString( kLocShopMessage ).c_str(), profile.LoanCar );
    // FUN_009f4ab0 (used dealer, stock not won, story step 7): +0xb43c =
    // C_INTRO_I when the previous mode is 7 TESTCAR (back from the test
    // drive), else C_FIRST_INTRO (CinProps |= 4) [CODE].
    const bool bFromTestDrive = pInstance->getPreviousGameMode() == eGameMode::GM_TestCar;
    introCinematic = bFromTestDrive ? "C_INTRO_I" : "C_FIRST_INTRO";
    if ( bFromTestDrive ) {
        GetTestDriveRequest().bActive = false;
        OTDU_LOG_INFO( "SHOP: back from the test drive (previous mode TESTCAR) -> %s\n", introCinematic.c_str() );
    }
    if ( CmdLine::HasArg( "-skipcine" ) || !scene.playMain( introCinematic, true,
                                                            static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) ) ) ) {
        startWalk();
    } else {
        state = eState::Intro;
    }
    return true;
}

void GMShop::exit() {
    if ( bLoaded ) {
        scene.unload();
    }
    bLoaded = false;
}

void GMShop::updateMoneyVariables() {
    // FUN_009ed1b0 /:MyMoneyStr, FUN_0099daa0 /:money (formatted money).
    const std::string money = formatMoney( GetPlayerProfile().Money );
    FlashPlayer &ui = scene.getUi();
    ui.setVariable( "/:MyMoneyStr", money );
    ui.setVariable( "/:money", money );
    ui.setVariableNumber( "/:MyMoneyInt", static_cast<double>( GetPlayerProfile().Money ) );
}

void GMShop::startWalk() {
    // Intro end (listener FUN_00a91820 event 3 of +0xb43c): mode vfunc 0x1c
    // = FUN_009f2c90 (SHOPSCAR /:IsCarInStock 2 (dealer vfunc 0x18 false),
    // goto "scr_open"), C_FIRST_LOOP started (FUN_00b402a0) [CODE]; the walk
    // camera at the Entity__FirstPerson StartCamera CAM_INTRO (Fov 0.6109)
    // [DATA].
    scene.stopMain();
    scene.playBackground( "C_FIRST_LOOP", true );
    FlashPlayer &ui = scene.getUi();
    // FUN_009f2c90: FUN_00b95e30 (/:is_compare = compare record valid, the
    // /:comp_* variables when valid), /:IsCarInStock 2, goto "scr_open".
    writeCompareVariables();
    ui.setVariableNumber( "/:IsCarInStock", 2.0 );
    ui.gotoLabel( "/", "scr_open", true );
    float fov = 0.610865f;
    walk.start( scene, "CAM_INTRO", fov );
    bWalkInput = true;
    state = eState::Walk;
}

void GMShop::onSelection( int32_t selectable ) {
    // The car wrapper over (vf14, FUN_00da50e0 / FUN_0099dbd0): the car data
    // of the movie before /:SelItemType and ItemType('over').
    hoveredCar = -1;
    for ( size_t i = 0; i < cars.size(); i++ ) {
        if ( cars[i].Selectable == selectable && selectable >= 0 ) {
            hoveredCar = static_cast<int32_t>( i );
            fillCarVariables( cars[i] );
            OTDU_LOG_INFO( "SHOP: car %s under the reticle ('%s', %s)\n", cars[i].PosCam.c_str(),
                           cars[i].Name.c_str(), formatMoney( cars[i].Price ).c_str() );
        }
    }
}

void GMShop::fillCarVariables( const ShopCar &car ) {
    // FUN_0099dbd0(record, false, movie, 0) + FUN_009f25f0 / FUN_009f4510
    // [CODE]; DBCarPhysicsData offsets of the code -> fields by the row layout
    // (0x470 bytes, fields in name order: +0x14 _acceleration_0_100_kmh,
    // +0x70 _braking_100_0, +0x144 _displacement, +0x150 _drive, +0x168
    // _engine_localisation, +0x170 _engine_type, +0x1cc _gearbox, +0x1f0
    // _handling, +0x1f4 _hasdoors, +0x240 _mass, +0x270 _max_theorical_speed,
    // +0x294 _nb_gears, +0x318 _power_bhp, +0x31c _power_rpm, +0x3f4
    // _torque_nm, +0x3f8 _torque_rpm, +0x41c _vehiclecategory, +0x45c
    // _windows_type) [DATA].
    XmbValue root = gpDatabase->getDocument().getRoot();
    XmbValue physics = root.getArrayElement( "DBCarPhysicsData", static_cast<uint32_t>( car.PhysicsRow ) );
    FlashPlayer &ui = scene.getUi();
    ui.setVariable( "/:Current_car", car.Name );
    ui.setVariable( "/:current_car", car.Name );
    // /:cur_group: car category name (table 0x00edfbe0, FUN_00d8f5c0) [CODE + DATA].
    static const std::pair<int32_t, const char *> kCategories[] = {
        { 1, "A1" },  { 2, "A2" },  { 3, "A3" },  { 4, "A4" },   { 5, "A5" },   { 6, "A6" },  { 7, "A7" },
        { 9, "B1" },  { 10, "B2" }, { 11, "B3" }, { 12, "B4" },  { 14, "C1" },  { 15, "C2" }, { 16, "C3" },
        { 17, "C4" }, { 18, "MA1" }, { 19, "MA2" }, { 22, "MC" }, { 23, "R1" }, { 24, "R2" }, { 25, "R3" },
        { 26, "R4" }, { 27, "R5" }, { 28, "R6" } };
    const int32_t category = static_cast<int32_t>( physics["_vehiclecategory"].asInt() );
    std::string group = "empty";
    for ( const auto &entry : kCategories ) {
        if ( entry.first == category ) {
            group = entry.second;
        }
    }
    ui.setVariable( "/:cur_group", group );
    // /:cur_difficulty = _difficulty clamped to 1..5 (record +0x78 -> +0x140).
    ui.setVariableNumber( "/:cur_difficulty", Clamp( static_cast<double>( physics["_difficulty"].asInt() ), 1.0, 5.0 ) );
    ui.setVariableBool( "/:IsBike", false );
    ui.setVariableBool( "/:DLC_Locked", false );
    ui.setVariable( "/:cur_upgrade", "level0" );
    ui.setVariableBool( "/:isCarTunable", false );
    ui.setVariableBool( "/:isCarPaintable", physics["_isvalidcarpaintdeluxe"].asInt() != 0 );
    ui.setVariableBool( "/:isCarStickable", physics["_isvalidsticker"].asInt() != 0 );
    // Mileage (record +0x78): a DBCarShop slot record is created new
    // (FUN_009f2790 -> FUN_00abffd0): 0 km [CODE]. Km_i = km * 0.6215.
    const float km = 0.0f;
    // "%08d" km, "%01d" tenths ((km - int) * 10) [CODE 0x99e035..0x99e0b6].
    const float miles = km * kKmToMiles;
    ui.setVariable( "/:Km_m", Format( "%08d", static_cast<int32_t>( km ) ) );
    ui.setVariable( "/:Km_i", Format( "%08d", static_cast<int32_t>( miles ) ) );
    ui.setVariable( "/:Centaine_m", Format( "%01d", static_cast<int32_t>( ( km - static_cast<int32_t>( km ) ) * 10.0f ) ) );
    ui.setVariable( "/:Centaine_i",
                    Format( "%01d", static_cast<int32_t>( ( miles - static_cast<int32_t>( miles ) ) * 10.0f ) ) );
    ui.setVariable( "/:state_Vehicle", "car" );
    ui.setVariableNumber( "/:IsWindows", physics["_windows_type"].asInt() != 3 ? 1.0 : 0.0 );
    ui.setVariableNumber( "/:IsDoors", physics["_hasdoors"].asInt() != 0 ? 1.0 : 0.0 );
    const std::string price = car.Price > 0 ? formatMoney( car.Price ) : std::string( "--" );
    ui.setVariable( "/:CurrentPrice", price );
    ui.setVariable( "/:TotalPrice", price );
    ui.setVariable( "/:current_price", price );
    ui.setVariable( "/:TotalPriceStr", price );
    ui.setVariableNumber( "/:IsCarInStock", 2.0 );
    ui.setVariableNumber( "/:reward_type", 0.0 );
    ui.setVariableBool( "/:IsRewardCar", false );
    // Specs (FUN_0099dbd0 formats; unit texts by DB id).
    const int32_t power = static_cast<int32_t>( physics["_power_bhp"].asInt() );
    const int32_t powerRpm = static_cast<int32_t>( physics["_power_rpm"].asInt() );
    const int32_t torque = static_cast<int32_t>( physics["_torque_nm"].asInt() );
    const int32_t torqueRpm = static_cast<int32_t>( physics["_torque_rpm"].asInt() );
    const int32_t mass = static_cast<int32_t>( physics["_mass"].asInt() );
    const float accel = physics["_acceleration_0_100_kmh"].asFloat();
    const float maxSpeed = physics["_max_theorical_speed"].asFloat();
    // Units: DB texts read by FUN_0099dbd0 (0x434 / 0x443 torque metric,
    // 0x442 / 0x2e8 imperial, 0x438 / 0x446 weight, 0x2e2 seconds, 0x3e1 /
    // 0x3e2 speed texts with the "SPEED" token (FUN_00999d00 / FUN_0099aa80),
    // 0x2e7 displacement, 0x1342 gears) and the literal "hp" [CODE]; the
    // choice between the language / unit branches of the code is [INF]: the
    // _m (metric) and _i (imperial) variables are both set.
    ui.setVariable( "/:cur_power_m", Format( "%d %s", power, "hp" ) );
    ui.setVariable( "/:cur_power_i", Format( "%d %s", power, "hp" ) );
    ui.setVariable( "/:cur_torque_m", Format( "%d %s @%d %s", torque, LocString( 0x434 ).c_str(), torqueRpm,
                                              LocString( 0x443 ).c_str() ) );
    // Imperial torque = Nm / 1.356 (DAT_00f4a7d0), rounded by FUN_006c8f00
    // [ENC -> INF: nearest].
    ui.setVariable( "/:cur_torque_i",
                    Format( "%d %s @%d %s", static_cast<int32_t>( std::lround( torque / 1.3559999465942383 ) ),
                            LocString( 0x442 ).c_str(), torqueRpm, LocString( 0x2e8 ).c_str() ) );
    ui.setVariable( "/:cur_weight_m", Format( "%d %s", mass, LocString( 0x438 ).c_str() ) );
    ui.setVariable( "/:cur_weight_i",
                    Format( "%d %s", static_cast<int32_t>( std::lround( mass / kKgToLb ) ), LocString( 0x446 ).c_str() ) );
    ui.setVariable( "/:cur_0_100_m", Format( "%.2f %s", accel, LocString( 0x2e2 ).c_str() ) );
    ui.setVariable( "/:cur_0_100_i", Format( "%.2f %s", accel * kZeroToSixty, LocString( 0x2e2 ).c_str() ) );
    auto speedText = []( int32_t textId, int32_t value ) {
        std::string text = LocString( textId );
        // FUN_00999b00: "\xA7" + token + "\xA7" (the 0xA7 of 0xee5a74
        // converted to UTF-8) replaced by the "%d" value [CODE].
        const std::string token = "\xC2\xA7SPEED\xC2\xA7";
        const size_t marker = text.find( token );
        if ( marker != std::string::npos ) {
            text.replace( marker, token.size(), std::to_string( value ) );
        }
        return text;
    };
    ui.setVariable( "/:cur_max_speed_m", speedText( 0x3e1, static_cast<int32_t>( maxSpeed ) ) );
    ui.setVariable( "/:cur_max_speed_i", speedText( 0x3e2, static_cast<int32_t>( maxSpeed * kKmToMiles ) ) );
    ui.setVariable( "/:cur_displacement_m", Format( "%d %s", static_cast<int32_t>( physics["_displacement"].asInt() ),
                                                    LocString( 0x2e7 ).c_str() ) );
    ui.setVariable( "/:cur_displacement_i", Format( "%d %s", static_cast<int32_t>( physics["_displacement"].asInt() ),
                                                    LocString( 0x2e7 ).c_str() ) );
    const int32_t gearbox = static_cast<int32_t>( physics["_gearbox"].asInt() );
    ui.setVariable( "/:cur_type_gears", LocString( gearbox == 2 ? 0x80d : gearbox == 3 ? 0x80c : 0x80a ) );
    ui.setVariable( "/:cur_nb_gears", Format( "%d %s", static_cast<int32_t>( physics["_nb_gears"].asInt() ),
                                              LocString( 0x1342 ).c_str() ) );
    // /:MotorPattern (_engine_localisation 1 -> 0, 2 -> 2, 3 -> 1),
    // /:WheelsPattern (_drive 1 -> 0, 3 -> 1, 4 -> 2) [CODE].
    const int32_t engine = static_cast<int32_t>( physics["_engine_localisation"].asInt() );
    ui.setVariableNumber( "/:MotorPattern", engine == 2 ? 2.0 : engine == 3 ? 1.0 : 0.0 );
    const int32_t drive = static_cast<int32_t>( physics["_drive"].asInt() );
    ui.setVariableNumber( "/:WheelsPattern", drive == 3 ? 1.0 : drive == 4 ? 2.0 : 0.0 );
    // Full data sheet (car_datas 'fd_open' -> data_full, sprite 5119:
    // /:cur_bhp_t_m / _i, /:cur_red_line_i, /:cur_type, /:cur_torque_i,
    // /:cur_displacement_i, /:cur_brakes_front_i / _rear_i, /:cur_tires_dim_f /
    // _r, /:cur_nb_gears, /:cur_type_gears, MotorPattern, WheelsPattern)
    // [DATA], set by FUN_0099dbd0 [CODE]. The unit branch: option 0 (metric,
    // [esp+0x3b]) and the language "us" (FUN_00ad9c60()[0] == 'u') use the
    // literal "hp" (0xf2bc78) instead of DB 0x443 "bhp" [CODE].
    const bool bUs = gpConfig != nullptr && !gpConfig->getLanguage().empty() && gpConfig->getLanguage()[0] == 'u';
    const std::string hp = bUs ? std::string( "hp" ) : LocString( 0x443 );
    const int32_t massLb = static_cast<int32_t>( std::lround( mass / kKgToLb ) );
    // "%.3f %s/%s": mass / power, "kg" (0x438) or "lb" (0x446) / hp [CODE 0x99e5d1..0x99e744].
    ui.setVariable( "/:cur_bhp_t_m", Format( "%.3f %s/%s", power > 0 ? static_cast<double>( static_cast<float>( mass ) / static_cast<float>( power ) ) : 0.0,
                                             LocString( 0x438 ).c_str(), hp.c_str() ) );
    ui.setVariable( "/:cur_bhp_t_i", Format( "%.3f %s/%s", power > 0 ? static_cast<double>( static_cast<float>( massLb ) / static_cast<float>( power ) ) : 0.0,
                                             LocString( 0x446 ).c_str(), hp.c_str() ) );
    // /:cur_type: _engine_type (+0x170) -> DB text (jump table 0x99f4fc) [CODE].
    static const std::pair<int32_t, int32_t> kEngineTypes[] = { { 1, 0x7f0 },  { 2, 0x7e5 },  { 3, 0x7ef },  { 4, 0x7ed },
                                                                { 5, 0x7e4 },  { 7, 0x7ec },  { 8, 0x7e7 },  { 9, 0x7e8 },
                                                                { 10, 0x7ee }, { 11, 0x7eb }, { 12, 0x7e6 }, { 13, 0x7e3 },
                                                                { 14, 0x7ea }, { 15, 0x1fb6 }, { 16, 0x7e9 }, { 17, 0x1fb3 } };
    int32_t engineText = 0x7e2;
    for ( const auto &entry : kEngineTypes ) {
        if ( entry.first == static_cast<int32_t>( physics["_engine_type"].asInt() ) ) {
            engineText = entry.second;
        }
    }
    ui.setVariable( "/:cur_type", LocString( engineText ) );
    // /:cur_red_line_m "%d %s" (power, hp) and /:cur_red_line_i "%d %s @%d %s"
    // (power, hp, +0x31c _power_rpm, rpm 0x2e8) [CODE, FUN_0099dbd0 "u" branch].
    ui.setVariable( "/:cur_red_line_m", Format( "%d %s", power, hp.c_str() ) );
    ui.setVariable( "/:cur_red_line_i", Format( "%d %s @%d %s", power, hp.c_str(), powerRpm, LocString( 0x2e8 ).c_str() ) );
    // /:cur_brakes_front_i / _rear_i: FUN_004d89b0 / FUN_004d8990 [ENC] on the
    // physics row -> the DB text of _brakes_characteristics_front_hc /
    // _rear_hc (0x7f2 / 0x7f3 for the Mustang) [INF].
    ui.setVariable( "/:cur_brakes_front_i", LocString( static_cast<int32_t>( physics["_brakes_characteristics_front_hc"].asUInt64() ) ) );
    ui.setVariable( "/:cur_brakes_rear_i", LocString( static_cast<int32_t>( physics["_brakes_characteristics_rear_hc"].asUInt64() ) ) );
    // /:cur_interior, /:cur_abs, /:cur_susp_f, /:cur_susp_r = "n/a" (0xf2be2c) [CODE].
    for ( const char *pName : { "/:cur_interior", "/:cur_abs", "/:cur_susp_f", "/:cur_susp_r" } ) {
        ui.setVariable( pName, "n/a" );
    }
    // Tires (FUN_00d8cc70(record rims +0x6c) = DBRims row): "%d/%d/%d"
    // (_largeur, _hauteur, _diameter) front (+0x38, +0x30, +0) and rear
    // (+0x3c, +0x34, +4); "n/a" without a rims row [CODE + DATA]. The "ZRF" /
    // "WR" variants are for the special cars of the function start (0x193 /
    // 0x194) [CODE].
    XmbValue rimsRow = FindRow( "DBRims", "_hashcode", static_cast<int64_t>( car.RimsHashcode ) );
    if ( rimsRow.isValid() ) {
        ui.setVariable( "/:cur_tires_dim_f", Format( "%d/%d/%d", static_cast<int32_t>( rimsRow["_largeur_front"].asInt() ),
                                                     static_cast<int32_t>( rimsRow["_hauteur_front"].asInt() ),
                                                     static_cast<int32_t>( rimsRow["_diameter_front"].asInt() ) ) );
        ui.setVariable( "/:cur_tires_dim_r", Format( "%d/%d/%d", static_cast<int32_t>( rimsRow["_largeur_rear"].asInt() ),
                                                     static_cast<int32_t>( rimsRow["_hauteur_rear"].asInt() ),
                                                     static_cast<int32_t>( rimsRow["_diameter_rear"].asInt() ) ) );
    } else {
        ui.setVariable( "/:cur_tires_dim_f", "n/a" );
        ui.setVariable( "/:cur_tires_dim_r", "n/a" );
    }
    // Quarter mile (+0x324 _quartermile_sec): 0 -> "n/a", else "%.1f %s" sec. [CODE].
    const float quarterMile = physics["_quartermile_sec"].asFloat();
    ui.setVariable( "/:cur_q_mile_m", quarterMile == 0.0f ? std::string( "n/a" ) : Format( "%.1f %s", quarterMile, LocString( 0x2e2 ).c_str() ) );
    ui.setVariable( "/:cur_q_mile_i", quarterMile == 0.0f ? std::string( "n/a" ) : Format( "%.1f %s", quarterMile, LocString( 0x2e2 ).c_str() ) );
    // Dimensions "%d mm\n%d mm\n%dmn": (+0x458 _width, +0x22c _length, +0x1fc
    // _height) and (+0x400 _track_front, +0x3fc _track_back, +0x454 _wheelbase) [CODE + DATA].
    ui.setVariable( "/:l_dim_01", Format( "%d mm\n%d mm\n%dmn", static_cast<int32_t>( physics["_width"].asInt() ),
                                          static_cast<int32_t>( physics["_length"].asInt() ),
                                          static_cast<int32_t>( physics["_height"].asInt() ) ) );
    ui.setVariable( "/:l_dim_02", Format( "%d mm\n%d mm\n%dmn", static_cast<int32_t>( physics["_track_front"].asInt() ),
                                          static_cast<int32_t>( physics["_track_back"].asInt() ),
                                          static_cast<int32_t>( physics["_wheelbase"].asInt() ) ) );
    // Gauges (FUN_00adef30 / FUN_00adeea0 / FUN_00adeaf0 / FUN_00adee20, upgrade
    // levels 0) [CODE].
    ui.setVariableNumber( "/:cur_accel", Gauge( accel, kAccelGauge ) );
    ui.setVariableNumber( "/:cur_speed", Gauge( maxSpeed, kSpeedGauge ) );
    ui.setVariableNumber( "/:cur_handling", Gauge( static_cast<float>( physics["_handling"].asInt() ), kHandlingGauge ) );
    ui.setVariableNumber( "/:cur_braking", Gauge( physics["_braking_100_0"].asFloat(), kBrakingGauge ) );
    OTDU_LOG_DEBUG( "SHOP: %s power %d @%d, torque %d @%d, %d kg, 0-100 %.2f s, %.0f km/h\n", car.Name.c_str(), power,
                    powerRpm, torque, torqueRpm, mass, accel, maxSpeed );
}

bool GMShop::buyOrEvaluate( bool bEvalOnly ) {
    // FUN_00ddec10(bEvalOnly) [CODE]: price (dealer vfunc 0x38 of the
    // selected car with the mode +0x1ba208 / +0x1ba21c / +0x1ba220 options, 0
    // for a reward car) against the money (+ the selected cars to sell,
    // FUN_009f7140: none here); a free garage place (FUN_00ab80b0(0, 0)) is
    // needed [INF: a new profile has one]. Enough: /:EnoughMoney 1, /:BuyOK 1,
    // /:PriceRed 0; not enough: /:EnoughMoney 3, "_Not enough money!",
    // /:BuyOK 0 (the 0 / 2 / 4 cases need cars to sell or a full garage).
    FlashPlayer &ui = scene.getUi();
    if ( hoveredCar < 0 ) {
        return false;
    }
    const ShopCar &car = cars[static_cast<size_t>( hoveredCar )];
    PlayerProfile &profile = GetPlayerProfile();
    const bool bEnough = static_cast<float>( car.Price ) <= static_cast<float>( profile.Money );
    if ( !bEnough ) {
        ui.setVariableNumber( "/:EnoughMoney", 3.0 );
        ui.setVariable( "/:Message", "_Not enough money!" );
        ui.setVariableNumber( "/:BuyOK", 0.0 );
        ui.setVariableNumber( "/:PriceRed", 1.0 );
        OTDU_LOG_INFO( "SHOP: eval %s: %d > money %d (not enough)\n", car.Name.c_str(), car.Price, profile.Money );
        return false;
    }
    ui.setVariableNumber( "/:EnoughMoney", 1.0 );
    ui.setVariableNumber( "/:BuyOK", 1.0 );
    ui.setVariableNumber( "/:PriceRed", 0.0 );
    if ( bEvalOnly ) {
        OTDU_LOG_INFO( "SHOP: eval %s: %d <= money %d\n", car.Name.c_str(), car.Price, profile.Money );
        return false;
    }
    // Purchase: SHOPSCAR closed (FUN_00d7fbe0 x2), FUN_00ada490 ->
    // FUN_00adc920: a garage record (FUN_00abffd0(car, colour, rims,
    // interior)), condition bytes +0x88..+0x8a (0: the slot car), money -=
    // price (FUN_004aaf70 / FUN_004aafd0), COLLECTION_CARS_DIF_n; then
    // FUN_00ddea60: the exit cinematic of the FREERIDE start DAT_0102582c =
    // "C_INSTALL_FROG" (story step < 11), the goal closed (FUN_00d61700), the
    // money display hidden, +0x1fa9 = 1: C_EXPLAIN_FROG once the walk mode is
    // off (DAT_010268b6, set by wmof) [CODE].
    OwnedCar owned;
    owned.IdCar = car.IdCar;
    owned.Color = car.Color;
    owned.Rims = car.Rims;
    owned.Interior = car.Interior;
    owned.Km = 0.0f;
    profile.Garage.push_back( owned );
    profile.Money -= car.Price;
    profile.CurrentGarageSlot = static_cast<int32_t>( profile.Garage.size() ) - 1;
    boughtCar = hoveredCar;
    OTDU_LOG_INFO( "SHOP: bought %s (car %d, colour %d '%s', rims %d) for %d, money %d, garage %zu car(s)\n",
                   car.Name.c_str(), car.IdCar, car.Color, car.ColorName.c_str(), car.Rims, car.Price, profile.Money,
                   profile.Garage.size() );
    updateMoneyVariables();
    scene.closeGoal();
    state = eState::Bought;
    return true;
}

void GMShop::startExplainFrog() {
    // FUN_00dde830 (dealer tick): +0x1fa9 && DAT_010268b0 == 0 &&
    // DAT_010268b6 -> C_EXPLAIN_FROG (+0x1ba334, CinProps |= 4: skippable)
    // started at the spot, listener = mode [CODE].
    scene.getUi().setClipVisible( "/cursor", false );
    walk.clearSelection( scene );
    scene.stopBackground( "C_FIRST_LOOP" );
    scene.setCameraOverride( nullptr );
    scene.playMain( "C_EXPLAIN_FROG", true );
    state = eState::ExplainFrog;
    OTDU_LOG_INFO( "SHOP: C_EXPLAIN_FROG\n" );
}

void GMShop::onCinematicEnd( const std::string &name ) {
    if ( name == introCinematic && state == eState::Intro ) {
        startWalk();
        return;
    }
    if ( state == eState::SelectableCinematic && name == selectableCinematic ) {
        // FUN_00dde890 (C_CHOICE_TODD, event 3) / camf: walk camera back
        // (FUN_0094fdb0), C_FIRST_LOOP again, walk on (FUN_0094fb90) [CODE].
        selectableCinematic.clear();
        scene.stopMain();
        scene.playBackground( "C_FIRST_LOOP", true );
        walk.resume( scene );
        bWalkInput = true;
        state = eState::Walk;
        return;
    }
    if ( name == "C_EXPLAIN_FROG" && state == eState::ExplainFrog ) {
        // Mode listener FUN_009f2eb0 event 6 of +0x1ba334: mode+0xc1 = 1, mode
        // := 1 FREERIDE; the step 7 tick (mode 1) -> vfunc8(8) "Tuto FROG"
        // (FUN_00b10da0 case 9: FREERIDE at the dealer exit FUN_00a6a650,
        // C_INSTALL_FROG of estol_b.bnk, pack_freerideFROG, timer 300 s, goal
        // TUTO_FROG) [CODE].
        state = eState::Done;
        PlayerProfile &profile = GetPlayerProfile();
        profile.StoryStep = 8;
        OTDU_LOG_INFO( "SHOP: end of C_EXPLAIN_FROG -> FREERIDE, story step 8 'Tuto FROG'\n" );
        pGameInstance->setNextGameMode( eGameMode::GM_FreeRide );
    }
}

void GMShop::onCommand( const std::string &command, const std::string &args ) {
    // SHOPSCAR FSCommands: the walk manager handler FUN_00952fa0 (camn, camf,
    // wmof, wmon, shou), the used dealer handler FUN_00ddf4c0 (eval, buyc /
    // buin, stci, shca, wrck) and the mode handler FUN_009f3d00 (gofr,
    // tstk...) [CODE].
    OTDU_LOG_INFO( "SHOP: FSCommand %s %s\n", command.c_str(), args.c_str() );
    if ( command == "wmof" ) {
        // FUN_0094fb30: walk mode off (+0xd1d = 1), DAT_010268b6 = 1.
        bWalkInput = false;
        bWalkOffSinceBuy = true;
    } else if ( command == "wmon" ) {
        // FUN_0094fb90: selection cleared (FUN_0094f440(-1)), walk on.
        bWalkInput = true;
        walk.clearSelection( scene );
    } else if ( command == "eval" ) {
        buyOrEvaluate( true );
    } else if ( command == "buyc" || command == "buin" ) {
        pendingCommands.push_back( "buy" );
    } else if ( command == "camn" ) {
        // FUN_00952300: the selected item's camera (vf20) and cinematic
        // (vf24, FpSelectable Cinematic: NPC_Todd_area -> C_CHOICE_TODD,
        // NPC_Jude_area -> C_JUDE_FRONTEND): C_FIRST_LOOP stopped, the
        // cinematic played at the spot. A car wrapper has none [INF].
        const char *pType = walk.getSelectedType( scene );
        if ( strcmp( pType, "npc_receiver" ) == 0 ) {
            pendingCommands.push_back( "cine:C_CHOICE_TODD" );
        } else if ( strcmp( pType, "stolencar" ) == 0 ) {
            pendingCommands.push_back( "cine:C_JUDE_FRONTEND" );
        }
    } else if ( command == "camf" ) {
        // FUN_0094fdb0 + FUN_00950a10 + FUN_00a91a80.
        pendingCommands.push_back( "camf" );
    } else if ( command == "stci" ) {
        // FUN_00ddf4c0 'stci': the loop +0xb440 stopped (FUN_00b3fc40(1)).
        pendingCommands.push_back( "stci" );
    } else if ( command == "dbxo" ) {
        // FUN_00d80ba0 'dbxo': /:Msg1Txt, /:Msg2Txt, /:Msg1Type, /:Msg2Type
        // and /:YesNo of the source movie copied to GENERAL, /:DialogBoxSource
        // = source movie name, GENERAL DialogBox() [CODE].
        FlashPlayer &ui = scene.getUi();
        FlashPlayer &general = scene.getGeneral();
        for ( const char *pVariable : { "/:Msg1Txt", "/:Msg2Txt", "/:Msg1Type", "/:Msg2Type", "/:YesNo" } ) {
            general.setVariableValue( pVariable, ui.getVariableValue( pVariable ) );
        }
        general.setVariable( "/:DialogBoxSource", "SHOPSCAR" );
        scene.setDialogBoxVisible( true );
        bDialogOpen = true;
        general.invoke( "/", "DialogBox", {} );
    } else if ( command == "dbxc" ) {
        // FUN_00d80ba0 'dbxc': GENERAL /:PlayerAnswer copied to the source
        // movie (/:DialogBoxSource), source DialogBox() [CODE].
        FlashPlayer &ui = scene.getUi();
        FlashPlayer &general = scene.getGeneral();
        bDialogOpen = false;
        scene.setDialogBoxVisible( false );
        OTDU_LOG_INFO( "SHOP: dialog answer '%s'\n", general.getVariable( "/:PlayerAnswer" ).c_str() );
        ui.setVariableValue( "/:PlayerAnswer", general.getVariableValue( "/:PlayerAnswer" ) );
        ui.invoke( "/", "DialogBox", {} );
    } else if ( command == "moup" ) {
        // FUN_0099c490 'moup': money display refresh (FUN_00abd0d0).
        updateMoneyVariables();
    } else if ( command == "rqca" ) {
        enterCar( hoveredCar );
    } else if ( command == "cain" ) {
        // FUN_009f3d00 'cain': mode +0x1ba174 = the selected slot (+0x1ae9b8);
        // walk manager: casino only (mode 0x1d) [CODE].
        OTDU_LOG_INFO( "SHOP: in the car (slot %d)\n", seatedCar >= 0 ? cars[static_cast<size_t>( seatedCar )].Slot : -1 );
    } else if ( command == "caou" ) {
        exitCar();
    } else if ( command == "caca" ) {
        // 'caca' (B() while loading): DAT_01025608 + 0x1a94c = 0 (request
        // cancelled) [CODE].
        exitCar();
    } else if ( command == "mcou" ) {
        // FUN_00d82692 'mcou': mouse cursor flag off (DAT_00f80168 +0x4604 ->
        // +0x24 = 0) [CODE]; nothing to do here.
    } else if ( command == "comp" ) {
        compareSelected();
    } else if ( command == "rsco" ) {
        // 'rsco' (car_datas 'cp_out'): no SHOP handler (only FUN_00a712d4 of
        // another mode -> FUN_00b957c0); the record stays [CODE].
    } else if ( command == "tstk" ) {
        pendingCommands.push_back( "tstk" );
    } else if ( command == "gofr" ) {
        // FUN_009f3d00 'gofr' -> FUN_004cc860 [ENC]: leave the shop. The
        // tutorial movie does not offer it (/:IsTutorial).
        OTDU_LOG_INFO( "SHOP: gofr (leave the shop) ignored during the tutorial\n" );
    }
}

bool GMShop::loadCockpitCamera( int32_t cameraId ) {
    // database/camerasingame.xmb "Car n" of the car's _camera, the camera of
    // mode 23 (cockpit) [DATA, as FIRST_WM]. The shop in-car view
    // FUN_00a50550 takes the slot camera (FUN_00ca0cf0(+0x34[slot]), FOV
    // FUN_00ca0d90(1.0)) and builds the eye from the car node STEER_HR [CODE;
    // the matrix helpers FUN_0044f4c0 / FUN_0042b4f0 are ENC] -> the cockpit
    // camera of the car [INF].
    XmbDocument document;
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "database/camerasingame.xmb" ).c_str(), data ) ||
         !document.load( data ) ) {
        return false;
    }
    XmbValue root = document.getRoot()["CameraInGame"];
    for ( int32_t c = 0; c < 400; c++ ) {
        XmbValue entry = root[( "Car " + std::to_string( c ) ).c_str()];
        if ( !entry.isValid() || entry["id_car"].asInt() != cameraId ) {
            continue;
        }
        const int32_t count = static_cast<int32_t>( entry["nb_cameras"].asInt() );
        for ( int32_t k = 0; k < count; k++ ) {
            XmbValue camera = entry[( "Camera " + std::to_string( k ) ).c_str()];
            if ( !camera.isValid() || camera["mode"].asInt() != 23 ) {
                continue;
            }
            cockpit.Position = Vector3f( camera["fixed_pos_x"].asFloat(), camera["fixed_pos_y"].asFloat(), camera["fixed_pos_z"].asFloat() );
            cockpit.Target = Vector3f( camera["fixed_targ_x"].asFloat(), camera["fixed_targ_y"].asFloat(), camera["fixed_targ_z"].asFloat() );
            cockpit.Fov = camera["fov"].asFloat( 55.0f * kDegToRad ); // radians (California: 0.96)
            cockpit.Near = camera["nearProjPlane"].asFloat( 0.05f );
            OTDU_LOG_INFO( "SHOP: cockpit camera of camera set %d: pos (%.3f %.3f %.3f) target (%.3f %.3f %.3f) fov %.1f\n",
                           cameraId, cockpit.Position.x, cockpit.Position.y, cockpit.Position.z, cockpit.Target.x,
                           cockpit.Target.y, cockpit.Target.z, cockpit.Fov / kDegToRad );
            return true;
        }
    }
    return false;
}

void GMShop::enterCar( int32_t carIndex ) {
    // FSCommand rqca: walk manager FUN_00952f50 (avatar of the walk hidden,
    // +0xd62 = 1) and mode FUN_009f3d00 -> FUN_008c1880(DAT_01025608 +
    // 0x1a898, selected slot, player): the slot car must be loaded
    // (+0x79bc, +0x3d0, +0x7460); seat choice FUN_008c1020 (the driver seat:
    // +0xac = 0, /:IsPassenger false); FUN_008c12f0: FUN_00a503c0 (shop car
    // doors / lights FUN_00a49370), FUN_00a512d0, FUN_00a519b0(slot, 0): the
    // player avatar (FUN_00b83860, +0x9c |= 1) is the driver of the slot car
    // (FUN_00dc8c50 current car, FUN_00b9fa00), FUN_00a50550: in-car camera;
    // then SHOPSCAR /:IsPassenger false, /:TimeOut false, /:CanEnterCar true
    // (the enter_car loader cmon_loader waits for _root.CanEnterCar, then
    // endLoading: help "in_car", FSCommands cain / shin) [CODE + DATA].
    if ( carIndex < 0 || cars[static_cast<size_t>( carIndex )].ShowCar < 0 ) {
        return;
    }
    const ShopCar &car = cars[static_cast<size_t>( carIndex )];
    XmbValue physics = gpDatabase->getDocument().getRoot().getArrayElement( "DBCarPhysicsData", static_cast<uint32_t>( car.PhysicsRow ) );
    const int32_t cameraId = static_cast<int32_t>( physics["_camera"].asInt() );
    if ( !loadCockpitCamera( cameraId != 0 ? cameraId : car.IdCar ) ) {
        OTDU_LOG_WARN( "SHOP: no cockpit camera for car %d (defaults)\n", car.IdCar );
    }
    seatedCar = carIndex;
    lookYaw = 0.0f;
    lookPitch = 0.0f;
    scene.setSeatedShowCar( car.ShowCar );
    scene.setPlayerSeated( true );
    state = eState::InCar;
    updateCockpitCamera( 0.0f );
    FlashPlayer &ui = scene.getUi();
    ui.setVariableBool( "/:IsPassenger", false );
    ui.setVariableBool( "/:TimeOut", false );
    ui.setVariableBool( "/:CanEnterCar", true );
    OTDU_LOG_INFO( "SHOP: entered %s (slot %d) on the driver seat\n", car.Name.c_str(), car.Slot );
}

void GMShop::exitCar() {
    // FSCommand caou: walk manager FUN_00952f00 (+0xd62 = 0, FUN_0094fdb0:
    // walk camera back) and mode FUN_009f3d00 -> FUN_008c11f0(player,
    // FUN_00a4f710 = driver (avatar +0x230 == 2000)): FUN_008c11a0 ->
    // FUN_00a517d0 (avatar out of the car, state 1000 walk, previous current
    // car back) [CODE].
    if ( seatedCar < 0 ) {
        return;
    }
    scene.setPlayerSeated( false );
    scene.setSeatedShowCar( -1 );
    scene.setCameraOverride( nullptr );
    walk.resume( scene );
    OTDU_LOG_INFO( "SHOP: out of %s\n", cars[static_cast<size_t>( seatedCar )].Name.c_str() );
    seatedCar = -1;
    state = eState::Walk;
}

void GMShop::updateCockpitCamera( float deltaTime ) {
    Matrix4x4f carWorld;
    if ( seatedCar < 0 || !scene.getShowCarWorld( static_cast<size_t>( cars[static_cast<size_t>( seatedCar )].ShowCar ), carWorld ) ) {
        return;
    }
    // Look around: mouse / right stick / menu left-right [INF: the in-car
    // look input of the shop is not in the readable code], yaw +-120 deg,
    // pitch +-45 deg [INF].
    float yawInput = gpInput->getMouseDeltaX() * 0.004f + gpInput->getRightStickX() * 2.0f * deltaTime;
    float pitchInput = -gpInput->getMouseDeltaY() * 0.004f - gpInput->getRightStickY() * 1.5f * deltaTime;
    if ( gpInput->isDown( eInputAction::IA_MenuLeft ) ) {
        yawInput -= 1.5f * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_MenuRight ) ) {
        yawInput += 1.5f * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_MenuUp ) ) {
        pitchInput += 1.0f * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_MenuDown ) ) {
        pitchInput -= 1.0f * deltaTime;
    }
    lookYaw = Clamp( lookYaw + yawInput, -120.0f * kDegToRad, 120.0f * kDegToRad );
    lookPitch = Clamp( lookPitch + pitchInput, -45.0f * kDegToRad, 45.0f * kDegToRad );
    const Vector3f eye = carWorld.transformPoint( cockpit.Position );
    const Vector3f direction = ( carWorld.transformPoint( cockpit.Target ) - eye ).normalized();
    // Eden convention: forward = R(q*) (0, 0, 1), q* = Ry(yaw) Rx(-pitch)
    // (GMFirstWalkMode::updateCockpitCamera).
    const float yaw = std::atan2( direction.x, direction.z ) - lookYaw;
    const float pitch = Clamp( std::asin( Clamp( direction.y, -1.0f, 1.0f ) ) + lookPitch, -1.4f, 1.4f );
    const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                  Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
    EdenSceneView::Camera camera = scene.getCamera();
    camera.Position = eye;
    camera.Rotation = Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
    camera.Fov = cockpit.Fov;
    camera.Near = cockpit.Near;
    scene.setCameraOverride( &camera );
}

void GMShop::compareSelected() {
    // FSCommand comp (car_datas 'cp_in'): FUN_00b95840(DAT_01025608 + 0x9bb4,
    // mode + 0x1ba1b0 = the record of the selected slot car, FUN_009f2790)
    // then FUN_00b959e0 (the /:comp_* variables) [CODE]. The compared car is
    // the shop car selected when Compare is validated; the next car under
    // the reticle is shown against it (car_datas 'cp_open' -> SetDatasCp)
    // [DATA]. The player's garage cars are not involved [CODE].
    if ( hoveredCar < 0 ) {
        return;
    }
    const ShopCar &car = cars[static_cast<size_t>( hoveredCar )];
    XmbValue physics = gpDatabase->getDocument().getRoot().getArrayElement( "DBCarPhysicsData", static_cast<uint32_t>( car.PhysicsRow ) );
    CompareRecord &record = GetCompareRecord();
    record = CompareRecord();
    record.bValid = true;
    record.Name = car.Name;
    record.Group = scene.getUi().getVariable( "/:cur_group" ); // FUN_00d8f5c0, as /:cur_group
    record.Upgrade = "level0"; // condition average of a slot record 0 -> "level0" [CODE]
    const float accel = physics["_acceleration_0_100_kmh"].asFloat();
    const float maxSpeed = physics["_max_theorical_speed"].asFloat();
    record.SpeedGauge = Gauge( maxSpeed, kSpeedGauge );
    record.AccelGauge = Gauge( accel, kAccelGauge );
    record.HandlingGauge = Gauge( static_cast<float>( physics["_handling"].asInt() ), kHandlingGauge );
    record.BrakingGauge = Gauge( physics["_braking_100_0"].asFloat(), kBrakingGauge );
    record.MaxSpeed = static_cast<int32_t>( maxSpeed );
    record.MaxSpeedMph = static_cast<int32_t>( maxSpeed * kKmToMiles ); // FUN_006c8f00 [ENC] -> as /:cur_max_speed_i [INF]
    record.ZeroToHundred = accel;
    record.ZeroToSixty = static_cast<float>( accel * kZeroToSixty );
    record.Power = static_cast<int32_t>( physics["_power_bhp"].asInt() );
    record.Difficulty = static_cast<int32_t>( Clamp( static_cast<double>( physics["_difficulty"].asInt() ), 1.0, 5.0 ) );
    OTDU_LOG_INFO( "SHOP: compare record = %s (speed %d accel %d braking %d, %d km/h, %.2f s, %d bhp)\n", record.Name.c_str(),
                   record.SpeedGauge, record.AccelGauge, record.BrakingGauge, record.MaxSpeed, record.ZeroToHundred, record.Power );
    writeCompareVariables();
}

void GMShop::writeCompareVariables() {
    // FUN_00b95e30: /:is_compare = record +0 ; FUN_00b959e0 when valid [CODE]:
    // /:Comp_car, /:comp_speed / accel / handling / braking (numbers),
    // /:comp_max_speed_m = DB 0x3e1 with SPEED = "%d" (+0x24), _i = 0x3e2
    // with +0x20, /:comp_0_100_m / _i = "%.1f %s" (+0x2c / +0x28, 0x2e2),
    // /:comp_power_m / _i = "%d %s" (+0x34 / +0x30, DB 0x443),
    // /:comp_difficulty, /:comp_group, /:comp_upgrade.
    const CompareRecord &record = GetCompareRecord();
    FlashPlayer &ui = scene.getUi();
    ui.setVariableBool( "/:is_compare", record.bValid );
    if ( !record.bValid ) {
        return;
    }
    auto speedText = []( int32_t textId, int32_t value ) {
        std::string text = LocString( textId );
        const std::string token = "\xC2\xA7SPEED\xC2\xA7";
        const size_t marker = text.find( token );
        if ( marker != std::string::npos ) {
            text.replace( marker, token.size(), std::to_string( value ) );
        }
        return text;
    };
    ui.setVariable( "/:Comp_car", record.Name );
    ui.setVariableNumber( "/:comp_speed", record.SpeedGauge );
    ui.setVariableNumber( "/:comp_accel", record.AccelGauge );
    ui.setVariableNumber( "/:comp_handling", record.HandlingGauge );
    ui.setVariableNumber( "/:comp_braking", record.BrakingGauge );
    ui.setVariable( "/:comp_max_speed_m", speedText( 0x3e1, record.MaxSpeed ) );
    ui.setVariable( "/:comp_max_speed_i", speedText( 0x3e2, record.MaxSpeedMph ) );
    ui.setVariable( "/:comp_0_100_m", Format( "%.1f %s", record.ZeroToHundred, LocString( 0x2e2 ).c_str() ) );
    ui.setVariable( "/:comp_0_100_i", Format( "%.1f %s", record.ZeroToSixty, LocString( 0x2e2 ).c_str() ) );
    ui.setVariable( "/:comp_power_m", Format( "%d %s", record.Power, LocString( 0x443 ).c_str() ) );
    ui.setVariable( "/:comp_power_i", Format( "%d %s", record.Power, LocString( 0x443 ).c_str() ) );
    ui.setVariableNumber( "/:comp_difficulty", record.Difficulty );
    ui.setVariable( "/:comp_group", record.Group );
    ui.setVariable( "/:comp_upgrade", record.Upgrade );
}

void GMShop::startTestDrive() {
    // FSCommand tstk (car_datas 'cd_close' end with _root.AfterValid ==
    // "test"): FUN_009f3b30 [CODE]: the selected slot car (FUN_009f2570) and
    // its slot record colour / rims / interior -> DAT_00fd92a0 /
    // DAT_00f77688..90, duration DAT_00f77694 = 120.0 (DAT_00edf610); the
    // passenger: shop type 0 "Hostess", 3 "Jude" -> the CPNJ entity of the
    // shop bank (FUN_00b34fd0(+0x1b6330, 'CPNJ', name)) +0xe0 (its config
    // name) or "shop_lamborghini" -> FUN_00dc2070: the DBPNJ row of that
    // _configid into DAT_00fdb450 (FUN_00cb5bd0); FUN_004cc860 [ENC]; mode
    // +0xc1 = 1; mode := 7 TESTCAR (previous mode 3).
    if ( hoveredCar < 0 ) {
        return;
    }
    const ShopCar &car = cars[static_cast<size_t>( hoveredCar )];
    TestDriveRequest &request = GetTestDriveRequest();
    request = TestDriveRequest();
    request.bActive = true;
    request.IdCar = car.IdCar;
    request.Color = car.Color;
    request.Rims = car.Rims;
    request.Interior = car.Interior;
    request.RimsHashcode = car.RimsHashcode;
    if ( car.ColorRow >= 0 ) {
        request.ColorId = gpDatabase->getDocument().getRoot().getArrayElement( "DBCarsAndColors", static_cast<uint32_t>( car.ColorRow ) )["_color_id_1"].asString();
    }
    request.Duration = 120.0f;
    request.ReturnMode = -1;
    request.Spot = kDealerSpot;
    // Shop type 3 (used car dealer, FUN_009ec8d0) -> "Jude"; the Entity__PNJ
    // "Jude" of istol_b: ConfigName "SPT_Jude" [DATA; entity +0xe0 = the
    // config name: INF].
    request.PassengerConfig = "shop_lamborghini";
    if ( const BankEntry *pEntities = scene.getSceneBank().findEntry( "entities.xmb" ) ) {
        XmbDocument entities;
        if ( entities.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
            for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__PNJ" ); i++ ) {
                XmbValue pnj = entityExport.getArrayElement( "ArrayOf_Entity__PNJ", i );
                if ( pnj["__Name"].asString() == "Jude" && !pnj["ConfigName"].asString().empty() ) {
                    request.PassengerConfig = pnj["ConfigName"].asString();
                }
            }
        }
    }
    OTDU_LOG_INFO( "SHOP: test drive of %s (car %d, colour %d, rims %d, interior %d), %.0f s, passenger %s -> TESTCAR\n",
                   car.Name.c_str(), car.IdCar, car.Color, car.Rims, car.Interior, request.Duration,
                   request.PassengerConfig.c_str() );
    state = eState::TestDrive;
    pGameInstance->setNextGameMode( eGameMode::GM_TestCar );
}

void GMShop::routeInput() {
    // Pad / keyboard -> SHOPSCAR: the buttons hold the key conditions
    // (DefineButton2: 13 -> A(), 14 / 15 -> Move up / down); B is the root
    // clip function of the focus clip _root.Clip [DATA]. The pad -> movie
    // routing itself is encrypted [ENC]: validate = key 13, back = B() on
    // /:Clip [INF].
    FlashPlayer &ui = scene.getUi();
    if ( bDialogOpen ) {
        // GENERAL dialog_box key catchers (ch449 / ch450): 13 -> A(),
        // 8 -> B() [DATA].
        FlashPlayer &general = scene.getGeneral();
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            general.sendKey( 13 );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            general.sendKey( 8 );
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( !ui.sendKey( 13 ) ) {
            ui.invokeOnClipVariable( "/:Clip", "A", {} );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        ui.invokeOnClipVariable( "/:Clip", "B", {} );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        ui.sendKey( 14 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        ui.sendKey( 15 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        ui.sendKey( 1 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        ui.sendKey( 2 );
    }
    if ( gpInput->wasMouseClicked() ) {
        ui.sendMouseRelease( gpInput->getMouseX(), gpInput->getMouseY() );
    }
}

void GMShop::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    // C_FIRST_INTRO / C_EXPLAIN_FROG are skippable (CinProps |= 4), not
    // C_INTRO_I (FUN_009f4ab0 sets the flag on C_FIRST_INTRO only) [CODE].
    const bool bSkippable = ( state == eState::Intro && introCinematic == "C_FIRST_INTRO" ) || state == eState::ExplainFrog;
    if ( bSkippable && scene.getMainTime() > 0.5f &&
         ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) ) {
        // C_FIRST_INTRO / C_EXPLAIN_FROG: CinProps |= 4 (skippable) [CODE]; the
        // skip input is encrypted -> validate / pause [INF].
        scene.skipMain();
    } else if ( state == eState::Walk || state == eState::InCar ) {
        routeInput();
    }
    scene.tick( deltaTime );
    for ( const std::string &name : scene.takeEndEvents() ) {
        onCinematicEnd( name );
    }
    std::vector<std::string> commands;
    commands.swap( pendingCommands );
    for ( const std::string &command : commands ) {
        if ( command == "buy" ) {
            buyOrEvaluate( false );
        } else if ( command.compare( 0, 5, "cine:" ) == 0 && state == eState::Walk ) {
            selectableCinematic = command.substr( 5 );
            scene.stopBackground( "C_FIRST_LOOP" );
            scene.setCameraOverride( nullptr );
            if ( scene.playMain( selectableCinematic, true ) ) {
                state = eState::SelectableCinematic;
            }
        } else if ( command == "camf" && state == eState::SelectableCinematic ) {
            onCinematicEnd( selectableCinematic );
        } else if ( command == "stci" ) {
            scene.stopBackground( "C_FIRST_LOOP" );
        } else if ( command == "tstk" && state == eState::Walk ) {
            startTestDrive();
        }
    }
    ProfileTickMoney( deltaTime );
    if ( state == eState::InCar ) {
        updateCockpitCamera( deltaTime );
    } else if ( state == eState::TestDrive ) {
        // FUN_009f3b30: mode := 7 TESTCAR (the switch happens next frame).
    } else if ( state == eState::Walk ) {
        walk.update( scene, deltaTime, bWalkInput );
        // Step 7 tick: shop ready, intro done (+0xb43c == 0), walk manager
        // active and the walk camera (type 9) -> pop_goal 4033, goal
        // TUTO_BUY_A_CAR (DBRewardXP 179), this[6] = 1 [CODE].
        if ( !bGoalShown ) {
            scene.showGoal( LocString( kLocBuyACar ) );
            bGoalShown = true;
        }
    } else if ( state == eState::Bought && bWalkOffSinceBuy ) {
        startExplainFrog();
    } else if ( state == eState::Bought && !bWalkOffSinceBuy ) {
        // The movie closes (scr_close) and turns the walk mode off; without
        // it, C_EXPLAIN_FROG starts once its close animation is over [INF].
        explainEnd += deltaTime;
        if ( explainEnd > 1.5f ) {
            startExplainFrog();
        }
    }
}

void GMShop::drawOffscreen() {
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( bLoaded && !bNo3D ) {
        scene.renderOffscreen();
    }
}

void GMShop::draw() {
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( bLoaded && !bNo3D ) {
        scene.draw();
    } else if ( bLoaded ) {
        scene.drawUi();
    }
}
