#pragma once

#include <cmath>

static constexpr float kPI = 3.14159265358979323846f;
static constexpr float kDegToRad = kPI / 180.0f;
static constexpr float kRadToDeg = 180.0f / kPI;

struct Vector2f {
    float x = 0.0f, y = 0.0f;
    Vector2f() = default;
    constexpr Vector2f( float x_, float y_ ) : x( x_ ), y( y_ ) {}
    Vector2f operator+( const Vector2f &o ) const { return { x + o.x, y + o.y }; }
    Vector2f operator-( const Vector2f &o ) const { return { x - o.x, y - o.y }; }
    Vector2f operator*( float s ) const { return { x * s, y * s }; }
};

struct Vector3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vector3f() = default;
    constexpr Vector3f( float x_, float y_, float z_ ) : x( x_ ), y( y_ ), z( z_ ) {}
    Vector3f operator+( const Vector3f &o ) const { return { x + o.x, y + o.y, z + o.z }; }
    Vector3f operator-( const Vector3f &o ) const { return { x - o.x, y - o.y, z - o.z }; }
    Vector3f operator*( float s ) const { return { x * s, y * s, z * s }; }
    Vector3f operator-() const { return { -x, -y, -z }; }
    Vector3f &operator+=( const Vector3f &o ) { x += o.x; y += o.y; z += o.z; return *this; }
    Vector3f &operator-=( const Vector3f &o ) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vector3f &operator*=( float s ) { x *= s; y *= s; z *= s; return *this; }
    float dot( const Vector3f &o ) const { return x * o.x + y * o.y + z * o.z; }
    Vector3f cross( const Vector3f &o ) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
    float length() const { return std::sqrt( dot( *this ) ); }
    Vector3f normalized() const {
        float len = length();
        return len > 1e-8f ? *this * ( 1.0f / len ) : Vector3f( 0, 0, 0 );
    }
};

inline Vector3f Lerp( const Vector3f &a, const Vector3f &b, float t ) {
    return a + ( b - a ) * t;
}

struct Vector4f {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    Vector4f() = default;
    constexpr Vector4f( float x_, float y_, float z_, float w_ )
        : x( x_ ), y( y_ ), z( z_ ), w( w_ ) {}
    Vector4f( const Vector3f &v, float w_ ) : x( v.x ), y( v.y ), z( v.z ), w( w_ ) {}
    Vector3f xyz() const { return { x, y, z }; }
};

struct Quaternionf {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    Quaternionf() = default;
    constexpr Quaternionf( float x_, float y_, float z_, float w_ )
        : x( x_ ), y( y_ ), z( z_ ), w( w_ ) {}

    static Quaternionf FromAxisAngle( const Vector3f &axis, float angle ) {
        Vector3f n = axis.normalized();
        float s = std::sin( angle * 0.5f );
        return { n.x * s, n.y * s, n.z * s, std::cos( angle * 0.5f ) };
    }

    Quaternionf operator*( const Quaternionf &q ) const {
        return { w * q.x + x * q.w + y * q.z - z * q.y,
                 w * q.y - x * q.z + y * q.w + z * q.x,
                 w * q.z + x * q.y - y * q.x + z * q.w,
                 w * q.w - x * q.x - y * q.y - z * q.z };
    }

    Vector3f rotate( const Vector3f &v ) const {
        Vector3f u( x, y, z );
        Vector3f t = u.cross( v ) * 2.0f;
        return v + t * w + u.cross( t );
    }
};

// Column-major 4x4 matrix (m[column][row]), right-handed, Vulkan clip space.
struct Matrix4x4f {
    float m[4][4];

    Matrix4x4f() { *this = Identity(); }

    static Matrix4x4f Identity() {
        Matrix4x4f r( 0 );
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    static Matrix4x4f Translation( const Vector3f &t ) {
        Matrix4x4f r = Identity();
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    static Matrix4x4f Scale( const Vector3f &s ) {
        Matrix4x4f r = Identity();
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        return r;
    }

    static Matrix4x4f FromQuaternion( const Quaternionf &q ) {
        Matrix4x4f r = Identity();
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0][0] = 1 - 2 * ( yy + zz );
        r.m[0][1] = 2 * ( xy + wz );
        r.m[0][2] = 2 * ( xz - wy );
        r.m[1][0] = 2 * ( xy - wz );
        r.m[1][1] = 1 - 2 * ( xx + zz );
        r.m[1][2] = 2 * ( yz + wx );
        r.m[2][0] = 2 * ( xz + wy );
        r.m[2][1] = 2 * ( yz - wx );
        r.m[2][2] = 1 - 2 * ( xx + yy );
        return r;
    }

    static Matrix4x4f FromRotationTranslation( const Quaternionf &q,
                                               const Vector3f &t ) {
        Matrix4x4f r = FromQuaternion( q );
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    static Matrix4x4f Perspective( float fovY, float aspect, float zNear, float zFar ) {
        const float f = 1.0f / std::tan( fovY * 0.5f );
        Matrix4x4f r( 0 );
        r.m[0][0] = f / aspect;
        r.m[1][1] = -f; // Vulkan: Y down in clip space
        r.m[2][2] = zFar / ( zNear - zFar );
        r.m[2][3] = -1.0f;
        r.m[3][2] = ( zNear * zFar ) / ( zNear - zFar );
        return r;
    }

    static Matrix4x4f LookAt( const Vector3f &eye, const Vector3f &target,
                              const Vector3f &up ) {
        Vector3f f = ( target - eye ).normalized();
        Vector3f s = f.cross( up ).normalized();
        Vector3f u = s.cross( f );
        Matrix4x4f r = Identity();
        r.m[0][0] = s.x;
        r.m[1][0] = s.y;
        r.m[2][0] = s.z;
        r.m[0][1] = u.x;
        r.m[1][1] = u.y;
        r.m[2][1] = u.z;
        r.m[0][2] = -f.x;
        r.m[1][2] = -f.y;
        r.m[2][2] = -f.z;
        r.m[3][0] = -s.dot( eye );
        r.m[3][1] = -u.dot( eye );
        r.m[3][2] = f.dot( eye );
        return r;
    }

    static Matrix4x4f Orthographic( float left, float right, float top, float bottom ) {
        Matrix4x4f r = Identity();
        r.m[0][0] = 2.0f / ( right - left );
        r.m[1][1] = 2.0f / ( bottom - top );
        r.m[2][2] = 1.0f;
        r.m[3][0] = -( right + left ) / ( right - left );
        r.m[3][1] = -( bottom + top ) / ( bottom - top );
        return r;
    }

    Matrix4x4f operator*( const Matrix4x4f &o ) const {
        Matrix4x4f r( 0 );
        for ( int c = 0; c < 4; c++ ) {
            for ( int row = 0; row < 4; row++ ) {
                r.m[c][row] = m[0][row] * o.m[c][0] + m[1][row] * o.m[c][1] +
                              m[2][row] * o.m[c][2] + m[3][row] * o.m[c][3];
            }
        }
        return r;
    }

    Vector3f transformPoint( const Vector3f &p ) const {
        return { m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
                 m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
                 m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2] };
    }

    Vector3f transformVector( const Vector3f &v ) const {
        return { m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                 m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                 m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z };
    }

    Vector3f getTranslation() const { return { m[3][0], m[3][1], m[3][2] }; }

    Matrix4x4f transposed() const {
        Matrix4x4f r( 0 );
        for ( int i = 0; i < 4; i++ ) {
            for ( int j = 0; j < 4; j++ ) {
                r.m[i][j] = m[j][i];
            }
        }
        return r;
    }

    // General inverse (returns identity if singular).
    Matrix4x4f inverse() const {
        const float *a = &m[0][0];
        float inv[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
                 a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
                 a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
                 a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
                  a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
                 a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
                 a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
                 a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
                  a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
                 a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
                 a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
                  a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
                  a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
                 a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
                 a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
                  a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
                  a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if ( std::fabs( det ) < 1e-20f ) {
            return Identity();
        }
        det = 1.0f / det;
        Matrix4x4f r( 0 );
        float *o = &r.m[0][0];
        for ( int i = 0; i < 16; i++ ) {
            o[i] = inv[i] * det;
        }
        return r;
    }

private:
    explicit Matrix4x4f( int ) { memset( m, 0, sizeof( m ) ); }
};

struct ColorRGBA {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    ColorRGBA() = default;
    constexpr ColorRGBA( float r_, float g_, float b_, float a_ = 1.0f )
        : r( r_ ), g( g_ ), b( b_ ), a( a_ ) {}
    static ColorRGBA FromBytes( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
        return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }
    uint32_t toRGBA8() const {
        auto c = []( float v ) {
            return static_cast<uint32_t>( Clamp( v, 0.0f, 1.0f ) * 255.0f + 0.5f );
        };
        return c( r ) | ( c( g ) << 8 ) | ( c( b ) << 16 ) | ( c( a ) << 24 );
    }
};
