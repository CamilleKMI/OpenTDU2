#include "shared.h"
#include "gs_flash.h"

#include "imgui.h"

GSFlash *gpFlash = nullptr;

GSFlash::GSFlash() { gpFlash = this; }

GSFlash::~GSFlash() { gpFlash = nullptr; }

bool GSFlash::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );
    // main.bnk holds the common frontend resources (fonts, icons pages).
    if ( getMovie( "main" ) == nullptr ) {
        OTDU_LOG_WARN( "Failed to load FrontEnd/HiRes/main.bnk (UI fonts missing)\n" );
    }
    return true;
}

void GSFlash::terminate() { movies.clear(); }

FlashMovie *GSFlash::getMovie( const char *pName ) {
    const std::string key = ToLower( pName );
    auto it = movies.find( key );
    if ( it != movies.end() ) {
        return it->second.get();
    }
    auto pMovie = std::make_unique<FlashMovie>();
    if ( !pMovie->load( pName ) ) {
        return nullptr;
    }
    FlashMovie *pResult = pMovie.get();
    movies[key] = std::move( pMovie );
    return pResult;
}

void GSFlash::releaseMovie( const char *pName ) {
    const std::string key = ToLower( pName );
    if ( key == "main" ) {
        return;
    }
    movies.erase( key );
}

const Font *GSFlash::getTitleFont() const {
    auto it = movies.find( "main" );
    if ( it == movies.end() ) {
        return nullptr;
    }
    const Font *pFont = it->second->getFont( "Berlin Sans FB", false );
    return pFont != nullptr ? pFont : it->second->getDefaultFont();
}

const Font *GSFlash::getTextFont() const {
    auto it = movies.find( "main" );
    if ( it == movies.end() ) {
        return nullptr;
    }
    const Font *pFont = it->second->getFont( "Berlin Sans FB", true );
    return pFont != nullptr ? pFont : it->second->getDefaultFont();
}

void GSFlash::drawDebug() {
    if ( !ImGui::Begin( "Flash" ) ) {
        ImGui::End();
        return;
    }
    for ( const auto &movie : movies ) {
        const VMFFile &vmf = movie.second->getVMF();
        if ( ImGui::TreeNode( movie.first.c_str() ) ) {
            ImGui::Text( "Stage %.0fx%.0f, %u frames @ %.0f fps, SWF v%u", vmf.getStageWidth(),
                         vmf.getStageHeight(), vmf.getFrameCount(), vmf.getFrameRate(),
                         vmf.getVersion() );
            ImGui::Text( "%zu tags, %zu bitmaps, %zu fonts", vmf.getTags().size(),
                         vmf.getBitmaps().size(), vmf.getFonts().size() );
            for ( const FlashFontDesc &font : vmf.getFonts() ) {
                ImGui::BulletText( "Font %u '%s' (texture %u, %zu/%zu glyphs)", font.CharacterId,
                                   font.Name.c_str(), font.TextureId, font.Glyphs.size(),
                                   font.SecondaryGlyphs.size() );
            }
            for ( const auto &exported : vmf.getExports() ) {
                ImGui::BulletText( "Export '%s' -> %u", exported.first.c_str(), exported.second );
            }
            for ( const std::string &label : vmf.getFrameLabels() ) {
                ImGui::BulletText( "Label '%s'", label.c_str() );
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

const Font *GSFlash::findFont( const char *pName ) const {
    auto main = movies.find( "main" );
    if ( main != movies.end() ) {
        if ( const Font *pFont = main->second->getFont( pName ) ) {
            return pFont;
        }
    }
    for ( const auto &entry : movies ) {
        if ( const Font *pFont = entry.second->getFont( pName ) ) {
            return pFont;
        }
    }
    return nullptr;
}
