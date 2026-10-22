#pragma once

// Parser for the Eden configuration files (SystemPC.cpr, GamePC.cpr,
// Physics.cpr... once decrypted):
//   [Section]
//   Key        = Value     // comment
//   Key  QUAL  = v0  v1    // optional qualifier, multiple values
class IniFile {
public:
    struct Entry {
        std::string Key;
        std::string Qualifier;
        std::vector<std::string> Values;
    };

    bool parse( const std::string &text );
    bool loadCPR( const char *pPath );

    const Entry *find( const char *pSection, const char *pKey,
                       const char *pQualifier = nullptr ) const;

    std::string getString( const char *pSection, const char *pKey,
                           const char *pDefault = "" ) const;
    int32_t getInt( const char *pSection, const char *pKey, int32_t defaultValue ) const;
    float getFloat( const char *pSection, const char *pKey, float defaultValue,
                    const char *pQualifier = nullptr, uint32_t valueIndex = 0 ) const;
    bool getBool( const char *pSection, const char *pKey, bool defaultValue ) const;

    inline const std::map<std::string, std::vector<Entry>> &getSections() const {
        return sections;
    }

private:
    std::map<std::string, std::vector<Entry>> sections; // lowercase section names
};
