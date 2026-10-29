#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_scene.h"

// C_FADE_FREERIDE (Scenarics/Cinematics/intro/fade_freeride.bnk, loaded by
// story step 2 enter into the step scene slot, FUN_00b9ff50): played by the
// step 2 tick when the 90 s timer runs out (listener = story +0x14), 15.0 s,
// one shot on NO_CAMERA (fade_blanc_M_FADE_FREERIDE.cin): Fx
// FxIntroNew-E_Meet_Tess_02_00 at 2.68 s (Tess wakes the player, the knock
// s_toctoc), actor WOMAN (M_FADE_FREERIDE_life.sce) with two voice Fx. Its
// natural end (listener FUN_00b10580, event 7) sets story +0x700 -> the next
// step 2 tick goes to story step 3 "Meeting Tess" [CODE]. The white screen
// is not drawn by any data of the bank (CinProps 32, no shot fade) [INF:
// OpenTDU2 fades to white].
class FadeFreeride {
public:
    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // origin: cinematic frame -> world (the sounds) [INF: the frame of the
    // NO_CAMERA scene is not tied to a spot; mapped on the player car].
    void start( const Matrix4x4f &origin );
    void tick( float deltaTime );
    inline bool isPlaying() const { return bPlaying; }
    inline bool isEnded() const { return bEnded; }
    inline float getTime() const { return time; }
    inline float getDuration() const { return cinematic.getDuration(); }

private:
    Bank bank;
    Cinematic cinematic;
    std::vector<CinematicScene> scenes;
    CinematicSounds sounds;
    std::vector<GSAudio::VoiceHandle> voices;
    float time = 0.0f;
    bool bPlaying = false;
    bool bEnded = false;
    bool bLoaded = false;
};
