#pragma once

#include "flash/flash_movie.h"
#include "game/gs_base.h"

// Frontend service. Keeps the shared frontend banks loaded (main.bnk carries
// the fonts used everywhere) and hands out movies to the game modes.
class GSFlash : public GameSystem {
public:
    GSFlash();
    ~GSFlash();

    const char *getName() const override { return "Service : Flash"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;
    void drawDebug() override;

    // Loads (or returns the already loaded) movie.
    FlashMovie *getMovie( const char *pName );
    void releaseMovie( const char *pName );

    // UI fonts (from main.bnk).
    const Font *getTitleFont() const;   // "Berlin Sans FB" bold
    const Font *getTextFont() const;    // "Berlin Sans FB" regular
    // Font by name in the loaded movies (main.bnk first).
    const Font *findFont( const char *pName ) const;

private:
    std::map<std::string, std::unique_ptr<FlashMovie>> movies;
};

extern GSFlash *gpFlash;
