#pragma once

// .cpr files ("CryPted Resource") are XMB documents or INI files encrypted
// with XTEA (32 rounds) used as a block keystream:
//   Plain[i] = Cipher[i] ^ XTEA_Decipher( Cipher[i + 1] )
// The last 8 bytes block is never decrypted (output size = size - 8).
// See FUN_0095eb40 (decrypt) / FUN_0095ea60 (XTEA decipher).
void XTEADecipher( uint32_t v[2], const uint32_t key[4] );

bool DecryptCPR( const uint8_t *pInput,
                 size_t inputSize,
                 std::vector<uint8_t> &output );

// Helper: loads and decrypts a .cpr file as text (INI style config files).
bool LoadCPRText( const char *pPath, std::string &outText );
