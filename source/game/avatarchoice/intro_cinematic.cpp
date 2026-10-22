#include "shared.h"
#include "intro_cinematic.h"

#include "audio/gs_audio.h"
#include "config/gs_config.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/flash_localization.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "core/xmb.h"
#include "render/eden/eden_planar_reflections.h"

#include <unordered_set>

namespace {
constexpr uint32_t kTrackAnimation = 0xdcd2e210; // "Animation"
// CinProps bits (intro_spec 7.2).
constexpr uint32_t kCinPropLoop = 0x2;
constexpr uint32_t kCinPropNoCamera = 0x10;

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

std::string SceneFileName( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    if ( dot != std::string::npos ) {
        file = file.substr( 0, dot );
    }
    return file;
}

uint32_t PackColor( const Vector3f &rgb, float alpha ) {
    auto channel = []( float value ) { return static_cast<uint32_t>( Clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f ); };
    return channel( rgb.x ) | ( channel( rgb.y ) << 8 ) | ( channel( rgb.z ) << 16 ) | ( channel( alpha ) << 24 );
}

} // namespace

IntroCinematic::IntroCinematic() {}

IntroCinematic::~IntroCinematic() { unload(); }

bool IntroCinematic::load() { return load( Setup() ); }

bool IntroCinematic::load( const Setup &newSetup ) {
    unload();
    setup = newSetup;
    const std::string bankPath = gpFile->getBankFolder() + setup.Bank;
    if ( !introBank.load( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: '%s' not found\n", bankPath.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( introBank, introBank.getPath().c_str() );
    introHashcodes.load( introBank );
    const BankEntry *pEntities = introBank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "IntroCinematic: entities.xmb not found\n" );
        return false;
    }
    loadDefinitions( entities );
    if ( definitions.empty() ) {
        OTDU_LOG_WARN( "IntroCinematic: no cinematic in '%s'\n", setup.Bank.c_str() );
        return false;
    }
    sounds.load( introBank );

    // Frontend intro.vmf: credits (generik1..5), "Select your character"
    // (label avatar), walk mode (wm_home).
    ui.setFSCommandCallback( [this]( const std::string &command, const std::string &args ) {
        if ( command == "sbdd" ) {
            // Handled by the engine after the action block (the movie sets
            // its own defaults right after the FSCommand, e.g. m_EnterCar_).
            bPendingUiLocalization = true;
        } else if ( commandCallback ) {
            commandCallback( command, args );
        }
    } );
    ui.load( gpFlash->getMovie( "intro" ) );
    if ( ui.isLoaded() ) {
        ApplyFlashLocalization( ui );
        bPendingUiLocalization = false;
        // FUN_00da92e0 / FUN_00da7f40: no casino save data; PC platform
        // (validate glyph, sprite 19).
        ui.setVariableBool( "/:IsCasinoSaveData", false );
        ui.setVariableNumber( "/:WhatPlatform", 1.0 );
        ui.setVariableNumber( "/:IsTransition", 1.0 );
        OTDU_LOG_DEBUG( "IntroCinematic: INTRO fake='%s' IsCasinoSaveData='%s' CasinoAvatar='%s'\n",
                        ui.getVariable( "/:fake" ).c_str(), ui.getVariable( "/:IsCasinoSaveData" ).c_str(),
                        ui.getVariable( "/:CasinoAvatar" ).c_str() );
    }
    // Cinematic start (FUN_00b3eba0, CinProps bit 4 clear): black stripes on,
    // only in the 4:3 screen ratio mode (DAT_00fe4c78 == 0).
    // GENERAL.vmf: letterbox and the objective box (pop_goal).
    general.setFSCommandCallback( [this]( const std::string &command, const std::string & ) {
        if ( command == "sbdd" ) {
            ApplyFlashLocalization( general );
        }
    } );
    if ( general.load( gpFlash->getMovie( "general" ) ) ) {
        ApplyFlashLocalization( general );
        if ( gpConfig->getVideoSettings().ScreenRatio == 0 ) {
            general.gotoLabel( "/black_stripes", "on", false );
        }
    }

    // Cinematic scene space -> island: the house spot (spots.bin), world =
    // local * RotationY(heading) * Translation(position) (D3D row vectors).
    Vector3f housePosition( 0.0f, 0.0f, 0.0f );
    float heading = 0.0f;
    if ( !setup.HouseSpot.empty() && !findHouseSpot( housePosition, heading ) ) {
        OTDU_LOG_WARN( "IntroCinematic: spot '%s' not found\n", setup.HouseSpot.c_str() );
    }
    sceneRotation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading );
    sceneOrigin = Matrix4x4f::Translation( housePosition ) * Matrix4x4f::FromQuaternion( sceneRotation );
    sounds.setSceneOrigin( sceneOrigin );
    const int32_t radius = CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    if ( !view.initialize( "ibiza", housePosition, setup.bWorld ? radius : -1 ) ) {
        return false;
    }
    view.setOceanEnabled( setup.bWorld );
    view.setDrawCallback(
        [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) { drawSets( renderer, pass ); } );
    loadSets( entities );
    loadPnjs( entities );
    loadVolumes( entities );
    loadLights( entities );
    collision = HavokCollision();
    for ( const BankEntry *pShk : introBank.findEntriesByExtension( "shk" ) ) {
        if ( LoadHavokCollision( pShk->pData, pShk->Size, collision, true ) ) {
            OTDU_LOG_INFO( "IntroCinematic: collision %s: %zu triangles, %zu boxes, %zu capsules\n",
                           pShk->Name.c_str(), collision.Indices.size() / 3, collision.Boxes.size(),
                           collision.Capsules.size() );
        }
        break;
    }
    applyShadowConfig( "CINE_DEFAULT" );
    camera = EdenSceneView::Camera();
    camera.Position = housePosition;
    bLoaded = true;
    return true;
}

void IntroCinematic::unload() {
    stop( main );
    stopBackground();
    sounds.stop( musicVoices );
    sounds.unload();
    ui.unload();
    general.unload();
    view.clearPlanarReflections();
    view.clearLightmapMaterials();
    view.destroy();
    car.unload();
    bCarEnvironment = false;
    entityPlacements.clear();
    cinCarHashcodes.clear();
    sets.clear();
    reflectSets.clear();
    props.clear();
    pnjs.clear();
    player = Pnj();
    animations.clear();
    entityCameras.clear();
    avatarLibrary.unload();
    if ( !exteriorBank.getEntries().empty() ) {
        gpRender->getTextureCache().releaseOwner( exteriorBank.getPath().c_str() );
    }
    exteriorBank.unload();
    if ( !introBank.getEntries().empty() ) {
        gpRender->getTextureCache().releaseOwner( introBank.getPath().c_str() );
    }
    introBank.unload();
    definitions.clear();
    endEvents.clear();
    weatherName.clear();
    modifierName.clear();
    volumes.clear();
    lights.clear();
    selectables.clear();
    goalTimer = -1.0f;
    bPlayerSeated = false;
    bDriverPose = false;
    driverBank.unload();
    bCameraOverride = false;
    bLoaded = false;
}

// Entity__Cinematic: CinFile[] rsc (subtype 16 .cin, 17 .sce), CinProps,
// DefaultShadowConfig, additionalsAnims[i].xan[0] and
// additionalsStyleAnims[s] { style; anims[i].xan[0] }.
void IntroCinematic::loadDefinitions( const XmbDocument &entities ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        auto pDef = std::make_unique<Definition>();
        pDef->Name = entry["__Name"].asString();
        pDef->CinProps = entry.getArraySize( "CinProps" ) > 0
                             ? static_cast<uint32_t>( entry.getArrayElement( "CinProps", 0 ).asInt() )
                             : 0u;
        pDef->ShadowConfig = entry["DefaultShadowConfig"].asString();
        const BankEntry *pCin = nullptr;
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ) && pCin == nullptr; j++ ) {
            XmbValue file = entry.getArrayElement( "CinFile", j );
            if ( file["rscSubType"].asInt() == 16 ) {
                pCin = introHashcodes.find( file["rsc"].asUInt64(), -1, 16 );
                if ( pCin == nullptr ) {
                    pCin = introHashcodes.find( file["rsc"].asUInt64() );
                }
            }
        }
        if ( pCin == nullptr || !pDef->Cin.parse( pCin->pData, pCin->Size ) ) {
            OTDU_LOG_WARN( "IntroCinematic: .cin of '%s' not found\n", pDef->Name.c_str() );
            continue;
        }
        // Scenes: the .sce named by the scene references (source path).
        for ( int32_t reference : pDef->Cin.getSceneReferences() ) {
            CinematicScene scene;
            if ( reference >= 0 && reference < static_cast<int32_t>( pDef->Cin.getReferences().size() ) ) {
                const std::string file = SceneFileName( pDef->Cin.getReferences()[reference].Text );
                const BankEntry *pScene = introBank.findEntry( file.c_str(), "sce" );
                if ( pScene == nullptr || !scene.parseScene( pScene->pData, pScene->Size ) ) {
                    OTDU_LOG_WARN( "IntroCinematic: scene '%s' not found\n", file.c_str() );
                }
            }
            pDef->Scenes.push_back( std::move( scene ) );
        }
        // Style anims: base additionalsAnims[i] -> anims[i] of the style.
        const uint32_t numAnims = entry.getArraySize( "additionalsAnims" );
        for ( uint32_t s = 0; s < entry.getArraySize( "additionalsStyleAnims" ); s++ ) {
            XmbValue style = entry.getArrayElement( "additionalsStyleAnims", s );
            const std::string styleName = style["style"].asString();
            const int32_t styleIndex = styleName == "m" ? 0 : styleName == "w" ? 1 : -1;
            if ( styleIndex < 0 ) {
                continue;
            }
            for ( uint32_t a = 0; a < numAnims && a < style.getArraySize( "anims" ); a++ ) {
                XmbValue base = entry.getArrayElement( "additionalsAnims", a );
                XmbValue styled = style.getArrayElement( "anims", a );
                if ( base.getArraySize( "xan" ) == 0 || styled.getArraySize( "xan" ) == 0 ) {
                    continue;
                }
                const uint64_t from = base.getArrayElement( "xan", 0 )["rsc"].asUInt64();
                const uint64_t to = styled.getArrayElement( "xan", 0 )["rsc"].asUInt64();
                if ( from != 0 && to != 0 ) {
                    pDef->StyleAnims[styleIndex][from] = to;
                }
            }
        }
        OTDU_LOG_DEBUG( "IntroCinematic: %s %.2f s, %zu shots, %zu scenes, props 0x%x\n", pDef->Name.c_str(),
                        pDef->Cin.getDuration(), pDef->Cin.getShots().size(), pDef->Scenes.size(), pDef->CinProps );
        definitions.push_back( std::move( pDef ) );
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu cinematics\n", definitions.size() );
}

const IntroCinematic::Definition *IntroCinematic::findDefinition( const std::string &name ) const {
    for ( const auto &pDef : definitions ) {
        if ( pDef->Name == name ) {
            return pDef.get();
        }
    }
    return nullptr;
}

const std::string &IntroCinematic::getMainName() const {
    static const std::string kEmpty;
    return main.pDef != nullptr ? main.pDef->Name : kEmpty;
}

// FUN_00b402a0 / FUN_00b3ff00: time = start time, messages and Fx at the
// start time dispatched.
void IntroCinematic::start( Playing &playing, const Definition *pDef, bool bLoop, bool bNoCamera, float startTime ) {
    stop( playing );
    playing.pDef = pDef;
    playing.bLoop = bLoop;
    playing.bNoCamera = bNoCamera;
    playing.Time = Clamp( startTime, 0.0f, Max( pDef->Cin.getDuration() - 0.001f, 0.0f ) );
    const bool bMain = &playing == &main;
    if ( bMain && !bNoCamera && !pDef->ShadowConfig.empty() ) {
        // FUN_00b3eba0: the entity shadow config (bit 4 clear).
        applyShadowConfig( pDef->ShadowConfig );
    }
    // A start time > 0 (-cinetime tests) is a seek: only the state messages
    // (weather, modifier, shadows) before it are applied.
    seekTime = playing.Time > 0.0f ? playing.Time : -1.0f;
    processMessages( playing, -1.0f, playing.Time, bMain );
    seekTime = -1.0f;
    if ( playing.Time <= 0.0f ) {
        updateFx( playing, -1.0f, playing.Time );
    }
    playing.PreviousTime = playing.Time;
}

void IntroCinematic::stop( Playing &playing ) {
    if ( !playing.Voices.empty() ) {
        sounds.stop( playing.Voices );
    }
    playing.Voices.clear();
    playing.pDef = nullptr;
    playing.bEnded = false;
    playing.Time = 0.0f;
    playing.PreviousTime = -1.0f;
}

bool IntroCinematic::playMain( const std::string &name, bool bStopPrevious ) {
    // FUN_00da7fd0: the previous main cinematic is stopped (no event 7).
    const Definition *pDef = findDefinition( name );
    if ( pDef == nullptr ) {
        OTDU_LOG_WARN( "IntroCinematic: cinematic '%s' not found\n", name.c_str() );
        return false;
    }
    (void)bStopPrevious;
    OTDU_LOG_DEBUG( "IntroCinematic: main %s\n", name.c_str() );
    start( main, pDef, ( pDef->CinProps & kCinPropLoop ) != 0, ( pDef->CinProps & kCinPropNoCamera ) != 0,
           main.pDef == nullptr && name == "C_INTRO" ? static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) )
                                                    : 0.0f );
    return true;
}

bool IntroCinematic::playBackground( const std::string &name, bool bLoop ) {
    // FUN_00da8040 (C_AVATAR) / FUN_00da8d90 (C_WALKMODE): CinProps |= 0x10,
    // loop forced.
    const Definition *pDef = findDefinition( name );
    if ( pDef == nullptr ) {
        OTDU_LOG_WARN( "IntroCinematic: cinematic '%s' not found\n", name.c_str() );
        return false;
    }
    OTDU_LOG_DEBUG( "IntroCinematic: background %s\n", name.c_str() );
    stopBackground( name );
    backgrounds.push_back( std::make_unique<Playing>() );
    start( *backgrounds.back(), pDef, bLoop, true, 0.0f );
    return true;
}

void IntroCinematic::stopMain() { stop( main ); }

void IntroCinematic::stopBackground() {
    for ( auto &pPlaying : backgrounds ) {
        stop( *pPlaying );
    }
    backgrounds.clear();
}

void IntroCinematic::stopBackground( const std::string &name ) {
    for ( auto it = backgrounds.begin(); it != backgrounds.end(); ) {
        if ( ( *it )->pDef != nullptr && ( *it )->pDef->Name == name ) {
            stop( **it );
            it = backgrounds.erase( it );
        } else {
            ++it;
        }
    }
}

bool IntroCinematic::isBackgroundPlaying( const std::string &name ) const {
    for ( const auto &pPlaying : backgrounds ) {
        if ( pPlaying->pDef != nullptr && pPlaying->pDef->Name == name ) {
            return true;
        }
    }
    return false;
}

void IntroCinematic::skipMain() {
    // Skip: jump to the end pose, the end is then reached on the next
    // update (listener event 7) [INF: the skip input path is encrypted].
    if ( main.pDef == nullptr ) {
        return;
    }
    sounds.stop( main.Voices );
    main.Time = Max( main.pDef->Cin.getDuration() - 0.001f, 0.0f );
    main.PreviousTime = main.Time;
    main.bLoop = false;
}

std::vector<std::string> IntroCinematic::takeEndEvents() {
    std::vector<std::string> result;
    result.swap( endEvents );
    return result;
}

// FUN_00b40350: t += dt; past duration - 0.001: end (event 7, stop) or loop
// (t = t <= duration ? 0.001 : t - duration, restart).
void IntroCinematic::advance( Playing &playing, float deltaTime, bool bMain ) {
    if ( playing.pDef == nullptr ) {
        return;
    }
    const Definition *pDef = playing.pDef;
    const float duration = pDef->Cin.getDuration();
    if ( duration <= 0.0f ) {
        // 0 s timeline (C_LIFE_SOUND): its Fx were started with it; nothing
        // to advance [INF: the restart of FUN_00b402f0 on such a timeline is
        // not traced].
        return;
    }
    const float from = playing.PreviousTime;
    float t = playing.Time + deltaTime;
    if ( t > duration - 0.001f ) {
        if ( !playing.bLoop ) {
            t = duration - 0.001f;
            processMessages( playing, from, t, bMain );
            updateFx( playing, from, t );
            playing.Time = t;
            playing.PreviousTime = t;
            endEvents.push_back( pDef->Name );
            // Stop(1), CinEnd 3: end pose kept until the listener starts
            // the next cinematic.
            sounds.stop( playing.Voices );
            playing.bEnded = true;
            return;
        }
        processMessages( playing, from, duration, bMain );
        updateFx( playing, from, duration );
        t = t <= duration ? 0.001f : t - duration;
        // Restart (FUN_00b402f0): the Fx of the cinematic start again.
        sounds.stop( playing.Voices );
        processMessages( playing, -1.0f, t, bMain );
        updateFx( playing, -1.0f, t );
    } else {
        processMessages( playing, from, t, bMain );
        updateFx( playing, from, t );
    }
    playing.Time = t;
    playing.PreviousTime = t;
}

void IntroCinematic::updateFx( Playing &playing, float from, float to ) {
    // .cin Fx events and the Fx tracks of the actors of the current scene
    // (voices of Tess in M_TESS_Tess.sce...).
    const Cinematic &cinematic = playing.pDef->Cin;
    sounds.update( cinematic, from, to, &playing.Voices );
    const Cinematic::Shot *pShot = cinematic.getShot( Max( to, 0.0f ) );
    if ( pShot == nullptr || pShot->Scene >= playing.pDef->Scenes.size() ) {
        return;
    }
    const Cinematic::Shot *pPreviousShot = from >= 0.0f ? cinematic.getShot( from ) : nullptr;
    const float sceneTime = pShot->getSceneTime( to );
    const float previousSceneTime =
        pPreviousShot == pShot && from >= 0.0f ? pShot->getSceneTime( from ) : pShot->CameraStart - 1e-4f;
    const CinematicScene &scene = playing.pDef->Scenes[pShot->Scene];
    sounds.updateSceneFx( scene, previousSceneTime, sceneTime, sceneOrigin * scene.getOrigin(), &playing.Voices );
}

void IntroCinematic::processMessages( const Playing &playing, float from, float to, bool bMain ) {
    // Only the main cinematic reaches the game mode listener (CinProps bit
    // 4 of the background cinematics).
    if ( !bMain || playing.pDef == nullptr ) {
        return;
    }
    const Cinematic &cinematic = playing.pDef->Cin;
    const auto &references = cinematic.getReferences();
    for ( const Cinematic::Event &event : cinematic.getMessages() ) {
        if ( !( event.Time > from && event.Time <= to ) && !( from < 0.0f && event.Time <= 0.0f && to >= 0.0f ) ) {
            continue;
        }
        if ( event.Reference < 0 || event.Reference >= static_cast<int32_t>( references.size() ) ) {
            continue;
        }
        const std::string &message = references[event.Reference].Text;
        if ( seekTime >= 0.0f && event.Time < seekTime - 0.5f &&
             ( message.rfind( "flsh:", 0 ) == 0 || message.rfind( "snd:", 0 ) == 0 ) ) {
            continue;
        }
        // Listener FUN_00da8040 (event 8), then FUN_009be090 -> FUN_00a91820.
        if ( message.rfind( "flsh:", 0 ) == 0 ) {
            const std::string label = message.substr( 5 );
            if ( label == "generik5" ) {
                // /:generik_date: localized string 0x139b.
                const char *pDate = gpDatabase != nullptr ? gpDatabase->getString( 0x139b ) : nullptr;
                ui.setVariable( "/:generik_date", pDate != nullptr ? pDate : "" );
            }
            ui.gotoLabel( "/", label, true );
        } else if ( message.rfind( "shad:", 0 ) == 0 ) {
            applyShadowConfig( message.substr( 5 ) );
        } else if ( message.rfind( "weat:", 0 ) == 0 ) {
            weatherName = message.substr( 5 );
            modifierName.clear();
        } else if ( message.rfind( "wmod:", 0 ) == 0 ) {
            modifierName = message.substr( 5 );
        } else if ( message == "snd:timestop" ) {
            if ( const auto *pSounds = sounds.findEmitter( "S_TimeStop" ) ) {
                sounds.play( *pSounds, Max( to - event.Time, 0.0f ) );
            }
        } else if ( message == "snd:music" ) {
            // The 4 party music emitters (looped).
            sounds.stop( musicVoices );
            for ( const char *pName : { "S_WALKMODE_Music", "S_WALKMODE_Music_01", "S_WALKMODE_Music_02",
                                        "S_WALKMODE_Music_03" } ) {
                if ( const auto *pSounds = sounds.findEmitter( pName ) ) {
                    sounds.play( *pSounds, Max( to - event.Time, 0.0f ), true, &musicVoices );
                }
            }
        }
    }
}

void IntroCinematic::stopMusic() { sounds.stop( musicVoices ); }

bool IntroCinematic::playEmitter( const char *pName ) {
    if ( const auto *pSounds = sounds.findEmitter( pName ) ) {
        sounds.play( *pSounds );
        return true;
    }
    return false;
}

void IntroCinematic::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    lastDeltaTime = deltaTime;
    if ( !main.bEnded ) {
        advance( main, deltaTime, true );
    }
    for ( auto &pPlaying : backgrounds ) {
        advance( *pPlaying, deltaTime, false );
    }
    for ( auto it = backgrounds.begin(); it != backgrounds.end(); ) {
        if ( ( *it )->bEnded ) {
            stop( **it );
            it = backgrounds.erase( it );
        } else {
            ++it;
        }
    }
    if ( goalTimer > 0.0f ) {
        // 30 s auto close (DAT_00ede1fc -> DAT_00f803b4).
        goalTimer -= deltaTime;
        if ( goalTimer <= 0.0f ) {
            closeGoal();
        }
    }
    ui.tick( deltaTime );
    if ( bPendingUiLocalization ) {
        ApplyFlashLocalization( ui );
        bPendingUiLocalization = false;
    }
    general.tick( deltaTime );
    view.tick( deltaTime );
}

void IntroCinematic::showGoal( const std::string &text ) {
    // FUN_00d61790: /:GoalTxt, pop_goal goto "open" (/:IsWalkmode false in
    // AVATAR_CHOICE: the non-WM animation), 30 s timer.
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariable( "/:GoalTxt", text );
    general.gotoLabel( "/pop_goal", "open", true );
    goalTimer = 30.0f;
}

void IntroCinematic::showTutorial( const std::string &text ) {
    // FUN_00d62ed0: FUN_00d62a90 sets /:TutText (the PC text _messpc_hc when
    // given and the device is not the X360 pad, else _mess_hc), then the
    // movie goes to "open_tuto" on /pop_tutorial (GENERAL.vmf sprite 647:
    // open_tuto_WM when _root.IsWalkMode; /:IsWalkmode stays false here, it
    // is only set for the "invi" message).
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariable( "/:TutText", text );
    general.gotoLabel( "/pop_tutorial", "open_tuto", true );
}

void IntroCinematic::closeTutorial() {
    // FUN_00d61600: pop_tutorial.close_popup().
    if ( general.isLoaded() ) {
        general.invoke( "/pop_tutorial", "close_popup", {} );
    }
}

bool IntroCinematic::getCarWorld( Matrix4x4f &outWorld ) const {
    return car.isLoaded() && computeCarWorld( outWorld );
}

bool IntroCinematic::getCarLocal( Matrix4x4f &outLocal ) const {
    Matrix4x4f world;
    if ( !getCarWorld( world ) ) {
        return false;
    }
    outLocal = sceneOrigin.inverse() * world;
    return true;
}

bool IntroCinematic::setPlayerSeated( bool bSeated ) {
    bPlayerSeated = bSeated;
    if ( !bSeated || bDriverPose ) {
        return bDriverPose || !bSeated;
    }
    // Entity__AvatarPlayer DriverMan / DriverWoman (avatarsdriver.bnk):
    // anim_blend_drive[0..6] = driveiklayer0..6.bas [DATA]. Layer 0 blends
    // stance_turn.xan (weight 1) with turn.xan (weight 0, the steering);
    // stance_turn.anm is the static wheel-straight pose (= turn.anm at
    // mid-clip, plus a ROOT key (0, -0.019, 0.059)) [DATA]. Layers 1 / 3
    // (stance.xan, empty), 2 (hand_size), 4 (hand_open, weight 0), 5 (gear
    // clips, weight 0) add nothing at rest; layer 6 holds the IK nodes
    // (updateSeatedPlayer) [DATA]. hand_size's sample time is not decoded:
    // the fingers keep the stance_turn grip [INF].
    const std::string path = gpFile->getBankFolder() + "scenarics/avatars/avatarsdriver.bnk";
    if ( driverBank.getEntries().empty() && !driverBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: '%s' not found\n", path.c_str() );
        return false;
    }
    const BankEntry *pStance = driverBank.findEntry( "stance_turn.anm" );
    bDriverPose = pStance != nullptr && driverPose.parse( pStance->pData, pStance->Size );
    bDriverIKLoaded = false;
    return bDriverPose;
}

// Euro/Bnk/DataBase/IKOffsetDB.bin (FUN_00b97cc0 load, FUN_00b96ba0 parse
// [CODE]; layout docs/re/driver_seat_spec.md). Big endian (the PC parser
// byte-swaps when the magic reads "BDKI") [CODE + DATA]:
//   +0x00 u16 2, u16 3, u32 0, u32 ?, "IKDB"
//   +0x10 chunk "CAIK", u16 2 (sexes), u16 0, u32, u32
//   +0x20 u32 numCars, then per car: u32 _id_car,
//         2 x { f32 seat[5]; 27 x { f32 offset[3]; f32 unused[3] } },
//         11 x f32[3]
//   then chunk "BIIK" (bikes).
// seat[] reads r1..r5 are stored at record +0x220, +0x228, +0x224, +0x230,
// +0x22c: the driver seat group moves by (0, +0x224, +0x228) = (0, r3, r2)
// (FUN_00b8ade0) [CODE]. Node offset = the first 3 floats (record entry
// +0x20, the one FUN_00b7e940 reads) [CODE]. Sex index 1 = woman
// (FUN_00cabec0() == 1) [INF: DBPNJ _sex 1 = woman].
bool IntroCinematic::loadDriverIKOffsets( int32_t idCar, bool bWoman ) {
    driverIK = DriverIKOffsets();
    std::vector<uint8_t> file;
    const std::string folder = gpFile->getBankFolder();
    if ( !gpFile->loadFile( ( folder + "DataBase/IKOffsetDB.bin" ).c_str(), file ) &&
         !gpFile->loadFile( ( folder + "database/ikoffsetdb.bin" ).c_str(), file ) ) {
        return false;
    }
    auto u32 = [&]( size_t offset ) {
        return static_cast<uint32_t>( file[offset] ) << 24 | static_cast<uint32_t>( file[offset + 1] ) << 16 |
               static_cast<uint32_t>( file[offset + 2] ) << 8 | file[offset + 3];
    };
    auto f32 = [&]( size_t offset ) {
        const uint32_t bits = u32( offset );
        float value;
        memcpy( &value, &bits, 4 );
        return value;
    };
    constexpr size_t kSexSize = 5 * 4 + 27 * 24;
    constexpr size_t kCarSize = 4 + 2 * kSexSize + 11 * 12;
    if ( file.size() < 0x24 || memcmp( file.data() + 0x0c, "BDKI", 4 ) != 0 ||
         memcmp( file.data() + 0x10, "KIAC", 4 ) != 0 ) {
        return false;
    }
    const uint32_t numCars = u32( 0x20 );
    for ( uint32_t c = 0; c < numCars && 0x24 + ( c + 1 ) * kCarSize <= file.size(); c++ ) {
        const size_t car = 0x24 + c * kCarSize;
        if ( static_cast<int32_t>( u32( car ) ) != idCar ) {
            continue;
        }
        const size_t sex = car + 4 + ( bWoman ? kSexSize : 0 );
        driverIK.SeatY = f32( sex + 8 );
        driverIK.SeatZ = f32( sex + 4 );
        for ( uint32_t n = 0; n < 27; n++ ) {
            const size_t entry = sex + 20 + n * 24;
            driverIK.Node[n] = Vector3f( f32( entry ), f32( entry + 4 ), f32( entry + 8 ) );
        }
        driverIK.bValid = true;
        return true;
    }
    return false;
}

void IntroCinematic::updateSeatedPlayer() {
    player.bVisible = false;
    Matrix4x4f carWorld;
    Matrix4x4f seat;
    if ( !player.Avatar || !bDriverPose || !getCarWorld( carWorld ) ||
         !car.getNodeModelTransform( "DVRSEAT_LR", seat ) ) {
        return;
    }
    if ( !bDriverIKLoaded ) {
        bDriverIKLoaded = true;
        const int32_t idCar = car.getIdCar();
        if ( !loadDriverIKOffsets( idCar, player.Avatar->isWoman() ) ) {
            OTDU_LOG_WARN( "IntroCinematic: no IKOffsetDB record for car %d (offsets 0)\n", idCar );
        }
    }
    const EdenSkeleton &skeleton = player.Avatar->getSkeleton();
    std::vector<BoneTransform> locals( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    driverPose.sample( skeleton, 0.0f, false, locals );

    // Avatar root (FUN_00b8ade0 / FUN_00b82450 [CODE]): car world with the
    // translation car * (DvrSeat node position + R_DvrSeat * IKOffsetDB
    // offset of DvrSeat), per car / per sex from IKOffsetDB.bin (0 when the
    // car has no record, FUN_00b97cc0). The seat group SEAT_*_L is also
    // moved by the stored (y, z) seat adjustment (+0x224 / +0x228 of the same
    // record) [CODE].
    // Seat group adjustment (0, y, z) moves DvrSeat and Back_L (children of
    // Seat_HR_L) [CODE]; FUN_0042b4f0's add-vs-set is [ENC]: added, since
    // the zero default must leave the seat in place [INF].
    const Vector3f seatAdjust( 0.0f, driverIK.SeatY, driverIK.SeatZ );
    seat = Matrix4x4f::Translation( seatAdjust + driverIK.Node[0] ) * seat; // + R_DvrSeat * offset (identity R)
    const Matrix4x4f seatInverse = seat.inverse();

    // IK goals (FUN_00b7e940 [CODE]): the car nodes Hand_*_L / Hand_*_R,
    // Foot_*_R / Foot_*_L and Back_*_L (+ R_node * their IKOffsetDB offsets),
    // weights 1 (+0x1df0 / +0x1df4 = 1.0 at seat-in, FUN_00b7fc74); chains of
    // driveiklayer6.bas [DATA]. Car nodes are read on the exterior model
    // (the _LR names; the interior _HR ones are at the same places [DATA]).
    // Node index in the IKOffsetDB 27-entry table (0xf7dbf0 LR / 0xf7db68 HR).
    auto goal = [&]( const char *pNode, int32_t index, Vector3f &out ) {
        Matrix4x4f node;
        if ( !car.getNodeModelTransform( pNode, node ) ) {
            return false;
        }
        Vector3f position = node.getTranslation();
        if ( index >= 0 ) {
            position += node.transformVector( driverIK.Node[index] );
        }
        if ( index == 6 ) {
            position += seatAdjust; // Back_L is in the seat group
        }
        out = seatInverse.transformPoint( position );
        return true;
    };
    auto bone = [&]( const char *pName ) { return skeleton.findBone( EdenNameHash4( pName ) ); };
    Vector3f target;
    if ( goal( "BACK_LR_L", 6, target ) ) {
        SolveAimIK( skeleton, locals, bone( "Spine" ), bone( "IK_Back" ), target );
    }
    if ( goal( "HAND_LR_R", 1, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "Arm_R" ), bone( "ForeArm_R" ), bone( "IK_Right_Hand" ), target,
                        bone( "Hand_R" ) );
    }
    if ( goal( "HAND_LR_L", 2, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "Arm_L" ), bone( "ForeArm_L" ), bone( "IK_Left_Hand" ), target,
                        bone( "Hand_L" ) );
    }
    if ( goal( "FOOT_LR_R", 3, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_R" ), bone( "Leg_R" ), bone( "IK_Right_Foot" ), target,
                        bone( "Foot_R" ) );
    }
    if ( goal( "FOOT_LR_L", 4, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_L" ), bone( "Leg_L" ), bone( "IK_Left_Foot" ), target,
                        bone( "Foot_L" ) );
    }
    skeleton.computeSkinMatrices( locals, player.WorldBones, player.Skin );
    player.World = carWorld * seat;
    player.bVisible = true;
    static int32_t logSeat = CmdLine::HasArg( "-logseat" ) ? 1 : 0;
    if ( logSeat > 0 ) {
        logSeat--;
        OTDU_LOG_INFO( "SEAT IKOffsetDB car %d valid %d seat (%.3f %.3f)\n", car.getIdCar(), driverIK.bValid ? 1 : 0,
                       driverIK.SeatY, driverIK.SeatZ );
        struct {
            const char *pBone, *pNode;
            int32_t Index;
        } pairs[] = { { "IK_Left_Hand", "HAND_LR_L", 2 }, { "IK_Right_Hand", "HAND_LR_R", 1 },
                      { "IK_Left_Foot", "FOOT_LR_L", 4 }, { "IK_Right_Foot", "FOOT_LR_R", 3 },
                      { "IK_Back", "BACK_LR_L", 6 },      { "Head", "DVRSEAT_LR", -1 } };
        for ( const auto &pair : pairs ) {
            const int32_t b = bone( pair.pBone );
            Vector3f t;
            if ( b >= 0 && goal( pair.pNode, pair.Index, t ) ) {
                const Vector3f p = player.WorldBones[b].getTranslation();
                OTDU_LOG_INFO( "SEAT %-14s (%.3f %.3f %.3f) goal %s (%.3f %.3f %.3f) dist %.3f\n", pair.pBone, p.x,
                               p.y, p.z, pair.pNode, t.x, t.y, t.z, ( p - t ).length() );
            }
        }
    }
    computeAvatarLighting( player.World.getTranslation(), player.Lighting );
}

void IntroCinematic::closeGoal() {
    // FUN_00d61700: pop_goal.close_popup().
    goalTimer = -1.0f;
    if ( general.isLoaded() ) {
        general.invoke( "/pop_goal", "close_popup", {} );
    }
}

void IntroCinematic::setLocalCamera( const Vector3f &position, const Quaternionf &heading, float fov ) {
    EdenSceneView::Camera local = camera;
    local.Position = sceneOrigin.transformPoint( position );
    local.Rotation = heading * Conjugate( sceneRotation );
    local.Fov = fov;
    setCameraOverride( &local );
}

bool IntroCinematic::getEntityCameraLocal( const std::string &name, Vector3f &outPosition,
                                           Quaternionf &outHeading ) const {
    auto it = entityCameras.find( name );
    if ( it == entityCameras.end() ) {
        return false;
    }
    outPosition = it->second.first;
    outHeading = it->second.second;
    return true;
}

const IntroCinematic::Volume *IntroCinematic::findVolume( const std::string &name ) const {
    for ( const Volume &volume : volumes ) {
        if ( volume.Name == name ) {
            return &volume;
        }
    }
    for ( const Volume &volume : selectables ) {
        if ( volume.Name == name ) {
            return &volume;
        }
    }
    return nullptr;
}

Vector3f IntroCinematic::Volume::toUnit( const Vector3f &p ) const {
    // D3D row vector: p' = (p, 1) * M.
    return Vector3f( p.x * Matrix[0][0] + p.y * Matrix[1][0] + p.z * Matrix[2][0] + Matrix[3][0],
                     p.x * Matrix[0][1] + p.y * Matrix[1][1] + p.z * Matrix[2][1] + Matrix[3][1],
                     p.x * Matrix[0][2] + p.y * Matrix[1][2] + p.z * Matrix[2][2] + Matrix[3][2] );
}

Vector3f IntroCinematic::Volume::toUnitDirection( const Vector3f &d ) const {
    return Vector3f( d.x * Matrix[0][0] + d.y * Matrix[1][0] + d.z * Matrix[2][0],
                     d.x * Matrix[0][1] + d.y * Matrix[1][1] + d.z * Matrix[2][1],
                     d.x * Matrix[0][2] + d.y * Matrix[1][2] + d.z * Matrix[2][2] );
}

bool IntroCinematic::Volume::contains( const Vector3f &p ) const {
    // FUN_00b40e50 (types 1..3).
    const Vector3f u = toUnit( p );
    switch ( OfType ) {
    case 1:
        return u.dot( u ) <= 1.0f;
    case 2:
        return u.x >= -0.5f && u.x <= 0.5f && u.y >= -0.5f && u.y <= 0.5f && u.z >= -0.5f && u.z <= 0.5f;
    case 3:
        return u.y >= -0.5f && u.y <= 0.5f && u.x * u.x + u.z * u.z <= 1.0f;
    default:
        return false;
    }
}

bool IntroCinematic::Volume::raycast( const Vector3f &origin, const Vector3f &direction, float maxDistance,
                                      float &outT ) const {
    // Slab test in the unit box (the selectable hit test vf04 is encrypted
    // [ENC]; the volume shape is the data).
    const Vector3f o = toUnit( origin );
    const Vector3f d = toUnitDirection( direction );
    float tMin = 0.0f;
    float tMax = maxDistance;
    const float origin3[3] = { o.x, o.y, o.z };
    const float direction3[3] = { d.x, d.y, d.z };
    for ( int32_t axis = 0; axis < 3; axis++ ) {
        if ( std::fabs( direction3[axis] ) < 1e-8f ) {
            if ( origin3[axis] < -0.5f || origin3[axis] > 0.5f ) {
                return false;
            }
            continue;
        }
        float t0 = ( -0.5f - origin3[axis] ) / direction3[axis];
        float t1 = ( 0.5f - origin3[axis] ) / direction3[axis];
        if ( t0 > t1 ) {
            std::swap( t0, t1 );
        }
        tMin = Max( tMin, t0 );
        tMax = Min( tMax, t1 );
        if ( tMin > tMax ) {
            return false;
        }
    }
    outT = tMin;
    return true;
}

void IntroCinematic::loadVolumes( const XmbDocument &entities ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    auto read = []( XmbValue entry, Volume &volume ) {
        volume.Name = entry["__Name"].asString();
        volume.OfType = static_cast<uint32_t>( entry["OfType"].asInt() );
        XmbValue matrix = entry["volume_mat"];
        const char *pRows[4] = { "_0", "_1", "_2", "_3" };
        const char *pColumns[4] = { "X", "Y", "Z", "W" };
        for ( int32_t r = 0; r < 4; r++ ) {
            for ( int32_t c = 0; c < 4; c++ ) {
                volume.Matrix[r][c] = matrix[pRows[r]][pColumns[c]].asFloat();
            }
        }
        XmbValue position = entry["orient_pos_scale"]["pos"];
        volume.Center = Vector3f( position["X"].asFloat(), position["Y"].asFloat(), position["Z"].asFloat() );
    };
    XmbValue volumeExport = entities.getRoot()["ExportType_Volume"];
    for ( uint32_t i = 0; i < volumeExport.getArraySize( "ArrayOf_Entity__Volume" ); i++ ) {
        Volume volume;
        read( volumeExport.getArrayElement( "ArrayOf_Entity__Volume", i ), volume );
        volumes.push_back( volume );
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Volume__FpSelectable" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Volume__FpSelectable", i );
        Volume volume;
        read( entry, volume );
        volume.SelectableType = static_cast<int32_t>( entry["Type"].asInt() );
        volume.SelectableString = entry["SelectableString"].asString();
        selectables.push_back( volume );
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu volumes, %zu selectables\n", volumes.size(), selectables.size() );
}

void IntroCinematic::setCameraOverride( const EdenSceneView::Camera *pCamera ) {
    bCameraOverride = pCamera != nullptr;
    if ( pCamera != nullptr ) {
        cameraOverride = *pCamera;
    }
}

bool IntroCinematic::getEntityCamera( const std::string &name, EdenSceneView::Camera &outCamera ) const {
    auto it = entityCameras.find( name );
    if ( it == entityCameras.end() ) {
        return false;
    }
    // Entity rot: same convention as the .sce camera Heading (forward =
    // R(q*) (0, 0, 1)) [INF: cam_entree_0N face the party, as c_fiesta_02].
    outCamera = camera;
    outCamera.Position = sceneOrigin.transformPoint( it->second.first );
    outCamera.Rotation = it->second.second * Conjugate( sceneRotation );
    return true;
}

void IntroCinematic::updateView() {
    if ( bCameraOverride ) {
        camera = cameraOverride;
    } else if ( main.pDef != nullptr && !main.bNoCamera ) {
        Vector3f position;
        Quaternionf rotation;
        float fov = camera.Fov;
        if ( main.pDef->Cin.getCamera( main.pDef->Scenes, main.Time, position, rotation, fov ) ) {
            // Scene space -> island.
            camera.Position = sceneOrigin.transformPoint( position );
            // Eden convention: the frame is R(conj q); world frame R_y R(conj q).
            camera.Rotation = rotation * Conjugate( sceneRotation );
            camera.Fov = fov;
        }
    }
    view.setCamera( camera );
    // Listener: the camera (FUN_00d944b0; Micro = 0 for these cinematics).
    if ( gpAudio != nullptr ) {
        const Vector3f forward = EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        cameraForward = forward;
        const float frameTime = Max( lastDeltaTime, 1e-4f );
        gpAudio->setListener( camera.Position, forward, ( camera.Position - previousListener ) * ( 1.0f / frameTime ) );
        previousListener = camera.Position;
    }
    view.setWeather( weatherName.empty()
                         ? nullptr
                         : view.getWeathers().findModified( weatherName.c_str(), modifierName.c_str() ) );
    Matrix4x4f carWorld;
    if ( car.isLoaded() && computeCarWorld( carWorld ) ) {
        view.setEnvironmentCenter( carWorld.getTranslation(), true );
    } else {
        view.setEnvironmentCenter( camera.Position, false );
    }
    // Draw ranges reach ~960 m with LOD factor 4 (spec_pmi_streaming 9.7).
    const float lodFactor = gpConfig->getVideoSettings().GlobalLodFactor;
    if ( setup.bWorld ) {
        view.getWorld().loadAround( camera.Position, lodFactor > 1.0f ? 2 : 1 );
    }
}

const CinematicScene::Object *IntroCinematic::findActor( const Playing &playing, uint64_t entityHashcode,
                                                         const char *pName, const CinematicScene *&outScene,
                                                         float &outSceneTime ) const {
    if ( playing.pDef == nullptr ) {
        return nullptr;
    }
    const Cinematic::Shot *pShot = playing.pDef->Cin.getShot( playing.Time );
    if ( pShot == nullptr || pShot->Scene >= playing.pDef->Scenes.size() ) {
        return nullptr;
    }
    const CinematicScene &scene = playing.pDef->Scenes[pShot->Scene];
    for ( const CinematicScene::Object &object : scene.getObjects() ) {
        if ( object.isCamera() ) {
            continue;
        }
        const bool bMatch = pName != nullptr ? object.Name == pName : object.EntityHashcode == entityHashcode;
        if ( bMatch ) {
            outScene = &scene;
            outSceneTime = pShot->getSceneTime( playing.Time );
            return &object;
        }
    }
    return nullptr;
}

std::vector<const IntroCinematic::Playing *> IntroCinematic::playingOrder() const {
    // Main cinematic first, then the most recent background ones.
    std::vector<const Playing *> order;
    order.push_back( &main );
    for ( auto it = backgrounds.rbegin(); it != backgrounds.rend(); ++it ) {
        order.push_back( it->get() );
    }
    return order;
}

void IntroCinematic::drawSets( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
    if ( pass == EdenSceneView::ePass::Environment ) {
        return;
    }
    // Projector actor layers: avatars and the car only.
    const bool bActorsOnly = pass == EdenSceneView::ePass::ProjectorActors;
    for ( const auto &pSet : sets ) {
        if ( bActorsOnly ) {
            break;
        }
        pSet->draw( renderer, sceneOrigin );
    }
    // Props: .sce actor of the main / background cinematic (same entity) or
    // the entity placement.
    for ( const Prop &prop : props ) {
        if ( bActorsOnly ) {
            break;
        }
        Matrix4x4f local = prop.Local;
        for ( const Playing *pPlaying : playingOrder() ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject = findActor( *pPlaying, prop.EntityHashcode, nullptr, pScene, sceneTime );
            if ( pObject != nullptr && pObject->findTrack( CinematicScene::kTrackPosition ) ) {
                local = Matrix4x4f::Translation( pObject->samplePosition( sceneTime ) ) *
                        EdenRotationFromQuaternion( pObject->sampleRotation( sceneTime ) );
                break;
            }
        }
        prop.Model->draw( renderer, sceneOrigin * local * prop.RootInverse );
    }
    for ( const Pnj &pnj : pnjs ) {
        if ( pnj.bVisible ) {
            pnj.Avatar->draw( renderer, pnj.World, pnj.Skin, &pnj.Lighting );
        }
    }
    if ( player.Avatar && player.bVisible ) {
        // Seated under the game camera (cockpit view, camera mode 23): the
        // head and what is worn on it are not drawn (the eye is inside the
        // head; the original seated view shows no head / hair) [INF: the
        // cockpit render branch FUN_00b75130 -> FUN_00b940a0(DAT_00ffbfd4)
        // was not decoded further].
        const bool bDrawHead = !( bPlayerSeated && bCameraOverride );
        player.Avatar->draw( renderer, player.World, player.Skin, &player.Lighting, bDrawHead );
    }
    Matrix4x4f carWorld;
    if ( car.isLoaded() && computeCarWorld( carWorld ) ) {
        car.draw( renderer, carWorld, view.getWeather(), bCarEnvironment );
    }
}

bool IntroCinematic::loadCar( const CinematicCar::Desc &desc, const std::string &restEntity,
                              const std::string &cinCarEntity ) {
    carRestEntity = restEntity;
    auto it = cinCarHashcodes.find( cinCarEntity );
    carEntityHashcode = it != cinCarHashcodes.end() ? it->second : 0;
    return car.load( desc );
}

bool IntroCinematic::computeCarWorld( Matrix4x4f &outWorld ) const {
    // .sce actor of the CinCar entity when a cinematic animates it, else the
    // rest pose (PosCam); the tires rest on the scene collision floor.
    Vector3f position;
    Quaternionf rotation;
    bool bPose = false;
    if ( carEntityHashcode != 0 ) {
        for ( const Playing *pPlaying : playingOrder() ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject = findActor( *pPlaying, carEntityHashcode, nullptr, pScene, sceneTime );
            if ( pObject != nullptr && pObject->findTrack( CinematicScene::kTrackPosition ) ) {
                position = pScene->getOrigin().transformPoint( pObject->samplePosition( sceneTime ) );
                rotation = pObject->sampleRotation( sceneTime );
                bPose = true;
                break;
            }
        }
    }
    if ( !bPose ) {
        auto it = entityPlacements.find( carRestEntity );
        if ( it == entityPlacements.end() ) {
            return false;
        }
        position = it->second.first;
        rotation = it->second.second;
    }
    const Matrix4x4f local = car.fitOnGround( position, rotation, [this]( float x, float z, float y, float &outHeight ) {
        return FindHavokFloor( collision, Vector3f( x, y, z ), 0.0f, 3.0f, outHeight );
    } );
    outWorld = sceneOrigin * local;
    return true;
}

void IntroCinematic::renderOffscreen() {
    if ( !bLoaded ) {
        return;
    }
    updateView();
    updatePnjs();
    if ( car.isLoaded() && !bCarEnvironment ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            car.assignEnvironment( *gpRender->getEdenRenderer(), pCube );
            bCarEnvironment = true;
        }
    }
    view.renderOffscreen();
}

void IntroCinematic::drawFade() {
    // Shot start transition (+0x14 type 1: fade from a colour over +0x18 s)
    // [INF].
    if ( main.pDef == nullptr ) {
        return;
    }
    const Cinematic::Shot *pShot = main.pDef->Cin.getShot( main.Time );
    if ( pShot == nullptr || pShot->FadeType != 1 || pShot->FadeDuration <= 0.0f ) {
        return;
    }
    const float elapsed = main.Time - pShot->Start;
    if ( elapsed < 0.0f || elapsed >= pShot->FadeDuration ) {
        return;
    }
    Renderer2D &renderer = gpRender->get2DRenderer();
    renderer.begin();
    renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ),
                       PackColor( pShot->FadeColor, 1.0f - elapsed / pShot->FadeDuration ) );
    renderer.end();
}

void IntroCinematic::draw() {
    if ( !bLoaded ) {
        return;
    }
    view.draw();
    drawFade();
    if ( general.isLoaded() && gpConfig->getVideoSettings().ScreenRatio == 0 && main.pDef != nullptr &&
         !main.bNoCamera && !bCameraOverride ) {
        general.drawClipOnly( gpRender->get2DRenderer(), "/black_stripes" );
    }
    if ( ui.isLoaded() ) {
        ui.draw( gpRender->get2DRenderer() );
    }
    if ( general.isLoaded() ) {
        general.drawClipOnly( gpRender->get2DRenderer(), "/pop_goal" );
        general.drawClipOnly( gpRender->get2DRenderer(), "/pop_tutorial" );
    }
}

bool IntroCinematic::findHouseSpot( Vector3f &outPosition, float &outHeading ) const {
    // islands/ibiza/level/spots.bin: ".SPT", "SPTA" { u32 size } then 0x110
    // byte records { u64 id; char bank[8]; vec3 position; f32; f32 heading;
    // ... }.
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/ibiza/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        return false;
    }
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    const uint64_t id = GetIdentifier64bit( setup.HouseSpot.c_str() );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size() && offset < 0x20 + size; offset += 0x110 ) {
        uint64_t recordId = 0;
        memcpy( &recordId, data.data() + offset, 8 );
        if ( recordId == id ) {
            memcpy( &outPosition, data.data() + offset + 0x10, 12 );
            memcpy( &outHeading, data.data() + offset + 0x20, 4 );
            OTDU_LOG_INFO( "IntroCinematic: house %s at (%.2f, %.2f, %.2f) heading %.4f\n", setup.HouseSpot.c_str(),
                           outPosition.x, outPosition.y, outPosition.z, outHeading );
            return true;
        }
    }
    return false;
}

namespace {
void ReadPlacement( XmbValue placement, Vector3f &position, Quaternionf &rotation, Vector3f &scale ) {
    rotation = Quaternionf( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                            placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
    position = Vector3f( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(), placement["pos"]["Z"].asFloat() );
    scale = Vector3f( placement["scale"]["X"].asFloat( 1.0f ), placement["scale"]["Y"].asFloat( 1.0f ),
                      placement["scale"]["Z"].asFloat( 1.0f ) );
}
} // namespace

// Models of a scene bank by entity (spec_pool_interior 6, P6):
// Scenery and Entity__Actor__Mesh (D_REFLECT) in the bank frame,
// Entity__Actor at entityTRS * rootLocal^-1 (the entity transform replaces
// the root node: the furniture roots sit at their absolute position),
// Entity__Actor__Interior (the views-through-windows LODs of an exterior
// bank) skipped when the real interior is loaded.
void IntroCinematic::loadBankModels( const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities,
                                     bool bSkipInteriorLods ) {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    auto loadModel = [&]( uint64_t rsc ) -> std::unique_ptr<EdenModel> {
        const BankEntry *pScene = hashcodes.find( rsc, 0, 0 );
        if ( pScene == nullptr ) {
            return nullptr;
        }
        const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
        const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        if ( pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            return nullptr;
        }
        // GSReflection projective slots, .UVA animations, NIGHTLMAP from the
        // weather (spec_pool_interior P2..P5).
        view.registerProjectiveMaterials( *pModel );
        pModel->bindUVAnimations( bank );
        view.registerLightmapMaterials( *pModel, bSkipInteriorLods ? EdenSceneView::eLightmapScope::Exterior
                                                                   : EdenSceneView::eLightmapScope::Interior );
        return pModel;
    };
    auto firstMesh = []( XmbValue entry, const char *pField ) -> uint64_t {
        for ( uint32_t j = 0; j < entry.getArraySize( pField ); j++ ) {
            XmbValue mesh = entry.getArrayElement( pField, j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                return mesh["rsc"].asUInt64();
            }
        }
        return 0;
    };
    XmbValue scenery = entities.getRoot()["ExportType_Scenery"];
    for ( uint32_t i = 0; i < scenery.getArraySize( "ArrayOf_Scenery" ); i++ ) {
        XmbValue entry = scenery.getArrayElement( "ArrayOf_Scenery", i );
        for ( uint32_t j = 0; j < entry.getArraySize( "Model" ); j++ ) {
            XmbValue model = entry.getArrayElement( "Model", j );
            if ( model["rscSubType"].asInt() != 0 ) {
                continue;
            }
            if ( auto pModel = loadModel( model["rsc"].asUInt64() ) ) {
                sets.push_back( std::move( pModel ) );
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor__Mesh" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Actor__Mesh", i );
        if ( auto pModel = loadModel( firstMesh( entry, "mesh" ) ) ) {
            // GSReflection projectors of the D_REFLECT scene (FUN_00d7e950).
            std::string bankName = bank.getPath();
            const size_t slash = bankName.find_last_of( "/\\" );
            if ( slash != std::string::npos ) {
                bankName = bankName.substr( slash + 1 );
            }
            view.registerReflectionScene( *pModel, sceneOrigin, bankName.c_str(),
                                          entry["AvatarReflectlayername1"].asString().c_str(),
                                          entry["AvatarReflectlayername2"].asString().c_str() );
            reflectSets.push_back( pModel.get() );
            sets.push_back( std::move( pModel ) );
        }
    }
    if ( !bSkipInteriorLods ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor__Interior" ); i++ ) {
            XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Actor__Interior", i );
            if ( auto pModel = loadModel( firstMesh( entry, "mesh" ) ) ) {
                sets.push_back( std::move( pModel ) );
            }
        }
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor", i );
        Prop prop;
        prop.Name = actor["__Name"].asString();
        // HOLOGRAMS: the LIVING mode icons; their visibility in the intro
        // is encrypted [ENC]: hidden [INF].
        if ( prop.Name == "HOLOGRAMS" ) {
            continue;
        }
        prop.Model = loadModel( firstMesh( actor, "mesh" ) );
        if ( !prop.Model ) {
            continue;
        }
        prop.EntityHashcode = actor["__Hc"].asUInt64();
        Vector3f position, scale;
        Quaternionf rotation;
        ReadPlacement( actor["orient_pos_scale"], position, rotation, scale );
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation ) *
                     Matrix4x4f::Scale( scale );
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                prop.RootInverse = node.Local.inverse();
                break;
            }
        }
        props.push_back( std::move( prop ) );
    }
}

void IntroCinematic::loadSets( const XmbDocument &entities ) {
    // GSReflection on/off per game mode (table 0xf817e0); the actor layers
    // (avatars in the mirror layers) on [INF: the intro-mode check is
    // encrypted; the original layer setup includes them].
    view.setPlanarReflectionsEnabled( EdenPlanarReflections::IsEnabledForGameMode( setup.GameModeId ) );
    view.setPlanarReflectionActors( true );
    loadBankModels( introBank, introHashcodes, entities, false );
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    // Entity__PosCam / Entity__CinCar placements (car rest pose).
    for ( const char *pArray : { "ArrayOf_Entity__PosCam", "ArrayOf_Entity__CinCar" } ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            XmbValue entry = entityExport.getArrayElement( pArray, i );
            Vector3f position, scale;
            Quaternionf rotation;
            ReadPlacement( entry["orient_pos_scale"], position, rotation, scale );
            entityPlacements[entry["__Name"].asString()] = std::make_pair( position, rotation );
            if ( strcmp( pArray, "ArrayOf_Entity__CinCar" ) == 0 ) {
                cinCarHashcodes[entry["__Name"].asString()] = entry["__Hc"].asUInt64();
            }
        }
    }
    // Entity__Camera (cam_entree_0N: walk mode start poses).
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Camera" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Camera", i );
        Vector3f position, scale;
        Quaternionf rotation;
        ReadPlacement( entry["orient_pos_scale"], position, rotation, scale );
        entityCameras[entry["__Name"].asString()] = std::make_pair( position, rotation );
    }
    // House exterior (spot bank), same frame; its interior LODs are hidden
    // (the intro bank carries the interior).
    const std::string exteriorPath = gpFile->getBankFolder() + setup.ExteriorBank;
    if ( !setup.ExteriorBank.empty() && exteriorBank.load( exteriorPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( exteriorBank, exteriorBank.getPath().c_str() );
        BankHashcodes exteriorHashcodes;
        exteriorHashcodes.load( exteriorBank );
        const BankEntry *pExteriorEntities = exteriorBank.findEntry( "entities.xmb" );
        XmbDocument exteriorEntities;
        if ( pExteriorEntities != nullptr && exteriorEntities.load( pExteriorEntities->pData, pExteriorEntities->Size ) ) {
            loadBankModels( exteriorBank, exteriorHashcodes, exteriorEntities, true );
        }
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu set models, %zu props, %zu cameras\n", sets.size(), props.size(),
                   entityCameras.size() );
}

void IntroCinematic::applyShadowConfig( const std::string &name ) {
    view.setShadowConfig( EdenShadows::FindConfig( name ) );
}

void IntroCinematic::loadPnjs( const XmbDocument &entities ) {
    if ( CmdLine::HasArg( "-nopnj" ) || !avatarLibrary.load() ) {
        return;
    }
    // The PNJ animated by a scene of one of the cinematics.
    std::unordered_set<uint64_t> used;
    for ( const auto &pDef : definitions ) {
        for ( const CinematicScene &scene : pDef->Scenes ) {
            for ( const CinematicScene::Object &object : scene.getObjects() ) {
                used.insert( object.EntityHashcode );
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__PNJ" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__PNJ", i );
        Pnj pnj;
        pnj.EntityHashcode = entry["__Hc"].asUInt64();
        pnj.Name = entry["__Name"].asString();
        if ( used.count( pnj.EntityHashcode ) == 0 ) {
            continue;
        }
        pnj.Avatar = std::make_unique<EdenAvatar>();
        if ( pnj.Avatar->load( avatarLibrary, entry["ConfigName"].asString().c_str() ) ) {
            pnjs.push_back( std::move( pnj ) );
        }
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu PNJ avatars\n", pnjs.size() );
}

bool IntroCinematic::setPlayerAvatar( const std::string &configId ) {
    // FUN_00da8d90: the player avatar rebuilt from the DBPNJ configuration.
    player = Pnj();
    player.Name = "Avatar";
    player.Avatar = std::make_unique<EdenAvatar>();
    if ( !avatarLibrary.isLoaded() && !avatarLibrary.load() ) {
        player.Avatar.reset();
        return false;
    }
    if ( !player.Avatar->load( avatarLibrary, configId.c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: player avatar '%s' not found\n", configId.c_str() );
        player.Avatar.reset();
        return false;
    }
    OTDU_LOG_INFO( "IntroCinematic: player avatar %s\n", configId.c_str() );
    return true;
}

const EdenAnimation *IntroCinematic::getAnimation( uint64_t hashcode ) {
    auto it = animations.find( hashcode );
    if ( it != animations.end() ) {
        return it->second.get();
    }
    std::unique_ptr<EdenAnimation> pAnimation;
    if ( const BankEntry *pEntry = introHashcodes.find( hashcode, 0, 8 ) ) {
        pAnimation = std::make_unique<EdenAnimation>();
        if ( !pAnimation->parse( pEntry->pData, pEntry->Size ) ) {
            pAnimation.reset();
        }
    }
    const EdenAnimation *pResult = pAnimation.get();
    animations[hashcode] = std::move( pAnimation );
    return pResult;
}

void IntroCinematic::animatePnj( Pnj &pnj, const Playing &playing, const CinematicScene &scene,
                                 const CinematicScene::Object &object, float sceneTime ) {
    // .sce actor: Position / Heading (actor root) and the Animation track
    // (avatar_spec 4.1): key k = last T_k <= t, clip time u = f2 - f7 +
    // (t - T_k), linear crossfade with the previous key during f7 seconds;
    // the clips loop. The clip reference is replaced by the style anim of
    // the avatar (additionalsStyleAnims "m" / "w").
    const EdenSkeleton &skeleton = pnj.Avatar->getSkeleton();
    std::vector<BoneTransform> locals( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    const auto &styleAnims = playing.pDef->StyleAnims[pnj.Avatar->isWoman() ? 1 : 0];
    const CinematicScene::Track *pTrack = object.findTrack( kTrackAnimation );
    if ( pTrack != nullptr && pTrack->NumComponents >= 10 && !pTrack->Times.empty() ) {
        auto keyField = [pTrack]( size_t key, uint32_t field ) {
            return pTrack->Values[key * pTrack->NumComponents + field];
        };
        auto keyRef = [&]( size_t key ) {
            uint32_t index = 0;
            const float value = keyField( key, 0 );
            memcpy( &index, &value, sizeof( index ) );
            return index;
        };
        auto samplePose = [&]( size_t key, std::vector<BoneTransform> &pose ) {
            const uint32_t ref = keyRef( key );
            if ( ref >= scene.getReferences().size() ) {
                return false;
            }
            uint64_t hashcode = scene.getReferences()[ref].Hashcode;
            auto style = styleAnims.find( hashcode );
            if ( style != styleAnims.end() ) {
                hashcode = style->second;
            }
            const EdenAnimation *pAnimation = getAnimation( hashcode );
            if ( pAnimation == nullptr ) {
                return false;
            }
            const float clipTime = keyField( key, 2 ) - keyField( key, 7 ) + ( sceneTime - pTrack->Times[key] );
            pAnimation->sample( skeleton, clipTime, true, pose );
            return true;
        };
        size_t key = 0;
        for ( size_t k = 0; k < pTrack->Times.size(); k++ ) {
            if ( pTrack->Times[k] <= sceneTime ) {
                key = k;
            }
        }
        std::vector<BoneTransform> current = locals;
        samplePose( key, current );
        const float blendIn = keyField( key, 7 );
        const float sinceKey = sceneTime - pTrack->Times[key];
        if ( key > 0 && blendIn > 0.0f && sinceKey < blendIn ) {
            std::vector<BoneTransform> previous = locals;
            if ( samplePose( key - 1, previous ) ) {
                BlendPoses( previous, current, Clamp( sinceKey / blendIn, 0.0f, 1.0f ), locals );
            } else {
                locals = current;
            }
        } else {
            locals = current;
        }
    }
    skeleton.computeSkinMatrices( locals, pnj.WorldBones, pnj.Skin );
    // ActorWorld = S * R(q*) * T(pos) * Origin.
    pnj.World = sceneOrigin * scene.getOrigin() * Matrix4x4f::Translation( object.samplePosition( sceneTime ) ) *
                EdenRotationFromQuaternion( object.sampleRotation( sceneTime ) );
    pnj.bVisible = true;
    computeAvatarLighting( pnj.World.getTranslation(), pnj.Lighting );
}

void IntroCinematic::updatePnjs() {
    // Each avatar: the actor of the main cinematic, else of the background
    // one, sampled at the scene time of its shot.
    auto update = [this]( Pnj &pnj, const char *pName ) {
        pnj.bVisible = false;
        if ( !pnj.Avatar ) {
            return;
        }
        for ( const Playing *pPlaying : playingOrder() ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject = findActor( *pPlaying, pnj.EntityHashcode, pName, pScene, sceneTime );
            if ( pObject != nullptr ) {
                animatePnj( pnj, *pPlaying, *pScene, *pObject, sceneTime );
                return;
            }
        }
    };
    for ( Pnj &pnj : pnjs ) {
        update( pnj, nullptr );
    }
    if ( bPlayerSeated ) {
        updateSeatedPlayer();
    } else {
        update( player, "Avatar" );
    }
}

void IntroCinematic::loadLights( const XmbDocument &entities ) {
    // Entity__Light (loader FUN_00def230, entry FUN_00a4c7a0 / FUN_00da07c0).
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    auto vec3 = []( XmbValue v ) { return Vector3f( v["X"].asFloat(), v["Y"].asFloat(), v["Z"].asFloat() ); };
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Light" ) && lights.size() < 64; i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Light", i );
        XmbValue placement = entry["orient_pos_scale"];
        const Quaternionf rotation( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                    placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        Light light;
        light.Position = sceneOrigin.transformPoint( vec3( placement["pos"] ) );
        // E+0x60 = normalize(-Y axis of the entity matrix).
        const Vector3f localDirection = rotation.rotate( Vector3f( 0.0f, -1.0f, 0.0f ) );
        light.Direction = sceneOrigin.transformVector( localDirection ).normalized();
        const float intensity = entry["Intensity"].asFloat( 1.0f );
        light.Ambient = vec3( entry["Ambient"] ) * intensity;
        light.Diffuse = vec3( entry["Diffuse"] ) * intensity;
        light.Specular = vec3( entry["Specular"] ) * intensity;
        const float drop = entry["DistanceDropOff"].asFloat();
        const float cut = entry["DistanceCutOff"].asFloat();
        // E+0x88 / E+0x8c = 2 x angle; cones use cos(angle / 2).
        const float cosOuter = std::cos( entry["AngularCutOff"].asFloat() );
        const float cosInner = std::cos( entry["AngularDropOff"].asFloat() );
        light.CosOuter = cosOuter;
        light.ConeScale = ( cosInner - cosOuter > 1.1920929e-7f ) ? 1.0f / ( cosInner - cosOuter ) : 8388608.0f;
        light.ConeBias = -cosOuter * light.ConeScale;
        // FUN_004bf410 is encrypted: linear fall-off from drop to cut [INF].
        if ( cut - drop > 1e-6f ) {
            light.DistanceA = cut / ( cut - drop );
            light.DistanceB = 1.0f / ( cut - drop );
        }
        light.bInterior = entry["isInterior"].asInt( 1 ) != 0;
        lights.push_back( light );
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu lights\n", lights.size() );
}

void IntroCinematic::computeAvatarLighting( const Vector3f &rootPosition, EdenAvatarLighting &out ) const {
    // FUN_00a4da90 (gathering, AVATAR_CHOICE / FIRST_WM: interior light
    // modes) -> FUN_00a4ca50 (camera light) -> FUN_00b87aa0 (useSun false in
    // these modes) -> FUN_00caf850 (CARINTEG / ATTEFACT, AVATAROUTCARLIGHTFACTOR).
    const WeatherPreset *pWeather = view.getWeather();
    const EdenSceneParams &sceneParams = view.getSceneParams();
    auto getFloat = [pWeather]( const char *pName, float value ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, value ) : value;
    };
    auto getVector3 = [pWeather]( const char *pName, const Vector3f &value ) {
        return pWeather != nullptr ? pWeather->getVector3( pName, value ) : value;
    };
    const float cameraFactor = getFloat( "avatar_cameralight_factor", 0.0f );
    const Vector4f influence = pWeather != nullptr
                                   ? pWeather->getVector4( "avatar_spotlight_interior_exterior_influence",
                                                           Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) )
                                   : Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    const float shadowInfluence = getFloat( "avatar_shadow_influence", 1.0f );
    const Vector3f sunDirection = sceneParams.SunDirection.normalized();
    // P: FUN_00b87100 (a skeleton point) is encrypted: root + (0, 1, 0) [INF].
    const Vector3f point = rootPosition + Vector3f( 0.0f, 1.0f, 0.0f );
    Vector3f direction = sunDirection;
    Vector3f ambient = sceneParams.Ambient;
    Vector3f diffuse = sceneParams.SunDiffuse;
    Vector3f specular = sceneParams.SunSpecular;
    if ( influence.x > 0.0f || influence.y > 0.0f || cameraFactor > 0.0f ) {
        // FUN_00a4beb0.
        float best = 1.17549435e-38f;
        float bestQ = 1.0f;
        int32_t bestIndex = -1;
        Vector3f accDirection, accAmbient, accDiffuse, accSpecular;
        for ( size_t i = 0; i < lights.size(); i++ ) {
            const Light &light = lights[i];
            const Vector3f d = point - light.Position;
            const float s = Clamp( d.dot( d ), 1.1920929e-7f, 100000.0f );
            const float lightInfluence = light.bInterior ? influence.x : influence.y;
            const float along = light.Direction.dot( d );
            if ( !( lightInfluence > 0.0f ) || !( along > 0.0f ) || along * along < light.CosOuter * light.CosOuter * s ) {
                continue;
            }
            const float length = std::sqrt( s );
            const Vector3f n = d * ( 1.0f / length );
            const float a = Clamp( light.DistanceA - light.DistanceB * length, 0.0f, 1.0f ) *
                            Clamp( light.ConeScale * light.Direction.dot( n ) + light.ConeBias, 0.0f, 1.0f ) *
                            lightInfluence;
            if ( !( a > 0.0f ) ) {
                continue;
            }
            const float q = s * s;
            accDirection = accDirection + n * ( a / q );
            accAmbient = accAmbient + light.Ambient * a;
            accDiffuse = accDiffuse + light.Diffuse * a;
            accSpecular = accSpecular + light.Specular * a;
            if ( a > best ) {
                best = a;
                bestQ = q;
                bestIndex = static_cast<int32_t>( i );
            }
        }
        float w = best;
        if ( bestIndex != -1 ) {
            const float t = 1.0f - ( best - 1.1920929e-7f );
            accDirection = accDirection + sunDirection * ( t / bestQ );
            ambient = accAmbient + sceneParams.Ambient * t;
            diffuse = accDiffuse + sceneParams.SunDiffuse * t;
            specular = accSpecular + sceneParams.SunSpecular * t;
            direction = accDirection.normalized();
        } else {
            w = 0.0f;
        }
        out.Shadow = shadowInfluence * ( 1.0f - w );
        // FUN_00a4ca50: the camera light (instance spot light), at the
        // anchor + 1e4 x camera forward (the surfaces facing the camera are
        // lit) [INF: camera axis FUN_0043dae0 is encrypted].
        const Vector3f anchor = rootPosition + Vector3f( 0.0f, 1.0f, 0.0f );
        if ( getFloat( "avatar_cameralight_desynchro", 0.0f ) < 0.5f ) {
            out.SpotPosition = anchor + cameraForward * 10000.0f;
        } else {
            out.SpotPosition =
                anchor - getVector3( "avatar_cameralight_direction", Vector3f( 0.0f, -1.0f, 0.0f ) ).normalized() * 10000.0f;
        }
        out.SpotAmbient = ambient * cameraFactor;
        out.SpotDiffuse = diffuse * cameraFactor;
        out.SpotSpecular = specular * cameraFactor;
    } else {
        out.Shadow = shadowInfluence;
        out.SpotAmbient = out.SpotDiffuse = out.SpotSpecular = Vector3f();
        out.SpotPosition = point;
    }
    // FUN_00b87aa0 (m = 1 out of a car).
    const float v = Clamp( 1.0f, getFloat( "avatar_lum_min", 0.0f ), getFloat( "avatar_lum_max", 1.0f ) );
    out.Ambient = ambient * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_ambiant_factor", 1.0f ) );
    out.Diffuse = diffuse * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_diffuse_factor", 1.0f ) );
    out.Specular = specular * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_specular_factor", 1.0f ) );
    out.Direction = direction;
    out.Environment =
        getFloat( "avatar_car_envmap_factor", 0.7f ) * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_car_night_reflection_factor", 0.0f ) );
}
