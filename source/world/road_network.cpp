#include "shared.h"
#include "road_network.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"

#include <cstring>

namespace {
constexpr int32_t kSamples = 16; // arc length samples per Bezier segment

template <typename T> bool Read( const uint8_t *pData, uint32_t size, uint32_t offset, T &out ) {
    if ( offset + sizeof( T ) > size ) {
        return false;
    }
    memcpy( &out, pData + offset, sizeof( T ) );
    return true;
}
} // namespace

Vector3f RoadNetwork::Bezier( const Vector3f *pP, float t ) {
    const float u = 1.0f - t;
    return pP[0] * ( u * u * u ) + pP[1] * ( 3.0f * u * u * t ) + pP[2] * ( 3.0f * u * t * t ) + pP[3] * ( t * t * t );
}

bool RoadNetwork::load( const char *pIsland ) {
    unload();
    Bank bank;
    const std::string path = gpFile->getBankFolder() + "islands/" + pIsland + "/level/" + pIsland + ".bnk";
    if ( !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "RoadNetwork: %s not loaded\n", path.c_str() );
        return false;
    }
    const BankEntry *pEntry = nullptr;
    for ( const BankEntry &entry : bank.getEntries() ) {
        // group 12 sub 1: <Island>_ROAD_DETAIL.IA [DATA]
        if ( entry.Group == 12 && entry.SubType == 1 ) {
            pEntry = &entry;
        }
    }
    if ( pEntry == nullptr || pEntry->Size < 12 || memcmp( pEntry->pData, "DI41DAEH", 8 ) != 0 ) {
        OTDU_LOG_WARN( "RoadNetwork: no ROAD_DETAIL in %s\n", path.c_str() );
        return false;
    }
    const uint8_t *pData = pEntry->pData;
    const uint32_t size = pEntry->Size;
    uint32_t numRoads = 0;
    Read( pData, size, 8, numRoads );
    uint32_t record = 12;
    roads.reserve( numRoads );
    for ( uint32_t r = 0; r < numRoads && record + 12 <= size; r++ ) {
        if ( memcmp( pData + record, "ATED", 4 ) != 0 ) {
            OTDU_LOG_WARN( "RoadNetwork: bad road record at 0x%x\n", record );
            break;
        }
        Road road;
        uint32_t numSections = 0;
        Read( pData, size, record + 4, road.Id );
        Read( pData, size, record + 8, numSections );
        uint32_t end = record + 12 + numSections * 4;
        float along = 0.0f;
        road.BoundsMin = Vector3f( 1e30f, 1e30f, 1e30f );
        road.BoundsMax = Vector3f( -1e30f, -1e30f, -1e30f );
        for ( uint32_t s = 0; s < numSections; s++ ) {
            uint32_t offset = 0;
            Read( pData, size, record + 12 + s * 4, offset );
            const uint32_t section = record + offset;
            uint16_t numPoints = 0;
            Section out;
            Read( pData, size, section, numPoints );
            Read( pData, size, section + 0x11, out.Type );
            Read( pData, size, section + 0x12, out.Length );
            uint32_t p = section + 0x14;
            for ( uint16_t i = 0; i < numPoints; i++ ) {
                float xyz[3] = {};
                if ( !Read( pData, size, p, xyz ) ) {
                    break;
                }
                p += 12;
                if ( i % 3 == 0 ) {
                    p += 4; // knot value (bank angle [INF])
                }
                const Vector3f point( xyz[0], xyz[1], xyz[2] );
                out.Points.push_back( point );
                road.BoundsMin = Vector3f( std::min( road.BoundsMin.x, point.x ), std::min( road.BoundsMin.y, point.y ),
                                           std::min( road.BoundsMin.z, point.z ) );
                road.BoundsMax = Vector3f( std::max( road.BoundsMax.x, point.x ), std::max( road.BoundsMax.y, point.y ),
                                           std::max( road.BoundsMax.z, point.z ) );
            }
            end = std::max( end, p );
            const int32_t numSegments = ( static_cast<int32_t>( out.Points.size() ) + 2 ) / 3 - 1;
            for ( int32_t k = 0; k < numSegments; k++ ) {
                const Vector3f *pP = &out.Points[static_cast<size_t>( k ) * 3];
                float length = 0.0f;
                Vector3f previous = pP[0];
                for ( int32_t j = 1; j <= kSamples; j++ ) {
                    const Vector3f q = Bezier( pP, static_cast<float>( j ) / kSamples );
                    length += ( q - previous ).length();
                    previous = q;
                }
                out.SegmentStart.push_back( along );
                out.SegmentLength.push_back( length );
                along += length;
            }
            road.Sections.push_back( std::move( out ) );
        }
        roads.push_back( std::move( road ) );
        // Next record: the next "ATED" after this one's data [DATA: records
        // are contiguous on Ibiza].
        uint32_t next = end;
        while ( next + 4 <= size && memcmp( pData + next, "ATED", 4 ) != 0 ) {
            next++;
        }
        record = next;
    }
    uint32_t numTunnel = 0;
    for ( const Road &road : roads ) {
        for ( const Section &section : road.Sections ) {
            numTunnel += section.Type == 6 ? 1 : 0;
        }
    }
    OTDU_LOG_INFO( "RoadNetwork: %s: %zu roads (header %u), %u tunnel sections\n", pIsland, roads.size(), numRoads,
                   numTunnel );
    return !roads.empty();
}

void RoadNetwork::unload() {
    roads.clear();
}

bool RoadNetwork::project( const Vector3f &point, float maxHorizontal, float maxVertical, Position &outPosition ) const {
    float best = maxHorizontal * maxHorizontal;
    bool bFound = false;
    for ( size_t r = 0; r < roads.size(); r++ ) {
        const Road &road = roads[r];
        if ( point.x < road.BoundsMin.x - maxHorizontal || point.x > road.BoundsMax.x + maxHorizontal ||
             point.z < road.BoundsMin.z - maxHorizontal || point.z > road.BoundsMax.z + maxHorizontal ||
             point.y < road.BoundsMin.y - maxVertical || point.y > road.BoundsMax.y + maxVertical ) {
            continue;
        }
        for ( size_t s = 0; s < road.Sections.size(); s++ ) {
            const Section &section = road.Sections[s];
            for ( size_t k = 0; k < section.SegmentLength.size(); k++ ) {
                const Vector3f *pP = &section.Points[k * 3];
                Vector3f previous = pP[0];
                float previousT = 0.0f;
                for ( int32_t j = 1; j <= kSamples; j++ ) {
                    const float t = static_cast<float>( j ) / kSamples;
                    const Vector3f q = Bezier( pP, t );
                    // Closest point of the chord previous -> q (horizontal).
                    const Vector3f d( q.x - previous.x, 0.0f, q.z - previous.z );
                    const float dd = d.dot( d );
                    float u = dd > 1e-8f ? Vector3f( point.x - previous.x, 0.0f, point.z - previous.z ).dot( d ) / dd : 0.0f;
                    u = std::min( std::max( u, 0.0f ), 1.0f );
                    const Vector3f c = previous + ( q - previous ) * u;
                    const float dx = point.x - c.x;
                    const float dz = point.z - c.z;
                    const float d2 = dx * dx + dz * dz;
                    if ( d2 < best && std::fabs( point.y - c.y ) <= maxVertical ) {
                        best = d2;
                        bFound = true;
                        outPosition.Road = static_cast<int32_t>( r );
                        outPosition.Section = static_cast<int32_t>( s );
                        outPosition.Segment = static_cast<int32_t>( k );
                        outPosition.T = previousT + ( t - previousT ) * u;
                        outPosition.Along = section.SegmentStart[k] + section.SegmentLength[k] * outPosition.T;
                        outPosition.Distance = std::sqrt( d2 );
                    }
                    previous = q;
                    previousT = t;
                }
            }
        }
    }
    return bFound;
}

float RoadNetwork::getTunnelDistance( const Position &position ) const {
    if ( position.Road < 0 || position.Road >= static_cast<int32_t>( roads.size() ) ) {
        return 0.0f;
    }
    const Road &road = roads[static_cast<size_t>( position.Road )];
    const int32_t numSections = static_cast<int32_t>( road.Sections.size() );
    if ( position.Section < 0 || position.Section >= numSections ||
         road.Sections[static_cast<size_t>( position.Section )].Type != 6 ) {
        return 0.0f;
    }
    // Run of type 6 sections around the position (the walk stops at the
    // first segment of another type or at the road end, FUN_00cfcbd0 /
    // FUN_00cfcb60).
    int32_t first = position.Section;
    while ( first > 0 && road.Sections[static_cast<size_t>( first - 1 )].Type == 6 ) {
        first--;
    }
    int32_t last = position.Section;
    while ( last + 1 < numSections && road.Sections[static_cast<size_t>( last + 1 )].Type == 6 ) {
        last++;
    }
    const Section &firstSection = road.Sections[static_cast<size_t>( first )];
    const Section &lastSection = road.Sections[static_cast<size_t>( last )];
    if ( firstSection.SegmentStart.empty() || lastSection.SegmentStart.empty() ) {
        return 0.0f;
    }
    const float runStart = firstSection.SegmentStart.front();
    const float runEnd = lastSection.SegmentStart.back() + lastSection.SegmentLength.back();
    // FUN_00cfde30 x 2: distances to both ends, the smaller kept (0x00c42013).
    return std::min( position.Along - runStart, runEnd - position.Along );
}

float RoadNetwork::getCarTunnelDistance( const Vector3f &carPosition, const Vector3f &cameraPosition ) const {
    const Vector3f toCamera = cameraPosition - carPosition;
    if ( toCamera.dot( toCamera ) >= 10000.0f ) {
        return 0.0f;
    }
    Position position;
    if ( !project( carPosition, 10.0f, 5.0f, position ) ) {
        return 0.0f;
    }
    return getTunnelDistance( position );
}
