#pragma once

#include "core/math.h"

// Euro/Bnk/DataBase/IKOffsetDB.bin record of a car for one sex
// (docs/re/driver_seat_spec.md 1.3): FUN_00b97cc0 load, FUN_00b96ba0 parse
// [CODE]. Big endian (the PC parser byte-swaps when the magic reads "BDKI"):
//   +0x00 u16 2, u16 3, u32 0, u32 ?, "IKDB"
//   +0x10 chunk "CAIK", u16 2 (sexes), u16 0, u32, u32
//   +0x20 u32 numCars, then per car: u32 _id_car,
//         2 x { f32 seat[5]; 27 x { f32 offset[3]; f32 unused[3] } },
//         11 x f32[3]
// seat[] r1..r5 are stored at record +0x220, +0x228, +0x224, +0x230, +0x22c
// [CODE]: the driver seat group moves by (0, +0x224, +0x228) = (0, r3, r2),
// the passenger one by (0, +0x22c, +0x230) = (0, r5, r4) (FUN_00b8ade0 state
// 2000 / 3000 through the avatar pointers +0x1b6c / +0x1b70). Node offset =
// the first 3 floats (record entry +0x20, read by FUN_00b7e940 / FUN_00b8ade0)
// [CODE]. Sex index 1 = woman (FUN_00cabec0() == 1) [INF: DBPNJ _sex 1].
// The 27 nodes (tables 0xf7dbf0 LR / 0xf7db68 HR) [CODE]: 0 DvrSeat, 1 Hand_R,
// 2 Hand_L, 3 Foot_R, 4 Foot_L, 5 Radio, 6 Back_L, 7 Gear, 8 HBK, 9 Wcd,
// 10 PsgSeat, 11 Back_R, 12 PsgFoot_R, 13 PsgFoot_L, 14 PsgSeat, 15..20
// Seat_L, 21..26 Seat_R.
struct CarIKOffsets {
    enum eNode : int32_t {
        DvrSeat = 0,
        HandR = 1,
        HandL = 2,
        FootR = 3,
        FootL = 4,
        BackL = 6,
        PsgSeat = 10,
        BackR = 11,
        PsgFootR = 12,
        PsgFootL = 13,
    };
    bool bValid = false;
    float Seat[5] = {}; // r1..r5 in file order
    Vector3f Node[27];

    inline float driverSeatY() const { return Seat[2]; }
    inline float driverSeatZ() const { return Seat[1]; }
    inline float passengerSeatY() const { return Seat[4]; }
    inline float passengerSeatZ() const { return Seat[3]; }
};

// False (offsets 0, as FUN_00b97cc0's zero fill) when the file or the car
// record is missing.
bool LoadCarIKOffsets( int32_t idCar, bool bWoman, CarIKOffsets &out );
