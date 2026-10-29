#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A car of the player's garages (profile vfunc 0x24 list: 0x280 records of
// 0x168 bytes at +0x30, FUN_00abffd0 fills one; docs/re/story_spec.md 5.2).
struct OwnedCar {
    int32_t IdCar = -1;        // DBCarPhysicsData _id_car (record +0x3c)
    int32_t Color = 0;         // DBCarsAndColors row of the car (+0x58)
    int32_t Rims = 0;          // DBCarsAndRims row of the car (+0x6c)
    int32_t Interior = 0;      // _interior_N of the colour row (+0x70)
    uint8_t Condition[3] = {}; // +0x88..+0x8a (0..4, FUN_00adc920)
    float Km = 0.0f;           // +0x78
};

// Player profile data shared between the game modes (profile+0x30 avatar
// configuration, profile+0xc1 name; the save game itself is not
// implemented).
struct PlayerProfile {
    std::string AvatarConfig; // DBPNJ _configid (AVT_<Male|Female>[_Asia|_Afrik][_Intro])
    std::string Name;
    int32_t StoryStep = 0;    // profile+0x58 (FUN_00b10da0)
    // Money (money object = profile vfunc 0x1c, value +0x2c, obfuscated
    // accessors FUN_0041b580 / FUN_004671e0 [ENC]). A new profile of the
    // story game (DAT_00fd90b6 set) is created with 27000 (FUN_00abe000
    // called at 0xadb2e6 with eax = 0x6978) [CODE]; without the story flag
    // DAT_00f7d4e8 = 100000000 [CODE + DATA].
    int32_t Money = 27000;
    // Money credited later (+0x44, FUN_00abe0e0 with bFlag 1, e.g. an event
    // reward), moved into Money by FUN_00abcf60 [CODE].
    int32_t PendingMoney = 0;
    // Money counter of the pending amount (money object +0x50): -1 idle,
    // FUN_00abd0d0 sets 1.0 s (DAT_00edf668) [CODE].
    float MoneyTransferTimer = -1.0f;
    // Money earned per reason (FUN_00abe0e0 param 4: +0xf05c + reason * 4 of
    // the statistics object, 28 reasons; 7 = event reward) [CODE].
    int32_t MoneyEarned[28] = {};
    std::vector<OwnedCar> Garage;
    // Current car (+0x54 garage slot, 0x280 = the loaned car of the story:
    // FUN_00ab9be0 / FUN_00aba280 set it to a DB car with no garage slot).
    int32_t CurrentGarageSlot = -1; // -1: the loaned car LoanCar
    int32_t LoanCar = 0x183;        // 387 California (story steps 1..6), 0x146 Dino_246_GTS (step 7)
};

PlayerProfile &GetPlayerProfile();

// Cinematic request of the CINE game mode (0x1f): FUN_00b9a1f0 stores it at
// DAT_01025608 + 0x9730 (kind), +0x9734 (callback, the story callback
// FUN_00b104d0), +0x973c (callback argument: the story object), +0x9740 /
// +0x9744 / +0x9748 / +0x974c / +0x9750; FUN_009b6df0 (CINE init) copies the
// kind to mode +0x1adc80 (docs/re/story_spec.md 3).
struct CineRequest {
    int32_t Kind = 0;       // 1 C_MEETING_TESS, 2 C_SOLAR_CROWN_E, 3 C_SOLAR_CROWN_I, 0x2b C_IGLOO...
    bool bStoryCallback = false; // callback FUN_00b104d0 (story step n -> n + 1)
};

CineRequest &GetCineRequest();

// Test drive request (mode 7 TESTCAR), written by the shop FSCommand tstk
// (FUN_009f3b30) [CODE]: DAT_00fd92a0 car (dealer slot car, FUN_009f2570),
// DAT_00f77688 colour / DAT_00f7768c rims / DAT_00f77690 interior of the
// slot record (+8 / +0xc / +0x10), DAT_00f77694 duration = DAT_00edf610
// 120.0 s, DAT_00f77698 = -1 (back to SHOP; FUN_008d9a10 sets 0x1d CASINO);
// the passenger config DAT_00fdb450 = the DBPNJ config of the shop PNJ
// "Hostess" (shop type 0) / "Jude" (shop type 3) of the shop bank (entity
// +0xe0), else "shop_lamborghini" (FUN_00dc2070).
struct TestDriveRequest {
    bool bActive = false;
    int32_t IdCar = -1;
    int32_t Color = 0;           // DBCarsAndColors row index of the car
    int32_t Rims = 0;            // DBCarsAndRims row index of the car
    int32_t Interior = 0;
    uint64_t RimsHashcode = 0;   // DBRims _hashcode of Rims (OpenTDU2: the car banks)
    std::string ColorId;         // DBCarsAndColors _color_id_1 (paint)
    float Duration = 120.0f;     // DAT_00f77694
    int32_t ReturnMode = -1;     // DAT_00f77698
    std::string PassengerConfig; // DAT_00fdb450
    std::string Spot;            // DAT_01025784 (the shop spot)
};

TestDriveRequest &GetTestDriveRequest();

// Money of the profile (docs/re/story_spec.md 4.7):
// FUN_00abe0e0(amount, bPending, reason): reason statistics, bPending 0 ->
// money (clamped >= 0), 1 -> pending money (shown by the money counter).
void ProfileAddMoney( int32_t amount, bool bPending, int32_t reason );
// FUN_00abd0d0: money display + transfer timer 1.0 s (moup).
void ProfileStartMoneyTransfer();
// Profile tick (FUN_00abd460 -> FUN_00abcf60(dt)) while the timer is >= 0:
// step = round(round(pending / timer) * dt) clamped to the pending amount,
// money += step, pending -= step, timer -= dt; below 0: the rest of the
// pending amount is added, timer -1 [CODE; the rounding FUN_006c8f00 is
// ENC -> nearest INF].
void ProfileTickMoney( float deltaTime );

// Story step 3 enter (FUN_00b10da0 case 4 -> FUN_00b105d0): the avatar
// configuration out of the dream, "AVT_%s%s" with "Male" / "Female" (DBPNJ
// _sex 2 / 1) and "" / "_Asia" / "_Afrik"; the profile keeps its hair and
// beard (slots 0 and 8 of the 10 clothing slots: _clothe_hair / _clothe_beard
// and their colours) [CODE].
std::string GetStoryAvatarConfig( const std::string &introConfig );
