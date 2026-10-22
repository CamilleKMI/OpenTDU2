#pragma once

// Basic SWF types used by the VMF runtime (twips, 2x3 matrices, colour
// transforms, bit-packed records).

struct FlashMatrix {
    // x' = A * x + C * y + TX ; y' = B * x + D * y + TY (SWF convention)
    float A = 1.0f, B = 0.0f, C = 0.0f, D = 1.0f, TX = 0.0f, TY = 0.0f;

    FlashMatrix operator*( const FlashMatrix &o ) const {
        // this * o : apply o first, then this
        FlashMatrix r;
        r.A = A * o.A + C * o.B;
        r.B = B * o.A + D * o.B;
        r.C = A * o.C + C * o.D;
        r.D = B * o.C + D * o.D;
        r.TX = A * o.TX + C * o.TY + TX;
        r.TY = B * o.TX + D * o.TY + TY;
        return r;
    }
    void transform( float x, float y, float &outX, float &outY ) const {
        outX = A * x + C * y + TX;
        outY = B * x + D * y + TY;
    }
    FlashMatrix inverse() const {
        FlashMatrix r;
        const float det = A * D - B * C;
        if ( std::fabs( det ) < 1e-12f ) {
            return r;
        }
        const float invDet = 1.0f / det;
        r.A = D * invDet;
        r.B = -B * invDet;
        r.C = -C * invDet;
        r.D = A * invDet;
        r.TX = -( r.A * TX + r.C * TY );
        r.TY = -( r.B * TX + r.D * TY );
        return r;
    }
};

struct FlashColorTransform {
    float Mul[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float Add[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // 0..255

    FlashColorTransform operator*( const FlashColorTransform &child ) const {
        // Apply child first, then this.
        FlashColorTransform r;
        for ( int i = 0; i < 4; i++ ) {
            r.Mul[i] = Mul[i] * child.Mul[i];
            r.Add[i] = Mul[i] * child.Add[i] + Add[i];
        }
        return r;
    }
    uint32_t apply( uint32_t rgba ) const {
        uint32_t out = 0;
        for ( int i = 0; i < 4; i++ ) {
            const float c = static_cast<float>( ( rgba >> ( i * 8 ) ) & 0xff );
            const float v = Clamp( c * Mul[i] + Add[i], 0.0f, 255.0f );
            out |= static_cast<uint32_t>( v + 0.5f ) << ( i * 8 );
        }
        return out;
    }
};

class FlashBitReader {
public:
    FlashBitReader( const uint8_t *pData, uint32_t size, uint32_t offset )
        : pData( pData ), size( size ), byteOffset( offset ), bitOffset( 0 ) {}

    uint32_t readUBits( uint32_t numBits ) {
        uint32_t value = 0;
        for ( uint32_t i = 0; i < numBits; i++ ) {
            const uint8_t byte = byteOffset < size ? pData[byteOffset] : 0;
            value = ( value << 1 ) | ( ( byte >> ( 7 - bitOffset ) ) & 1 );
            if ( ++bitOffset == 8 ) {
                bitOffset = 0;
                byteOffset++;
            }
        }
        return value;
    }
    int32_t readSBits( uint32_t numBits ) {
        uint32_t value = readUBits( numBits );
        if ( numBits > 0 && numBits < 32 && ( value & ( 1u << ( numBits - 1 ) ) ) ) {
            value |= ~( ( 1u << numBits ) - 1 );
        }
        return static_cast<int32_t>( value );
    }
    void align() {
        if ( bitOffset != 0 ) {
            bitOffset = 0;
            byteOffset++;
        }
    }
    uint32_t getOffset() const { return byteOffset; }

    void readRect( float &xMin, float &xMax, float &yMin, float &yMax ) {
        const uint32_t numBits = readUBits( 5 );
        xMin = static_cast<float>( readSBits( numBits ) );
        xMax = static_cast<float>( readSBits( numBits ) );
        yMin = static_cast<float>( readSBits( numBits ) );
        yMax = static_cast<float>( readSBits( numBits ) );
        align();
    }
    FlashMatrix readMatrix() {
        FlashMatrix m;
        if ( readUBits( 1 ) ) {
            const uint32_t numBits = readUBits( 5 );
            m.A = readSBits( numBits ) / 65536.0f;
            m.D = readSBits( numBits ) / 65536.0f;
        }
        if ( readUBits( 1 ) ) {
            const uint32_t numBits = readUBits( 5 );
            m.B = readSBits( numBits ) / 65536.0f;
            m.C = readSBits( numBits ) / 65536.0f;
        }
        const uint32_t numBits = readUBits( 5 );
        m.TX = static_cast<float>( readSBits( numBits ) );
        m.TY = static_cast<float>( readSBits( numBits ) );
        align();
        return m;
    }
    FlashColorTransform readColorTransform( bool bWithAlpha ) {
        FlashColorTransform cx;
        const bool bHasAdd = readUBits( 1 ) != 0;
        const bool bHasMul = readUBits( 1 ) != 0;
        const uint32_t numBits = readUBits( 4 );
        const int numComponents = bWithAlpha ? 4 : 3;
        if ( bHasMul ) {
            for ( int i = 0; i < numComponents; i++ ) {
                cx.Mul[i] = readSBits( numBits ) / 256.0f;
            }
        }
        if ( bHasAdd ) {
            for ( int i = 0; i < numComponents; i++ ) {
                cx.Add[i] = static_cast<float>( readSBits( numBits ) );
            }
        }
        align();
        return cx;
    }

private:
    const uint8_t *pData;
    uint32_t size;
    uint32_t byteOffset;
    uint32_t bitOffset;
};
