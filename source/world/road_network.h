#pragma once

#include "core/math.h"

#include <string>
#include <vector>

// Island road network (docs/re/car_lights_spec.md 6).
//
// <island>.bnk group 12 holds two files [DATA]:
//   sub 0 <Island>_ROAD_MAP.IA    "RI41" "DAEH" ...: graph nodes ("EDON" records)
//   sub 1 <Island>_ROAD_DETAIL.IA "DI41" "DAEH" u32 numRoads, then one record
//         per road:
//           +0x00 "ATED"  +0x04 u32 road id  +0x08 u32 numSections
//           +0x0c u32 sectionOffset[numSections] (from the record start)
//         section (at record + offset):
//           +0x00 u16 numPoints (cubic Bezier control points, 3n + 1)
//           +0x02 u16, +0x04 u32, +0x08 u32, +0x0c u32 (unidentified)
//           +0x10 u8 (unidentified, 2 / 4 / 5)
//           +0x11 u8 type: 6 = tunnel (FUN_00c41d20 / FUN_00c62a70 [CODE]);
//                 Ibiza: 0 (216), 1 (497), 2 (3022), 3 (1816), 4 (29), 6 (30)
//           +0x12 u16 length in metres (= the Bezier arc length +-2 m)
//           +0x14 points: { f32 x, y, z } each, a f32 after every knot
//                 (points 0, 3, 6...: small values, bank angle [INF])
// The game reads the loaded copy in place: road +0x28 -> record, record +8
// = numSections (byte), record +0xc + i * 4 = section i, section +0 =
// numPoints ((n + 2) / 3 - 1 Bezier segments, FUN_00cfcbd0), section +0x11
// = type [CODE].
class RoadNetwork {
public:
    struct Section {
        uint8_t Type = 0;
        uint16_t Length = 0;
        std::vector<Vector3f> Points; // 3n + 1 Bezier control points
        // Arc length samples per Bezier segment (kSamples + 1 each).
        std::vector<float> SegmentStart; // distance along the road at segment start
        std::vector<float> SegmentLength;
    };
    struct Road {
        uint32_t Id = 0;
        std::vector<Section> Sections;
        Vector3f BoundsMin, BoundsMax;
    };
    // Position on the road network (the game's road position: road, section
    // (+0x36 word), Bezier segment (+0x38), parameter (+0x40), FUN_00cf4540).
    struct Position {
        int32_t Road = -1;
        int32_t Section = -1;
        int32_t Segment = -1;
        float T = 0.0f;
        float Along = 0.0f;    // distance from the road start
        float Distance = 0.0f; // from the query point to the road centre line
    };

public:
    // pIsland: "ibiza", "hawai"... Loads Euro/Bnk/islands/<island>/level/<island>.bnk.
    bool load( const char *pIsland );
    void unload();
    inline bool isLoaded() const { return !roads.empty(); }
    inline const std::vector<Road> &getRoads() const { return roads; }

    // Nearest road centre line point within maxHorizontal metres
    // horizontally and maxVertical vertically [INF: the game tracks the car's
    // road position with its traffic / AI code (car->ctrl +0x20 / +0x24), not
    // readable; the nearest centre line stands in for it].
    bool project( const Vector3f &point, float maxHorizontal, float maxVertical, Position &outPosition ) const;
    // FUN_00c41d20 [CODE]: 0 when the section at the position is not a tunnel
    // (type 6); else the distance along the road to the nearest end of the
    // run of tunnel sections (walking segment by segment both ways while the
    // section type stays 6, stopping at the road ends; min of both ways).
    float getTunnelDistance( const Position &position ) const;
    // FUN_00c62a70 [CODE]: the car tunnel distance (car +0x2acc) is computed
    // only when the camera is less than 100 m from the car (squared distance
    // < 10000, 0x00edda84), else 0. Road position: project() within 10 m
    // horizontally / 5 m vertically [INF].
    float getCarTunnelDistance( const Vector3f &carPosition, const Vector3f &cameraPosition ) const;

private:
    static Vector3f Bezier( const Vector3f *pP, float t );
    std::vector<Road> roads;
};
