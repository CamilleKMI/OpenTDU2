#include "shared.h"
#include "flash_movie.h"

#include "filesystem/gs_file.h"
#include "render/gs_render.h"

namespace {
std::string GetFlashTextureName( uint32_t characterId ) {
    char name[32];
    snprintf( name, sizeof( name ), "fla_%04u", characterId );
    return name;
}
} // namespace

FlashMovie::FlashMovie() : bLoaded( false ) {}

FlashMovie::~FlashMovie() { unload(); }

bool FlashMovie::load( const char *pName, const char *pResolution ) {
    unload();
    name = pName;

    std::string path = gpFile->getBankFolder() + "FrontEnd/" + pResolution + "/" + pName + ".bnk";
    if ( !gpFile->fileExists( path.c_str() ) ) {
        path = gpFile->getBankFolder() + "FrontEnd/" + pName + ".bnk";
    }
    if ( !bank.load( path.c_str() ) ) {
        return false;
    }
    owner = path;

    gpRender->getTextureCache().loadBankTextures( bank, owner.c_str() );

    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension == "2db" && StringNCmpI( entry.Name.c_str(), "page_", 5 ) == 0 ) {
            GPUTexture *pTexture = gpRender->getTextureCache().find( owner.c_str(), entry.Name.c_str() );
            if ( pTexture != nullptr ) {
                pages.emplace_back( ToLower( entry.Name ), pTexture );
            }
        }
    }

    const std::vector<const BankEntry *> movies = bank.findEntriesByExtension( "vmf" );
    if ( movies.empty() || !vmf.load( movies[0]->pData, movies[0]->Size ) ) {
        OTDU_LOG_WARN( "Bank '%s' has no valid flash movie\n", path.c_str() );
    } else {
        OTDU_LOG_INFO( "Flash movie '%s': %.0fx%.0f, %u frames @ %.0f fps, %zu tags, %zu bitmaps, "
                       "%zu fonts\n",
                       pName, vmf.getStageWidth(), vmf.getStageHeight(), vmf.getFrameCount(),
                       vmf.getFrameRate(), vmf.getTags().size(), vmf.getBitmaps().size(),
                       vmf.getFonts().size() );
        createFonts();
    }

    bLoaded = true;
    return true;
}

void FlashMovie::unload() {
    if ( !owner.empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( owner.c_str() );
    }
    owner.clear();
    fonts.clear();
    pages.clear();
    bank.unload();
    bLoaded = false;
}

GPUTexture *FlashMovie::findTexture( const std::string &textureName ) const {
    if ( bank.findEntry( textureName.c_str(), "2db" ) == nullptr ) {
        return nullptr;
    }
    return gpRender->getTextureCache().find( owner.c_str(), textureName.c_str() );
}

void FlashMovie::createFonts() {
    for ( const FlashFontDesc &desc : vmf.getFonts() ) {
        FontEntry entry;
        entry.Name = desc.Name;

        // The textured glyph table maps to fla_<CharacterId>; TDU2 renders the
        // bold variant with the same atlas (the secondary padded atlas is a glow
        // rendition used by some animated texts).
        GPUTexture *pAtlas = findTexture( GetFlashTextureName( desc.CharacterId ) );
        if ( pAtlas != nullptr && !desc.Glyphs.empty() ) {
            auto pFont = std::make_unique<Font>();
            if ( pFont->createFromFlash( desc, desc.Glyphs, pAtlas ) ) {
                entry.pRegular = std::move( pFont );
            }
        }
        if ( entry.pBold != nullptr || entry.pRegular != nullptr ) {
            fonts.push_back( std::move( entry ) );
        }
    }
}

const Font *FlashMovie::getFont( const char *pFontName, bool bRegular ) const {
    for ( const FontEntry &entry : fonts ) {
        if ( pFontName != nullptr && StringCmpI( entry.Name.c_str(), pFontName ) != 0 ) {
            continue;
        }
        if ( bRegular && entry.pRegular != nullptr ) {
            return entry.pRegular.get();
        }
        if ( entry.pBold != nullptr ) {
            return entry.pBold.get();
        }
        return entry.pRegular.get();
    }
    return nullptr;
}

const Font *FlashMovie::getDefaultFont() const {
    return fonts.empty() ? nullptr : getFont( fonts[0].Name.c_str(), true );
}

FlashMovie::BitmapRef FlashMovie::getTexture( const char *pTextureName ) const {
    BitmapRef ref;
    ref.pTexture = findTexture( pTextureName );
    if ( ref.pTexture != nullptr ) {
        ref.UV = Rect2D( 0.0f, 0.0f, 1.0f, 1.0f );
        ref.Width = static_cast<float>( ref.pTexture->Width );
        ref.Height = static_cast<float>( ref.pTexture->Height );
    }
    return ref;
}

FlashMovie::BitmapRef FlashMovie::getBitmap( uint16_t characterId ) const {
    BitmapRef ref;
    const FlashBitmapDesc *pDesc = vmf.findBitmap( characterId );
    if ( pDesc == nullptr ) {
        return ref;
    }
    ref.UV = Rect2D( pDesc->U0, pDesc->V0, pDesc->U1 - pDesc->U0, pDesc->V1 - pDesc->V0 );
    ref.Width = pDesc->Width;
    ref.Height = pDesc->Height;

    // Standalone bitmap texture (fla_<id>.2db) or atlas page: pick the page
    // whose size matches the UV rectangle / pixel size ratio.
    ref.pTexture = findTexture( GetFlashTextureName( characterId ) );
    if ( ref.pTexture != nullptr ) {
        return ref;
    }
    float bestError = 1e9f;
    for ( const auto &page : pages ) {
        const float expectedWidth = ref.UV.Width > 0.0f ? ref.Width / ref.UV.Width : 0.0f;
        const float expectedHeight = ref.UV.Height > 0.0f ? ref.Height / ref.UV.Height : 0.0f;
        const float error = std::fabs( expectedWidth - page.second->Width ) +
                            std::fabs( expectedHeight - page.second->Height );
        if ( error < bestError ) {
            bestError = error;
            ref.pTexture = page.second;
        }
    }
    return ref;
}
