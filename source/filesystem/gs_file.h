#pragma once

#include "game/gs_base.h"

// Virtual file system. TDU2 normally reads its data from the "bigfiles"
// (bigfile_EU_*.map + TDU2v*.big). OpenTDU2 only supports the unpacked layout
// (the one the TDU2 Universal Launcher enables), i.e.:
//   <GameRoot>/Euro/Bnk/...      banks (.bnk), databases (.cpr), sounds...
//   <GameRoot>/Euro/Shaders/...  shader libraries (.sob)
//   <GameRoot>/*.cpr             configuration files
//
// Paths given to the VFS are the ones used by the original game (relative to
// the game root, '\' or '/' separators, any case). They are resolved case
// insensitively since the original data has inconsistent casing.
class GSFile : public GameSystem {
public:
    inline const std::filesystem::path &getGameRoot() const {
        return gameRoot;
    }
    inline const std::filesystem::path &getUserDataFolder() const {
        return userDataFolder;
    }

    // Root of the bank folder ("Euro/Bnk/"), as used by the format strings of
    // the original executable (e.g. "%sVehicules/High/%s.bnk").
    inline std::string getBankFolder() const { return "Euro/Bnk/"; }
    inline std::string getShaderFolder() const { return "Euro/Shaders/"; }

public:
    GSFile();
    ~GSFile();

    const char *getName() const override { return "Service : File"; }
    bool initialize( TestDriveGameInstance *pGameInstance ) override;
    void tick( float deltaTime ) override {}
    void terminate() override;

    bool fileExists( const char *pPath );
    std::optional<std::filesystem::path> resolvePath( const char *pPath );

    // Loads the whole file in memory (16 bytes aligned). Caller owns the
    // memory (free with TestDrive::FreeAligned).
    bool loadFile( const char *pPath, void **ppOutBuffer, uint32_t *pOutSize );
    bool loadFile( const char *pPath, std::vector<uint8_t> &outBuffer );

    std::vector<std::filesystem::path> listFolder( const char *pPath,
                                                   const char *pExtension );

private:
    std::filesystem::path gameRoot;
    std::filesystem::path userDataFolder;
    std::unordered_map<std::string, std::filesystem::path> resolvedPathCache;
    std::mutex cacheMutex;

private:
    bool findGameRoot();
    std::optional<std::filesystem::path>
    resolveCaseInsensitive( const std::string &normalizedPath );
};

extern GSFile *gpFile;
