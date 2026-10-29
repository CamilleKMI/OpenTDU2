#include "shared.h"
#include "fade_freeride.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

namespace {
constexpr const char *kBank = "scenarics/cinematics/intro/fade_freeride.bnk";
constexpr const char *kCinematic = "C_FADE_FREERIDE";

std::string FileStem( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    return dot != std::string::npos ? file.substr( 0, dot ) : file;
}
} // namespace

bool FadeFreeride::load() {
    unload();
    const std::string path = gpFile->getBankFolder() + kBank;
    if ( !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "FadeFreeride: '%s' not found\n", path.c_str() );
        return false;
    }
    BankHashcodes hashcodes;
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        return false;
    }
    XmbValue entityRoot = entities.getRoot()["ExportType_Entity"];
    const BankEntry *pCin = nullptr;
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
        XmbValue entry = entityRoot.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        if ( entry["__Name"].asString() != kCinematic ) {
            continue;
        }
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ) && pCin == nullptr; j++ ) {
            XmbValue file = entry.getArrayElement( "CinFile", j );
            if ( file["rscSubType"].asInt() == 16 ) {
                pCin = hashcodes.find( file["rsc"].asUInt64() );
            }
        }
    }
    if ( pCin == nullptr || !cinematic.parse( pCin->pData, pCin->Size ) ) {
        OTDU_LOG_WARN( "FadeFreeride: %s .cin not found\n", kCinematic );
        return false;
    }
    for ( int32_t reference : cinematic.getSceneReferences() ) {
        CinematicScene scene;
        if ( reference >= 0 && reference < static_cast<int32_t>( cinematic.getReferences().size() ) ) {
            const BankEntry *pScene = bank.findEntry( FileStem( cinematic.getReferences()[reference].Text ).c_str(), "sce" );
            if ( pScene != nullptr ) {
                scene.parseScene( pScene->pData, pScene->Size );
            }
        }
        scenes.push_back( std::move( scene ) );
    }
    sounds.load( bank );
    bLoaded = true;
    OTDU_LOG_INFO( "FadeFreeride: %s %.2f s, %zu scenes\n", kCinematic, cinematic.getDuration(), scenes.size() );
    return true;
}

void FadeFreeride::unload() {
    sounds.stop( voices );
    sounds.unload();
    scenes.clear();
    bank.unload();
    bPlaying = bEnded = bLoaded = false;
    time = 0.0f;
}

void FadeFreeride::start( const Matrix4x4f &origin ) {
    time = 0.0f;
    bEnded = false;
    bPlaying = bLoaded;
    sounds.setSceneOrigin( origin );
    if ( bPlaying && !CmdLine::HasArg( "-noaudio" ) ) {
        sounds.update( cinematic, -1.0f, 0.0f, &voices );
    }
}

void FadeFreeride::tick( float deltaTime ) {
    if ( !bPlaying ) {
        return;
    }
    const float previous = time;
    time = Min( time + deltaTime, cinematic.getDuration() );
    if ( !CmdLine::HasArg( "-noaudio" ) ) {
        // .cin Fx (FxIntroNew-E_Meet_Tess_02_00) and the WOMAN voice Fx of
        // the scene.
        sounds.update( cinematic, previous, time, &voices );
        const Cinematic::Shot *pShot = cinematic.getShot( time );
        if ( pShot != nullptr && pShot->Scene < scenes.size() ) {
            const CinematicScene &scene = scenes[pShot->Scene];
            sounds.updateSceneFx( scene, previous > 0.0f ? pShot->getSceneTime( previous ) : pShot->CameraStart - 1e-4f,
                                  pShot->getSceneTime( time ), sounds.getSceneOrigin() * scene.getOrigin(), &voices );
        }
    }
    // FUN_00b40350: end reached (t > duration - 0.001) -> event 7.
    if ( time >= cinematic.getDuration() - 0.001f ) {
        bPlaying = false;
        bEnded = true;
    }
}
