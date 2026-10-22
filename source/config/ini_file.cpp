#include "shared.h"
#include "ini_file.h"

#include "core/crypto/cpr.h"

namespace {
std::string Trim( const std::string &s ) {
    size_t begin = s.find_first_not_of( " \t\r\n" );
    if ( begin == std::string::npos ) {
        return std::string();
    }
    size_t end = s.find_last_not_of( " \t\r\n" );
    return s.substr( begin, end - begin + 1 );
}

std::vector<std::string> Tokenize( const std::string &s ) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while ( i < s.size() ) {
        while ( i < s.size() && isspace( static_cast<unsigned char>( s[i] ) ) ) {
            i++;
        }
        if ( i >= s.size() ) {
            break;
        }
        if ( s[i] == '"' ) {
            size_t end = s.find( '"', i + 1 );
            if ( end == std::string::npos ) {
                end = s.size();
            }
            tokens.push_back( s.substr( i + 1, end - i - 1 ) );
            i = end + 1;
            continue;
        }
        size_t end = i;
        while ( end < s.size() && !isspace( static_cast<unsigned char>( s[end] ) ) ) {
            end++;
        }
        tokens.push_back( s.substr( i, end - i ) );
        i = end;
    }
    return tokens;
}
} // namespace

bool IniFile::parse( const std::string &text ) {
    sections.clear();
    std::string currentSection;

    size_t lineStart = 0;
    while ( lineStart < text.size() ) {
        size_t lineEnd = text.find( '\n', lineStart );
        if ( lineEnd == std::string::npos ) {
            lineEnd = text.size();
        }
        std::string line = text.substr( lineStart, lineEnd - lineStart );
        lineStart = lineEnd + 1;

        // Strip comments (outside of quotes)
        bool bInQuotes = false;
        for ( size_t i = 0; i + 1 < line.size(); i++ ) {
            if ( line[i] == '"' ) {
                bInQuotes = !bInQuotes;
            } else if ( !bInQuotes && line[i] == '/' && line[i + 1] == '/' ) {
                line.resize( i );
                break;
            }
        }
        line = Trim( line );
        if ( line.empty() ) {
            continue;
        }

        if ( line[0] == '[' ) {
            size_t end = line.find( ']' );
            currentSection = ToLower( line.substr( 1, end == std::string::npos ? std::string::npos : end - 1 ) );
            continue;
        }

        size_t equal = line.find( '=' );
        if ( equal == std::string::npos ) {
            continue;
        }

        std::vector<std::string> keyTokens = Tokenize( line.substr( 0, equal ) );
        if ( keyTokens.empty() ) {
            continue;
        }
        Entry entry;
        entry.Key = keyTokens[0];
        if ( keyTokens.size() > 1 ) {
            entry.Qualifier = keyTokens[1];
        }
        entry.Values = Tokenize( line.substr( equal + 1 ) );
        sections[currentSection].push_back( entry );
    }
    return true;
}

bool IniFile::loadCPR( const char *pPath ) {
    std::string text;
    if ( !LoadCPRText( pPath, text ) ) {
        return false;
    }
    return parse( text );
}

const IniFile::Entry *IniFile::find( const char *pSection, const char *pKey,
                                     const char *pQualifier ) const {
    auto it = sections.find( ToLower( pSection ) );
    if ( it == sections.end() ) {
        return nullptr;
    }
    for ( const Entry &entry : it->second ) {
        if ( StringCmpI( entry.Key.c_str(), pKey ) != 0 ) {
            continue;
        }
        if ( pQualifier != nullptr && StringCmpI( entry.Qualifier.c_str(), pQualifier ) != 0 ) {
            continue;
        }
        return &entry;
    }
    return nullptr;
}

std::string IniFile::getString( const char *pSection, const char *pKey,
                                const char *pDefault ) const {
    const Entry *pEntry = find( pSection, pKey );
    return ( pEntry != nullptr && !pEntry->Values.empty() ) ? pEntry->Values[0] : pDefault;
}

int32_t IniFile::getInt( const char *pSection, const char *pKey, int32_t defaultValue ) const {
    const Entry *pEntry = find( pSection, pKey );
    return ( pEntry != nullptr && !pEntry->Values.empty() ) ? atoi( pEntry->Values[0].c_str() )
                                                            : defaultValue;
}

float IniFile::getFloat( const char *pSection, const char *pKey, float defaultValue,
                         const char *pQualifier, uint32_t valueIndex ) const {
    const Entry *pEntry = find( pSection, pKey, pQualifier );
    return ( pEntry != nullptr && valueIndex < pEntry->Values.size() )
               ? static_cast<float>( atof( pEntry->Values[valueIndex].c_str() ) )
               : defaultValue;
}

bool IniFile::getBool( const char *pSection, const char *pKey, bool defaultValue ) const {
    const Entry *pEntry = find( pSection, pKey );
    if ( pEntry == nullptr || pEntry->Values.empty() ) {
        return defaultValue;
    }
    const std::string &value = pEntry->Values[0];
    return StringCmpI( value.c_str(), "TRUE" ) == 0 || value == "1";
}
