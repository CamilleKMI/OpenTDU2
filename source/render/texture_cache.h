#pragma once

#include "render/vulkan/render_device.h"

class Bank;
struct Texture2DB;

// Owns the GPU textures created from .2DB resources. Textures are looked up by
// Eden hashcode (GetIdentifier64bit of the resource name) and grouped by
// "owner" (usually the bank path) so a whole bank can be released at once.
class TextureCache {
public:
    TextureCache();
    ~TextureCache();

    void initialize( RenderDevice *pDevice );
    void destroy();

    GPUTexture *createFrom2DB( const Texture2DB &texture, const char *pName );
    GPUTexture *createFromRGBA( uint32_t width,
                                uint32_t height,
                                const uint8_t *pPixels,
                                const char *pName );

    // Loads every .2db of a bank. Returns the number of textures loaded.
    uint32_t loadBankTextures( const Bank &bank, const char *pOwner );
    void releaseOwner( const char *pOwner );

    // Global lookup (most recently loaded owner first).
    GPUTexture *find( uint64_t hashcode ) const;
    GPUTexture *find( const char *pName ) const;
    // Lookup restricted to the textures of an owner (bank).
    GPUTexture *find( const char *pOwner, const char *pName ) const;
    GPUTexture *getWhiteTexture() const { return pWhiteTexture; }
    GPUTexture *getCheckerTexture() const { return pCheckerTexture; }

private:
    struct OwnerTextures {
        std::string Owner;
        std::unordered_map<uint64_t, GPUTexture *> Textures;  // Owned
        std::unordered_map<uint64_t, GPUTexture *> Aliases;   // Not owned
    };

    RenderDevice *pDevice;
    std::vector<OwnerTextures> owners; // Load order

    OwnerTextures &getOwner( const char *pOwner );
    GPUTexture *pWhiteTexture;
    GPUTexture *pCheckerTexture;
};
