#include "shared.h"
#include "eden_shader_system.h"

#include "filesystem/gs_file.h"

namespace {
// PTR_s_S_NBTEXTURES_00f91468
const char *const kSpecNames[EdenShaderSystem::kNumSpecs] = {
    "S_NBTEXTURES",  "S_COORDTEXTURE", "S_USEGLOSSMAP", "S_USEREFLECTION", "S_USEAMBIENT",
    "S_USEDIFFUSE",  "S_USESPECULAR",  "S_USEEMISSIVE", "S_UVMATRIXMASK",  "S_USESKINNING",
    "S_NBPRELIGHT",  "S_FOG",          "S_NORMAL",      "S_TANGENT",       "S_BINORMAL",
    "S_TANGENTSPACE", "S_PASS_Z",      "S_PASS_SHADOWCAST", "S_PASS_SHADOWRECEIVE", "S_MODE",
    "S_LOD",         "S_USEALPHATEST", "PAD",           "PAD",             "PAD",
    "PAD",           "PAD",            "PAD",           "PAD",             "PAD",
    "PAD",           "PAD",            "S_USERTYPE1",   "S_USERTYPE2",     "S_USERTYPE3",
    "S_USERTYPE4",   "S_USERTYPE5",    "S_USERTYPE6",   "S_USERTYPE7",     "S_USERTYPE8",
    "S_USERTYPE9",   "S_USERTYPE10",   "S_USERTYPE11",  "S_USERTYPE12",    "S_USERTYPE13",
    "S_USERTYPE14",  "S_USERTYPE15",   "S_USERTYPE16",
};

// Cost of a differing switch. The pass / skinning switches select another
// kind of shader and must match; the texture count and LOD are adjusted at
// runtime by the original scene handler.
uint32_t SpecWeight( uint32_t index ) {
    switch ( index ) {
    case EdenShaderSystem::S_USESKINNING:
    case EdenShaderSystem::S_PASS_Z:
    case EdenShaderSystem::S_PASS_SHADOWCAST:
    case EdenShaderSystem::S_PASS_SHADOWRECEIVE:
        return 1000;
    case EdenShaderSystem::S_LOD:
        return 1;
    case EdenShaderSystem::S_NBTEXTURES:
        return 4;
    case EdenShaderSystem::S_FOG:
    case EdenShaderSystem::S_MODE:
        return 6;
    default:
        return 10;
    }
}

uint32_t SpecCost( const uint8_t *pMaterialSpec, const std::vector<uint8_t> &itemSpec, uint32_t *pNumDiffs ) {
    uint32_t cost = 0;
    uint32_t numDiffs = 0;
    for ( uint32_t i = 0; i < EdenShaderSystem::kNumSpecs; i++ ) {
        const uint8_t itemValue = i < itemSpec.size() ? itemSpec[i] : 0;
        if ( itemValue != pMaterialSpec[i] ) {
            cost += SpecWeight( i );
            numDiffs++;
        }
    }
    if ( pNumDiffs != nullptr ) {
        *pNumDiffs = numDiffs;
    }
    return cost;
}

const ShaderLibrary::Item *FindBestItem( const ShaderLibrary::Profile &profile, const uint8_t *pSpec,
                                         uint32_t *pNumDiffs ) {
    const ShaderLibrary::Item *pBest = nullptr;
    uint32_t bestCost = ~0u;
    for ( const ShaderLibrary::Item &item : profile.Items ) {
        uint32_t numDiffs = 0;
        const uint32_t cost = SpecCost( pSpec, item.Spec, &numDiffs );
        if ( cost < bestCost ) {
            bestCost = cost;
            pBest = &item;
            if ( pNumDiffs != nullptr ) {
                *pNumDiffs = numDiffs;
            }
            if ( cost == 0 ) {
                break;
            }
        }
    }
    return pBest;
}
} // namespace

EdenShaderSystem::EdenShaderSystem() {}

EdenShaderSystem::~EdenShaderSystem() { destroy(); }

uint64_t EdenShaderSystem::HashSpec( const uint8_t *pSpec ) {
    constexpr uint64_t kFeedback = 0x00f0d50a00600340ull;
    constexpr uint64_t kOffset = 0x6422fb175d04f76bull;
    uint64_t r = 0;
    for ( uint32_t i = 0; i < kNumSpecs; i++ ) {
        for ( uint32_t step = 0; step < 8; step++ ) {
            const bool bFeedback = ( ( r >> 31 ) & 1 ) != 0;
            r = ( r << 1 ) | ( r >> 63 );
            if ( bFeedback ) {
                r ^= kFeedback;
            }
        }
        r ^= static_cast<uint64_t>( pSpec[i] ) << 32;
    }
    return r ^ kOffset;
}

const char *EdenShaderSystem::GetSpecName( uint32_t index ) {
    return index < kNumSpecs ? kSpecNames[index] : "?";
}

bool EdenShaderSystem::initialize() {
    destroy();
    const std::vector<std::filesystem::path> files = gpFile->listFolder( gpFile->getShaderFolder().c_str(), ".sob" );
    for ( const std::filesystem::path &path : files ) {
        auto pLibrary = std::make_unique<ShaderLibrary>();
        const std::string relativePath = gpFile->getShaderFolder() + path.filename().string();
        if ( !pLibrary->load( relativePath.c_str() ) ) {
            continue;
        }
        librariesByGUID[pLibrary->getGUID()] = pLibrary.get();
        librariesByName[ToUpper( pLibrary->getName() )] = pLibrary.get();
        libraries.push_back( std::move( pLibrary ) );
    }
    OTDU_LOG_INFO( "Eden shaders: %zu libraries loaded from %s\n", libraries.size(),
                   gpFile->getShaderFolder().c_str() );
    return !libraries.empty();
}

void EdenShaderSystem::destroy() {
    programs.clear();
    librariesByGUID.clear();
    librariesByName.clear();
    libraries.clear();
}

ShaderLibrary *EdenShaderSystem::findLibrary( uint64_t guid ) {
    auto it = librariesByGUID.find( guid );
    if ( it != librariesByGUID.end() ) {
        return it->second;
    }
    return nullptr;
}

ShaderLibrary *EdenShaderSystem::findLibrary( const char *pName ) {
    auto it = librariesByName.find( ToUpper( pName ) );
    return it != librariesByName.end() ? it->second : nullptr;
}

EdenProgram *EdenShaderSystem::buildProgram( ShaderLibrary *pLibrary, uint32_t vsAttrib, uint32_t psAttrib ) {
    const ShaderLibrary::Profile *pVSProfile = pLibrary->getProfile( ShaderLibrary::SP_VertexShader );
    const ShaderLibrary::Profile *pPSProfile = pLibrary->getProfile( ShaderLibrary::SP_PixelShader );
    if ( pVSProfile == nullptr || pPSProfile == nullptr || vsAttrib >= pVSProfile->Attribs.size() ||
         psAttrib >= pPSProfile->Attribs.size() ) {
        return nullptr;
    }
    const std::string key = pLibrary->getName() + ":" + std::to_string( vsAttrib ) + ":" + std::to_string( psAttrib );
    auto it = programs.find( key );
    if ( it != programs.end() ) {
        return it->second.get();
    }

    auto pProgram = std::make_unique<EdenProgram>();
    pProgram->Name = pLibrary->getName() + " vs" + std::to_string( vsAttrib ) + "/ps" + std::to_string( psAttrib );
    pProgram->ShaderSetGUID = pLibrary->getGUID();
    pProgram->VSAttrib = vsAttrib;
    pProgram->PSAttrib = psAttrib;
    pProgram->pVS = pLibrary->getTranslatedShader( ShaderLibrary::SP_VertexShader, vsAttrib );
    pProgram->pPS = pLibrary->getTranslatedShader( ShaderLibrary::SP_PixelShader, psAttrib );
    if ( pProgram->pVS == nullptr || pProgram->pPS == nullptr ) {
        OTDU_LOG_WARN( "Eden shaders: failed to translate %s\n", pProgram->Name.c_str() );
        programs[key] = nullptr;
        return nullptr;
    }
    const ShaderLibrary::Attrib &vs = pVSProfile->Attribs[vsAttrib];
    const ShaderLibrary::Attrib &ps = pPSProfile->Attribs[psAttrib];
    DXSO::ParseConstantMembers( vs.pBytecode, vs.BytecodeSize, pProgram->VSMembers );
    DXSO::ParseConstantMembers( ps.pBytecode, ps.BytecodeSize, pProgram->PSMembers );
    DXSO::ParseSamplerTypes( vs.pBytecode, vs.BytecodeSize, pProgram->VSSamplerTypes, pProgram->VSSamplerMask );
    DXSO::ParseSamplerTypes( ps.pBytecode, ps.BytecodeSize, pProgram->PSSamplerTypes, pProgram->PSSamplerMask );

    EdenProgram *pResult = pProgram.get();
    programs[key] = std::move( pProgram );
    return pResult;
}

const EdenProgram *EdenShaderSystem::getProgram( const char *pLibraryName, uint32_t vsAttrib, uint32_t psAttrib ) {
    ShaderLibrary *pLibrary = findLibrary( pLibraryName );
    return pLibrary != nullptr ? buildProgram( pLibrary, vsAttrib, psAttrib ) : nullptr;
}

const EdenProgram *EdenShaderSystem::getProgram( uint64_t guid, const uint8_t *pSpec ) {
    std::string key = "#";
    key.append( reinterpret_cast<const char *>( &guid ), sizeof( guid ) );
    key.append( reinterpret_cast<const char *>( pSpec ), kNumSpecs );
    auto it = programs.find( key );
    if ( it != programs.end() ) {
        return it->second.get();
    }

    // Candidate libraries: the family GUID, or (a few materials reference a
    // GUID no shipped library has, e.g. 37078865c0ca336e for the vegetation
    // trunks) every library, the closest GUID first.
    std::vector<ShaderLibrary *> candidates;
    auto exact = librariesByGUID.find( guid );
    if ( exact != librariesByGUID.end() && exact->second != nullptr ) {
        candidates.push_back( exact->second );
    } else {
        for ( const auto &pCandidate : libraries ) {
            candidates.push_back( pCandidate.get() );
        }
        auto similarity = [guid]( const ShaderLibrary *pCandidate ) {
            uint32_t score = 0;
            for ( uint32_t nibble = 0; nibble < 16; nibble++ ) {
                score += ( ( guid >> ( nibble * 4 ) ) & 0xf ) == ( ( pCandidate->getGUID() >> ( nibble * 4 ) ) & 0xf );
            }
            return score;
        };
        std::stable_sort( candidates.begin(), candidates.end(), [&]( const ShaderLibrary *x, const ShaderLibrary *y ) {
            return similarity( x ) > similarity( y );
        } );
    }

    ShaderLibrary *pLibrary = nullptr;
    uint8_t candidate[kNumSpecs];
    uint32_t numDiffs = 0;
    const ShaderLibrary::Item *pPSItem = nullptr;
    const ShaderLibrary::Item *pVSItem = nullptr;
    for ( ShaderLibrary *pCandidateLibrary : candidates ) {
        const ShaderLibrary::Profile *pVSProfile = pCandidateLibrary->getProfile( ShaderLibrary::SP_VertexShader );
        const ShaderLibrary::Profile *pPSProfile = pCandidateLibrary->getProfile( ShaderLibrary::SP_PixelShader );
        if ( pVSProfile == nullptr || pPSProfile == nullptr ) {
            continue;
        }
        // Exact permutation (hash of the switches, as the original renderer),
        // then the variants the runtime scene handler produces: texture count,
        // cleared optional switches (e.g. S_USESPECULAR without gloss map on
        // TD_Building, the rain switch S_USERTYPE7 on TD_Ground...).
        auto findItem = []( const ShaderLibrary::Profile &profile, uint64_t id ) -> const ShaderLibrary::Item * {
            for ( const ShaderLibrary::Item &item : profile.Items ) {
                if ( item.Hashcode == id ) {
                    return &item;
                }
            }
            return nullptr;
        };
        auto tryCandidate = [&]() {
            const uint64_t id = HashSpec( candidate );
            const ShaderLibrary::Item *pPS = findItem( *pPSProfile, id );
            const ShaderLibrary::Item *pVS = pPS != nullptr ? findItem( *pVSProfile, id ) : nullptr;
            if ( pPS != nullptr && pVS != nullptr ) {
                pPSItem = pPS;
                pVSItem = pVS;
                return true;
            }
            return false;
        };
        std::vector<uint32_t> optionalSwitches;
        for ( uint32_t i = 1; i < kNumSpecs; i++ ) {
            if ( pSpec[i] != 0 && i != S_LOD ) {
                optionalSwitches.push_back( i );
            }
        }
        std::vector<uint8_t> textureCounts;
        textureCounts.push_back( pSpec[S_NBTEXTURES] );
        for ( int32_t delta = 1; delta <= 10; delta++ ) {
            if ( pSpec[S_NBTEXTURES] + delta <= 16 ) {
                textureCounts.push_back( static_cast<uint8_t>( pSpec[S_NBTEXTURES] + delta ) );
            }
            if ( pSpec[S_NBTEXTURES] - delta >= 0 ) {
                textureCounts.push_back( static_cast<uint8_t>( pSpec[S_NBTEXTURES] - delta ) );
            }
        }
        const size_t numOptional = optionalSwitches.size();
        for ( uint32_t numCleared = 0; numCleared <= 2 && pPSItem == nullptr; numCleared++ ) {
            for ( uint8_t numTextures : textureCounts ) {
                if ( pPSItem != nullptr ) {
                    break;
                }
                auto setup = [&]() {
                    memcpy( candidate, pSpec, kNumSpecs );
                    candidate[S_NBTEXTURES] = numTextures;
                    candidate[S_LOD] = 0;
                };
                if ( numCleared == 0 ) {
                    setup();
                    tryCandidate();
                } else if ( numCleared == 1 ) {
                    for ( size_t a = 0; a < numOptional && pPSItem == nullptr; a++ ) {
                        setup();
                        candidate[optionalSwitches[a]] = 0;
                        tryCandidate();
                    }
                } else {
                    for ( size_t a = 0; a < numOptional && pPSItem == nullptr; a++ ) {
                        for ( size_t b = a + 1; b < numOptional && pPSItem == nullptr; b++ ) {
                            setup();
                            candidate[optionalSwitches[a]] = 0;
                            candidate[optionalSwitches[b]] = 0;
                            tryCandidate();
                        }
                    }
                }
            }
            numDiffs = numCleared;
        }
        if ( pPSItem == nullptr ) {
            // Closest stored permutation (libraries that keep the switches).
            pPSItem = FindBestItem( *pPSProfile, pSpec, &numDiffs );
            if ( pPSItem != nullptr && !pPSItem->Spec.empty() ) {
                pVSItem = findItem( *pVSProfile, pPSItem->Hashcode );
                memcpy( candidate, pPSItem->Spec.data(), Min<size_t>( pPSItem->Spec.size(), kNumSpecs ) );
            } else {
                pPSItem = nullptr;
            }
        }

        if ( pPSItem != nullptr && pVSItem != nullptr ) {
            pLibrary = pCandidateLibrary;
            break;
        }
    }
    if ( pLibrary == nullptr || pPSItem == nullptr || pVSItem == nullptr ) {
        OTDU_LOG_WARN( "Eden shaders: no permutation for family %016llx (%016llx)\n",
                       static_cast<unsigned long long>( guid ), static_cast<unsigned long long>( HashSpec( pSpec ) ) );
        programs[key] = nullptr;
        return nullptr;
    }
    if ( candidates.size() > 1 ) {
        librariesByGUID[guid] = pLibrary;
        OTDU_LOG_INFO( "Eden shaders: family %016llx -> %s\n", static_cast<unsigned long long>( guid ),
                       pLibrary->getName().c_str() );
    }

    EdenProgram *pBase = buildProgram( pLibrary, pVSItem->AttribIndex, pPSItem->AttribIndex );
    if ( pBase == nullptr ) {
        programs[key] = nullptr;
        return nullptr;
    }
    auto pProgram = std::make_unique<EdenProgram>( *pBase );
    pProgram->ItemId = pPSItem->Hashcode;
    pProgram->NumDifferences = numDiffs;
    if ( Logger::IsVerbose() ) {
        std::string differences;
        for ( uint32_t i = 0; i < kNumSpecs; i++ ) {
            if ( candidate[i] != pSpec[i] ) {
                differences += std::string( kSpecNames[i] ) + "(shader " + std::to_string( candidate[i] ) +
                               " mat " + std::to_string( pSpec[i] ) + ") ";
            }
        }
        OTDU_LOG_DEBUG( "Eden shaders: %s item %016llx %s\n", pProgram->Name.c_str(),
                        static_cast<unsigned long long>( pProgram->ItemId ),
                        differences.empty() ? "(exact)" : differences.c_str() );
    }
    EdenProgram *pResult = pProgram.get();
    programs[key] = std::move( pProgram );
    return pResult;
}
