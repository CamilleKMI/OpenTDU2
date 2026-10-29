# TDU2 PC: 3D sound positioning (spec_sound3d.md)

Tags: **[CODE]** = read in readable code (VA >= 0x6c1000), **[DATA]** = read in game files / exe data,
**[INF]** = inference (not proven), **[ENC]** = the logic sits in SecuROM-encrypted code (< 0x6c1000). It was
not read, decrypted or emulated.

Sources: `TestDrive2.exe` (Ghidra project `/tmp/claude-0/bm/gh`, `text.asm`), `Euro/Bnk/sound/audiosetup.xmb`,
`islands/ibiza/sound/ambience/ambiences.xmb`, `sound/vehicles/fullsize1_high.bnk` (CarVSTConfig.xmb), the `.bfx`
files of `new_intro / garage / meeting_tess / fade_freeride / worldactors / eph_l3di / lph_l3d`, `entities.xmb`
and `.cin` of `new_intro.bnk`, `SystemPC.cpr`. Scratch dumps: `/tmp/claude-0/-home-claude/44d890d5-.../scratchpad/`
(`audiosetup_v.txt`, `as_c.txt`, `amb.txt`, `vst.txt`, `bfx2.py`, `s1..s6.c`).

---------------------------------------------------------------------------------------------------
## 0. Short answers

| question | answer |
|---|---|
| Audio engine | Eden's own **edAudio** software mixer (edAudioMixer / edAudioSpatializer / edAudioSourceSpatial / edAudioChannelRouter / pipelines). **Not FMOD, not XACT, not X3DAudio, not DirectSound3D, no HRTF.** [DATA/CODE] |
| Output | `edAudioOutputPCDirectSound` (IDirectSound8 streaming buffer, the **default**: `SystemPC.cpr [Audio] SetOutputDriver "DirectSound"`) or `edAudioOutputPCXAudio` (**XAudio2 2.3**, March 2009 SDK). `SetOutputNbSpeakers 6` (2 / 6 / 8). [DATA] |
| Who spatializes | edAudio itself: each spatial source is panned into an internal **5-channel bus (FL, FR, BL, BR, C)**. Fixed routing matrices from `audiosetup.xmb` then map it to stereo / 5.1 / 7.1. The device only receives a finished PCM stream. [DATA + INF] |
| Listener | **The camera** (game camera, which a cinematic drives). If the cinematic entity has a **`Micro`** (non-zero), the cinematic actor that matches it becomes the listener and the camera listener is switched off. [CODE] |
| Listener orientation | forward = camera forward **projected on the horizontal plane** (y = 0, then normalized; (0,0,1) if degenerate); **up is always (0,1,0)**; right = up x forward. So elevation is ignored. [CODE] |
| Distance model | Per source: **ClipNear / ClipMid / ClipFar / ClipMidVolume / ClipAttenuation** (`.bfx`), ClipNear/ClipFar (+ DopplerFactor) for SC emitters, ClipNear/ClipFar/RefractionFactor for vehicles. The curve is computed in **[ENC]** code. The data fits a piecewise-linear "1 at Near, MidVol at Mid, 0 at Far" curve [INF]. |
| 2D vs 3D | Chosen by the **client** (`.bfx +0xdc`). Each client goes to a pipeline. **Cinematic / CinematicMusic → "Cinematic" pipeline, 2 inputs (plain stereo)**. **AmbienceInternalSfx, CinematicSfx, Woosh, AnimationSfx… → "Sfx" pipeline, 5 inputs (spatialized)**. [DATA] |
| Ambiance_SoundEmiter1/2 | SC sound emitters in "ambiance" mode. They play **listener-relative** at **(x = -5 or +5, y = 0.1, z = 0.1)** with **Volume x 0.5**: a left/right pair. [CODE] (which flag value is set for which emitter: [INF]) |
| Doppler | Per source DopplerFactor plus a global `SETOUTPUTDOPPLERFACTOR` (GSAudio+0x9a0). The listener velocity comes from the camera (+0x360), the source velocity from source+0x300. The formula is **[ENC]**. |

---------------------------------------------------------------------------------------------------
## 1. Engine and output [DATA/CODE]

- RTTI (0x00f919dc…0x00f91f20): `edAudio, edAudioPC, edAudioConfig, edAudioMixer, edAudioBuffer, edAudioChannelRouter,
  edAudioChannelAdapter, edAudioPipelineBase, edAudioSourceBase, edAudioSource<edAudioSetupSpatial>,
  edAudioSourceSpatial, edAudioSpatializer, edAudioCluster, edAudioListener, edAudioVUMeter, edAudioStream,
  edAudioMemoryStream`, codecs, `IXAudio2VoiceCallback`, `edFxSound`.
- All their vtables point into **0x57xxxx (edAudio) and 0x5eXXXX (edFxSound)**, which is **[ENC]**:
  - edAudioSpatializer: vtable 0xf3da78 → 0x577bf0, 0x5781f0, …
  - edAudioSourceSpatial: vtable 0xf3da14 → 0x5786b0, 0x575b10, 0x578920, …
  - edFxSound: vtable 0xf49314 → 0x5ea540…
  
  **So the pan law, the attenuation curve, the cone and the Doppler math cannot be read.** Everything below comes
  from the readable game layer (GSAudio 0xd9xxxx, SC_SoundEmiter 0xb3axxx, vehicles 0xaffxxx, listener 0xd944b0,
  0xc3e530, 0xb3b240…) and from data.
- Imports: `DSOUND.dll #11` (DirectSoundCreate8) and `#6` (DirectSoundCaptureCreate, voice chat). There is **no
  X3DAudio DLL, no "X3DAudio" string and no IDirectSound3DBuffer/Listener string**. The DS error strings show
  one streaming buffer with notifications (`IDirectSoundNotify::SetNotificationPositions`, `CreateSoundBuffer`).
  XAudio2 is created with COM. The edAudioOutputPCXAudio vtable data holds CLSID `4c5e637a-16c7-4de3-9c46-5ed22181962d`
  (= XAudio2 **2.3**) and `ef0aa05d-…` (its debug CLSID). [DATA]
- GSAudio singleton = **0x01000a90** (ctor FUN_00d95350, "GSAudio" service, vtable 0xeac394: Init 0xd95770,
  Update 0xd94460, Terminate 0xd94e30). edAudioPC instance = 0x013e29f0 (init 0x5772e0 [ENC]). [CODE]
- Speaker mode (GSAudio+0x8d8 = 0x01001368, `SETOUTPUTNBSPEAKERS`, default 6): 2 → channel mask 0x3, 6 → 0x3f,
  8 → 0xff (Init 0xd957e2…0xd95816). [CODE]
- `SETOUTPUTSPATIALFACTOR` → GSAudio+0x9a4 (0x01001434), `SETOUTPUTDOPPLERFACTOR` → GSAudio+0x9a0 (0x01001430)
  (handlers 0xd69c80 / 0xd69c60, setters 0xd93bd0/0xd93be0, getters 0xd93b60/0xd93b70). Neither is set in
  `SystemPC.cpr`. Their default and their use are **[ENC]** (only encrypted code reads them).
- Init (0xd95770) builds a fixed graph of 5-channel buffers/mixers (`edAudioMixer` ctor 0x577730 with ecx = 5):
  "Vst filter's Input Mixer/Buffer", "Vst filter", "Tunnels Reverb Buffer", "Tunnels Reverb", "Tunnels Reverb
  Output Buffer", "Reverb and other VST Pipelines Mixer", "Vst Output Buffer", "Vst Output Adapter"
  (0x577cc0(5,…)). Then `Sound/audioSetup.xmb` (FUN_00d95640) adds pipelines, clients, mixers, ducking, mix presets
  and game modes. [CODE]

---------------------------------------------------------------------------------------------------
## 2. audiosetup.xmb (Euro/Bnk/sound/audiosetup.xmb, 15271 bytes) [DATA]

Schema: `AudioSetupExport { PipelineExport[] Pipelines; ClientExport[] Clients; MixerExport[] Mixers;
DuckingPresetExport[] DuckingPresets; MixPresetExport[] MixPresets; GameModeExport[] GameModes }`.
- `PipelineExport { RoutingMatrix RoutingStereo, Routing51, Routing71; String Name; Int32 HashCode }`
- `RoutingMatrix { Int32 InputChannelCount; Int32 OutputChannelCount; Single[] Mapping }`: Mapping is row-major
  **[input][output] in dB**, where -80 = mute. Linear gain = 10^(dB/20).
- `ClientExport { Int32 Pipeline (pipeline hash); Boolean IsMusic; String Name; Int32 HashCode }`
- `MixPresetExport { ClientPair[] {Gain, TargetId}; MixerPair[] {Gain, TargetId}; Name; HashCode }` (linear gains)
- `DuckingPresetExport { Trigger (client); TargetsIds[]; AttackTime; SustainTime; SustainGain; ReleaseTime }`
- `GameModeExport { MixPreset; Name }`

### 2.1 Internal channel order (deduced from the matrices)

| pipeline inputs | meaning [INF, strongly supported] |
|---|---|
| 5 (Sfx, AmbienceVolumic, Misc, SpatializedPlayer, OtherVehicles) | spatializer output **0 FL, 1 FR, 2 BL, 3 BR, 4 C** |
| 2 | stereo L, R |
| 1 | mono |

Output order: 5.1 = **FL, FR, BL, BR, C, LFE**; 7.1 = **FL, FR, BL, BR, SL, SR, C, LFE**. (edAudio order. SDL's
order is different, see §8.)

"Sfx" (5 in), in dB:
- stereo, rows FL..C: `FL[-6,-80] FR[-80,-6] BL[-6,-80] BR[-80,-6] C[-6,-6]`, so
  **L = 0.501·(FL+BL+C), R = 0.501·(FR+BR+C)**.
- 5.1: identity to outputs 0..4, plus every input to LFE at -12 dB.
- 7.1: FL→FL, SL -6; FR→FR, SR -6; BL→BL, SL -6; BR→BR, SR -6; C→C; all inputs → LFE -12.

"SpatializedPlayer"/"OtherVehicles"/"AmbienceVolumic"/"Misc": same as Sfx but without LFE (Misc 5.1/7.1 are all 0 dB;
AmbienceVolumic stereo C = -12).

"Cinematic" (2 in): stereo = identity (0 dB); 5.1: L→FL 0, BL -42; R→FR 0, BR -42; 7.1 similar.
**So it is a plain front stereo pass-through.**
"AmbienceInternal"/"AmbienceExternal" (2 in): stereo identity; 5.1: L→FL -6, BL -6; R→FR -6, BR -6.
"VoiceMono"/"VoiceSubtitled" (1 in): stereo -6/-6; 5.1/7.1 → C 0 dB.
"FrontEndStereo" (2 in): identity; 5.1 L→FL 0, BL -42.

### 2.2 Clients → pipeline (39 clients)

| client | pipeline | spatial (5-ch)? |
|---|---|---|
| VoiceGPS, VoicePlayers, OffroadWarning, edVideoMono | VoiceMono | no (center) |
| VoiceStoryline, VoiceCops, VoiceGSM, VoicePassenger, VoiceAvatar | VoiceSubtitled | no (center) |
| FrontEnd, FrontEndMusic, edVideoStereo, Radio | FrontEndStereo | no |
| edVideo5_1 | FrontEnd5_1 | no |
| **AmbienceInternalSfx, AmbienceExternalSfx, AmbienceLightning, VoiceSpatialized, Aircraft, Boat, Woosh, CinematicSfx, AnimationSfx, PhysicSfx** | **Sfx** | **yes** |
| **Cinematic, CinematicMusic** | **Cinematic** | **no (stereo)** |
| AmbienceInternal / AmbienceRain / AmbienceZone | AmbienceInternal / AmbienceExternal / AmbienceVolumic | no / no / yes |
| Engine, EngineFx, ExhaustPipe, Transmission, Aerodynamic, Skid, Rolling, Misc, SpatializedPlayer, OtherVehicles | own pipelines | vehicles |

### 2.3 Client index used by .bfx (+0xdc) [CODE table + DATA]

The exe table of client names at **0x00f90ac0** (u32 pointers, index 0..33):
`0 VoiceStoryline, 1 VoiceCops, 2 VoiceGSM, 3 VoicePassenger, 4 VoiceAvatar, 5 VoiceGPS, 6 VoicePlayers, 7 FrontEnd,
8 FrontEndMusic, 9 Radio, 10 AmbienceInternal, 11 AmbienceInternalSfx, 12 AmbienceZone, 13 AmbienceRain,
14 AmbienceExternalSfx, 15 Aircraft, 16 Boat, 17 AmbienceLightning, 18 Woosh, 19 CinematicSfx, 20 AnimationSfx,
21 PhysicSfx, 22 Cinematic, 23 CinematicMusic, 24 Engine, 25 EngineFx, 26 ExhaustPipe, 27 Transmission,
28 Aerodynamic, 29 Skid, 30 Rolling, 31 Misc, 32 OtherVehicles`.
Check: every `*_CINEMATIC.wav` node has 22, `*_AMBIENCE_INTERNAL_SFX.wav` 11, `*_VOICE_STORYLINE.wav` 0, smashable
props 21 (PhysicSfx), `Amb_Ventilation_01` 10, `S_WALKMODE_Garage` 23. **The wav suffix is the client name.**

### 2.4 Mix presets and ducking (useful for the intro)

GameModes: `AVATAR_CHOICE` and `FIRST_WM` → preset **Intro**; `CINE`, `CINVIEWER`, `JAIL`, `CAR_WASH`, `EXIT` → **Cinematic**;
`BOOTMENU/LOGIN/VIDEOBUMPER…` → Menu; `FREERIDE/MISSION/…` → Freeride.

| preset | Cinematic | CinematicMusic | CinematicSfx | AmbienceInternalSfx | AmbienceInternal | VoiceStoryline | Woosh |
|---|---|---|---|---|---|---|---|
| Intro | 1 | 0.75 | 1 | **0.3** | 1 | **1.5** | 0 |
| Cinematic | 1 | 0.6 | 1 | 1 | 1 | 1.5 | 0 |
| Default | 1 | 1 | 1 | 1 | 1 | 1.5 | 1 |
| Menu | 0 | 0 | 0 | 0 | 0 | 1.5 | 0 |

Ducking (envelope Attack / Sustain / SustainGain / Release, all in s):
- VoiceStoryline → {Cinematic, CinematicMusic}: 0.4 / 0.2 / **0.75** / 0.8
- VoiceStoryline → AmbienceInternalSfx: 0.2 / 0.3 / **0.3** / 1.0
- VoiceAvatar → vehicles + Cinematic + CinematicMusic: 0.8 / 0.2 / 0.3 / 0.8
- Cinematic → player vehicle pipelines: 0.2 / 0.3 / 0.5 / 1.0

The full list is in `as_c.txt`. Mixers: Master ⊃ {Music, Voices, Vehicles, VoicePlayers, SFX}, SFX ⊃ …, `Cinematic` mixer =
{CinematicSfx, Cinematic, AnimationSfx}. The UserVolume options sliders apply per mixer (FUN_00dd7a70…).

---------------------------------------------------------------------------------------------------
## 3. .bfx SND node (edFxSound) [DATA; names from the exe debug string]

The exe debug format at 0x00f49278 (file 0xb48a78) lists the edFxSound parameters:
`S@Vol=%.2f Ptch=%.2f VolRnd=%.2f PtchRnd=%.2f ClpNr=%.2f ClpMd=%.2f ClpFr=%.2f ClpMdVol=%.2f ClpAttn=%.2f ID=%u
CnIn=%.2f CnOut=%.2f CnOutVol=%.2f`. A second, run-time format reads `S@Vol Ptch ModVol ModPtch IsSpc=%s Lgth`.

Node layout (offsets from the node start; a single-sound .bfx is u32 6 + one node; a voice .bfx is an `LSGL`
language-switch node, then 8 SND nodes of 0xe0+4 bytes):

| off | type | field | observed values (92 nodes) |
|---|---|---|---|
| +0x10 | f32[16] | node local matrix | identity |
| +0x50 | u64 | node id | |
| +0x58 | u32 | class hash 0x8540076c | constant |
| +0x78 | char[32] | name | |
| +0xa0 | u64 | sound resource hash (Fx__Sound SoundFile rsc) | |
| +0xa8 | f32 | ? | 0 |
| +0xac | f32 | **Vol** (linear) | 1, 0.501 (-6 dB), 0.631 (-4 dB), 0.794, 0.126 |
| +0xb0 | f32 | **Ptch** (ratio) | 1; smashables 0.943874 = 2^(-1/12) |
| +0xb4 | f32 | **VolRnd** | 0 |
| +0xb8 | f32 | **PtchRnd** | 0; smashables 1 |
| +0xbc | f32 | **CnIn** (cone inner angle, deg) | 180 |
| +0xc0 | f32 | **CnOut** (cone outer angle, deg) | 180 (S_WATER 179.98) |
| +0xc4 | f32 | **CnOutVol** | 0.25 |
| +0xc8 | f32 | **ClpNr** near distance | 4 (cinematics), 10 (S_TimeStop, smashables), 20 (S_WALKMODE_Music), 5 (S_WATER) |
| +0xcc | f32 | **ClpMd** mid distance | 40 / 50 / 20 |
| +0xd0 | f32 | **ClpFr** far distance | ClpMd + ε (0x42200003 = 40.00001, 50.00001, 20.00001) |
| +0xd4 | f32 | **ClpMdVol** volume at mid | 0 |
| +0xd8 | f32 | **ClpAttn** | 0 |
| +0xdc | u32 | **ID = client index** (§2.3) | 22, 11, 0, 21, 10, 23 |
| +0xe0 | char[4] | `" DNS"` | |

The mapping from the printf names to these offsets is **[INF]**. The order matches the printf except that the cone
group comes before the clip group. Supporting evidence: Md < Fr always, cone angles are 180, CnOutVol is in [0,1], the
client check is exact.

The exporter always writes ClpFr = ClpMd + ε and ClpMdVol = 0. In practice this gives **one segment: full volume up to
Near, 0 at Mid ≈ Far** [INF].

---------------------------------------------------------------------------------------------------
## 4. Emitters

### 4.1 Entity__Fx (cinematic bank) [DATA]

`Entity__Fx { __Hc; __Name; IDClass; orient_pos_scale {rot quat, pos, scale}; …; UINT64 Fx (Fx__Root); BOOL AutoStart }`.
The position is the emitter position in the bank frame (the same frame as the .sce actors):
- S_TimeStop (0, 33.32, 0)
- S_WALKMODE_Music (2.00, 32.27, -11.14)
- FxS_WATER (-1.14, 31.25, 0.75)

The house floor is at y ≈ 31. They are started by `FUN_00b17b70(entity, -1.0)`, which goes into edFx (0x5eXXXX
**[ENC]**), and edFx creates the edFxSound / edAudio source.

### 4.2 .cin FX record (0x54 bytes) [DATA]

`{u32 ref; u32 flags; f32 0; f32 duration; f32 0; f32[16] D3D row-major world matrix (translation = row 3)}`.
The flags are **0x37 for the music** (identity matrix) and **0x27** for the whooshes and the logo. Only bit **0x10**
differs. Its meaning is **[ENC]**. It is the best candidate for a "2D / not positional" bit [INF]. The .cin track
reader is also [ENC]: the constant 0x7846 does not occur in readable code.

The 0x27 positions do not lie on the camera path:
- woosh01_00 is at (155.3, -235.4, -48.9) while the generique camera at that time is at (-1011.6, 3.6, -856.3);
- the logo is at (340.6, -1001, -1290) while the camera is at (-163, -2.5, -253).

That is more than 700 units away, while ClipFar = 40. **If these sounds were distance-attenuated they would be
silent.** So the "Cinematic" client is played as plain stereo, without distance attenuation [INF]. This is consistent
with its 2-input pipeline (§2.1). It should be checked against a capture of the original game.

### 4.3 SCSoundEmitter (SC_SoundEmiter.cpp) [CODE]

Loader FUN_00b3af90. Each field is read with default -1.0 (0xede404) unless noted:

| xmb field | offset | ctor default (FUN_00b3a660) |
|---|---|---|
| Ressource (sound rsc) | +0xe8 | 0 |
| Volume | +0xf0 | 1.0 |
| VolumeRandomness | +0x10c | 0 |
| Frequency | +0x108 | 1.0 |
| FrequencyRandomness | +0x104 | 0 |
| DopplerFactor | +0xfc | |
| ClipNearDistance | +0xf4 | 5.0 |
| ClipFarDistance | +0xf8 | 1000.0 |
| Loop (bool) | +0x100 | 0 |
| (runtime) random seed | +0x114 | 12345, then reseeded |
| (runtime) source | +0x110 | |
| (runtime) ambiance mode | +0x120 | 0 |
| (runtime) left/linked | +0x121 | |

Missing xmb fields overwrite the defaults with -1. Treat any value < 0 as "use the default" [INF].

Normal start, FUN_00b3ac00 (when +0x120 == 0):
1. `pos = entity matrix translation (+0x40,+0x44,+0x48)`. If the emitter is parented (+0xc), it is transformed by
   the parent matrix (0x42a7d0 / 0x405210).
2. `pitch = RandPitch(FrequencyRandomness, Frequency)`; `vol = RandVol(VolumeRandomness, Volume)`.
3. `src = 0x40aca0()` (allocated from the "CinematicSoundEmiter" pool), name "SCSoundEmitter";
   `0x575620(src, -1)` binds the sample.
4. `0x5787b0(src, &pos4, 0)`: **SetPosition, world space (flag 0)**.
5. `0x578430(src, vol)`, `0x578820(src, DopplerFactor)`, `0x578860(src, ClipNear, ClipFar)`, `0x578440(src, pitch)`.
6. `src+0xcc = Loop ? -1 : 0` (loop count); `0x575900(src, 0.0)` starts it.

The position is set **only when the source is created**, so an SC emitter is static.

Ambiance start, FUN_00b3aa70 (when +0x120 != 0):
- `pos = ( +0x121 ? -5.0 : +5.0, 0.1, 0.1, 0 )` and `0x5787b0(src, &pos, 1)`: **flag 1 = listener-relative** [INF:
  the vehicle code writes the same source fields directly, +0x2e0 = pos and byte +0x2f0 = 0].
- `0x578430(src, Volume * 0.5)`; then Doppler, Clip near/far and Frequency as above (no randomness).
- If +0x121: `0x575770(src, &emitter+0xe0)` [ENC]. The stop path (0xb3a510) also calls `0x5757a0` when +0x121 is
  set. This looks like the left emitter being linked/synchronised with its pair [INF].

The cinematic loader FUN_00b406e0 reads `Ambiance_SoundEmiter1` (+0x5d0) and `Ambiance_SoundEmiter2` (+0x5e0).
These are the two emitters of the pair [INF: the code that sets +0x120/+0x121 is not in readable code]. In the intro
banks (26 Entity__Cinematic) both are 0, so they are unused there.

Randomness [CODE]. The LCG is `s = s*0x7ff8a3ed + 0x2aa01d31` (emitter+0x114), and `u = (uint32)s / 2^32`.
- `RandVol(VR, V)` (0xb3a180) = `VR == 0 ? V : clamp(V + VR*u - 0.5*VR, 0, 1)`
- `RandPitch(FR, F)` (0xb3a230): `r = 2u - 1`. If FR == 0 → F. Else if r >= 0 → `F*(1 + r*FR)`. Else
  `r*F + (1 - r)*F/(1 + FR)` (traced from the x87 stack; the negative branch looks odd and is worth rechecking).

### 4.4 Vehicles (for reference) [CODE+DATA]

CarVSTConfig.xmb instances have `fClipNear 10, fClipFar 500, fRefractionFactor 0, fTrackingClipNear/Far/RefractionFactor,
TrackingClippingFactors[7], InstancePosition`. The loader at 0xaff2bb puts them at +0x78…+0x8c.

Per-frame update 0xaffdf0 on the playing source:
- `0x578430(vol)`, `0x578440(pitch)`;
- `0x578860(near=+0x108, far=+0x10c)`;
- **`0x578830(xmm1 = +0x110, the RefractionFactor)`**;
- `src+0x2e0..0x2e8 = position`, `src+0x2f0 = 0`, `src+0x300..0x308 = velocity`;
- `0x578820(Doppler = +0x114)`.

Other users of 0x578860/0x578830: boat horn (50, 2000, 0.8), helicopter (…, 0.7), Woosh (globals 0x10251ec…f8).
Ambiences (ambiences.xmb): `ClipNear 5, ClipFar 50…500, Attenuation 0`.
**So the third distance parameter (bfx ClpAttn / vehicle RefractionFactor / ambience Attenuation) is 0 in nearly all
data.** Its meaning is [ENC].

### 4.5 Source spatial API (callee addresses, [ENC] bodies, semantics from the call sites)

| function | args | meaning |
|---|---|---|
| 0x40aca0 | – | allocate a source from the current pool (returns obj, name at +0x1a) |
| 0x575620 | (src, sample, -1) | bind sample / voice |
| 0x5787b0 | eax=src, ecx=&vec4, push relFlag | SetPosition (+0x2e0), listener-relative flag (+0x2f0) |
| 0x578430 | eax=src, xmm0 | SetVolume (linear) |
| 0x578440 | eax=src, xmm0 | SetPitch (ratio) |
| 0x578860 | eax=src, xmm0=near, [esp]=far | SetClipDistances |
| 0x578830 | eax=src, xmm1 | SetRefraction / attenuation factor (0..1) |
| 0x578820 | eax=src, xmm0 | SetDopplerFactor |
| src+0x300 | vec3 | velocity |
| src+0xcc | i32 | loop count (-1 = infinite) |
| 0x575900 | (src, startTime) | Play |
| 0x575a70 / 0x575d70 | | stop / release |

### 4.6 Spot exterior fx loops (fountains) [DATA + CODE; attenuation INF]

The looping sounds of the fountains are **not** ambience banks (`islands/<island>/sound/ambience/ambiences.xmb` is
the zone ambience: day / night beds, thunder) and not SC emitters: they are the " DNS" nodes of the `Entity__Fx`
effects of the spot exterior bank itself [DATA]:

- ecas.bnk: `Fx__SourceData` VFX_BigFountain / VFX_MidFountain, `SoundList[] { SoundNode }` -> `Fx__Sound`
  `SoundFile` -> `sound\casino\sfx\s_fountain_loop_01_AMBIENCE_EXTERNAL_SFX.wav` (41985 bytes) / `_02_` (24019
  bytes), bank group 10 / sub 6, **in ecas.bnk** (no separate sound bank).
- .bfx " DNS" node (vfx_casino_F1E3... / 8C2B...): name `S_fountain_loop_01/02`, LoopCount (+0x5c) -1, Vol
  **0.708** (-3 dB), Ptch 1, CnIn / CnOut **179.98**, CnOutVol 0.25, **ClpNr 10, ClpMd 20, ClpFr 40**, ClpMdVol 0,
  ClpAttn 0, client **14 = AmbienceExternalSfx** (pipeline Sfx, 5 inputs: positional). Node matrix identity: the
  source is at the entity position.
- 6 entities (`V_Fx_Fountain_*`, AutoStart 1): world positions with the ECAS spot frame (2642.0, 1.7, 1588.1):
  Big (2709.3, 3.4, 1475.1), Big_01 (2748.8, 3.4, 1511.7), Middle (2704.1, 3.1, 1465.2), Middle_01 (2720.1, 3.1,
  1480.0), Middle_02 (2743.1, 3.1, 1501.4), Middle_03 (2759.0, 3.1, 1516.1).
- The other banks with Entity__Fx: e_hotela (water sheets) and eph_l3a (butterflies) have no SoundList entry
  [DATA]; the Ibiza vfx_env.bnk ambient effects have none either.
- Start: Entity__Fx loader FUN_00b18040 (+0xe8 Fx root hash, +0xfc AutoStart) and FUN_00b17b70 (FUN_0048e2f0 ->
  FUN_00a9ba20 / FUN_00b17ac0: the edFx instance; the edFxSound source is created by edFx [ENC]) [CODE].

Waterfalls (Hawai) [DATA]: the splash is a world ambient effect (`VFX_EnvWaterfall_Splash`, hawai vfx_env.bnk, 22
`-F.pmi` placements, no SoundList: `particles_spec.md` 7); the water noise is the **ambience environment** TypeID 11
`Cascade` of `islands/hawai/sound/ambience/ambiences.xmb` (`waterfall_08.wav` day and night, ClassID 8,
PriorityRank 3, ClipNear 5, ClipFar 500, Attenuation 0.999, Volume 1). The ambience manager FUN_00cc13c0 loads
`%sIslands/%s/Sound/Ambience/Ambiences.xmb` (Environments, ReverbPresets, Rain, Lightning) and FUN_00cbf3e0
swaps the day / night resource of an environment (`%sIslands/%s/Sound/Ambience/%s.wav`) [CODE]; where an
environment applies on the map (the zone placement of TypeID 11) was not decoded in this pass: **not implemented**
(open).

OpenTDU2: `SpotExterior::startSounds` (with the particle effects, World Viewer): `CinematicSounds` on the exterior
bank (same Fx chain and .bfx parser as the cinematics), one looped 3D voice per AutoStart Entity__Fx and spot frame
at `frame * entity position`; stopped with the exterior. Listener = the free-fly camera (`setListener` every tick),
mix preset of the game mode FREERIDE [INF: the tool stands for the free roaming mode]. Distance gain: the [INF]
curve of 7 (1 up to 10 m, 0 at 20 m).

Test (World Viewer, casino ECAS, `-campos 2733,6,1482 -camyaw 35`, `OTDU_AUDIO_LOG=1 -audiodump`): 6 voices
created (client 14, bus Sfx); Middle_01 at 13.4 m -> attenuation 0.66, gains BL 0.06 / BR 0.22 (the source is
behind right of the camera), the others beyond 20 m -> 0; the 3.33 s offline mix has RMS L 0.0020 / R 0.0071
(`scratchpad/fx3/cas2.log`; the WAV dump was deleted with the staged data).

Test without an audio device: `-audiodump <file.wav>` runs the mixer offline (48 kHz stereo float), advanced by
the game time step, and writes the mix; `OTDU_AUDIO_LOG=1` logs every voice (distance, attenuation, bus gains)
once a second.

---------------------------------------------------------------------------------------------------
## 5. Listener [CODE]

`GSAudio::SetListener` = **FUN_00d944b0** (eax = &forward vec4, [esp+4] = &position, edi = &velocity). It writes
the listener struct at **GSAudio+0x940 (0x010013d0)** and calls `0x577100(0x010013d0)` [ENC] to push it to edAudio:

```
f = (fwd.x, 0, fwd.z)                         // y dropped
if (f.x*f.x + f.z*f.z <= 1e-5) f = (0,0,1) else f = normalize(f)   // 0x40dae0
+0x10 (0x10013e0) position = *pos
+0x20 (0x10013f0) forward  = f
+0x30 (0x1001400) up       = (0, 1, 0)
+0x40 (0x1001410) right    = cross(up, forward)   // 0x40d9e0(ecx=up, arg=fwd); +X when fwd=+Z (D3D LH)
+0x50 (0x1001420) velocity = *vel
```

Callers:
1. **Camera listener**, FUN_00c3e530 at 0xc3e74a. This runs only if **`GSAudio+0x938` (0x010013c8, byte, default 1)
   != 0** and `[0xfd51c0]+0x420 != 0`. Camera = `*(0x01195bf0)`:
   - position = camera+0x310;
   - velocity = camera+0x360 [INF: velocity];
   - forward = 0x43dae0(camera) [ENC, presumably the view direction].
2. **Micro listener**, FUN_00b3b330 (SetPos) / FUN_00b3b480 (SetMatrix) of the scenaric object base class. When an
   object with byte **+0xe0 = 1** moves:
   - position = world position (local + world offset 0x01367a10, or 0x01178130 if byte 0x0117810c);
   - forward = its matrix Z row (+0x20..+0x28);
   - velocity = 0.

   The cinematic actor update (vfunc at 0xb3b240) sets `actor+0xe0 = (actor == player+0x84)`.
   `FUN_00b3ff00` (cinematic start) copies **Micro (cinematic +0x5a0) → player(+0xe0)+0x84** and sets
   **0x010013c8 = 0**, which disables the camera listener. That happens only if Micro != 0. `FUN_00b3fa70`
   (stop, 0xb3fb7c) sets it back to 1.

So in a cinematic without Micro (all intro cinematics: Micro = 0), the listener is the camera that the cinematic
drives.

---------------------------------------------------------------------------------------------------
## 6. What stays unknown ([ENC])

- The distance attenuation curve, the use of ClpMd/ClpMdVol/ClpAttn and of the Refraction/Attenuation factor.
- The pan law and the speaker angles of the 5-channel spatializer, and whether elevation is used (the listener up is
  fixed and the forward is horizontal, so most likely azimuth only).
- The Doppler formula, the speed of sound, and the defaults of the global Doppler and Spatial factors.
- The cone evaluation (data: CnIn = CnOut = 180, so omnidirectional in practice).
- The meaning of the .cin FX flags (0x10), and how edFxSound treats a stereo source in the spatial ("IsSpc") path.

---------------------------------------------------------------------------------------------------
## 7. Model to implement in OpenTDU2 (follows the data; [INF] where noted)

Coordinates: the game space is D3D left-handed (+Z forward, +Y up). The .cin FX matrix is row-major with the
translation in the last row. Entity__Fx positions are `orient_pos_scale.pos`. The listener comes from the cinematic
camera:
- position = camera world position;
- forward = R(q*)·(0,0,1) (intro_spec §7.8), y set to 0 and normalized.

Per voice:

```
if client pipeline has 2 inputs (Cinematic, CinematicMusic, AmbienceInternal, FrontEnd…) or .cin flag 0x10:
    2D: gains = routing matrix of the pipeline (stereo → identity), no attenuation.     [DATA + INF]
else (Sfx clients: AmbienceInternalSfx, CinematicSfx, Woosh, AnimationSfx, PhysicSfx…):
    d    = |srcPos - listenerPos|          (relative sources: srcPos = listenerPos + R_listener·relPos)
    dist = d <= Near ? 1
         : d <= Mid  ? lerp(1, MidVol, (d-Near)/(Mid-Near))
         : d <  Far  ? lerp(MidVol, 0, (d-Mid)/(Far-Mid))
         : 0                                                                              [INF]
    (alternative to A/B test: inverse distance Near/d, cut at Far)
    v    = srcPos - listenerPos ;  az = atan2(dot(v,right), dot(v,forward))   // elevation ignored [INF]
    cone: if CnOut < 180, standard inner/outer cone with CnOutVol (omni at 180)            [INF]
    5-ch pan (FL -30°, FR +30°, C 0°, BL -110°, BR +110°): pairwise constant-power panning
    between the two speakers adjacent to az; if d < Near/2, blend toward an equal spread   [INF]
    then pipeline matrix (Sfx) → device layout (stereo: L = .501(FL+BL+C), R = .501(FR+BR+C))  [DATA]
gain = bfxVol * clientPresetGain * mixerGain * duckingGain * dist * cone
pitch = bfxPtch * doppler,  doppler = (c - DF*vl·u)/(c - DF*vs·u), c = 343 m/s, u = unit(src - listener) [INF]
```

A stereo clip on a spatial client is downmixed to mono before panning [INF]. Spatial sources in the game assets are
mono "_mono" files; the intro AMBIENCE_INTERNAL_SFX wavs are stereo 22 kHz.

What this gives for the intro (bank new_intro.bnk):

| sound | client | handling | Near / Far |
|---|---|---|---|
| c_intro (FxC_INTRO, .cin flag 0x37) | 22 Cinematic | 2D stereo, gain 1.0 | – |
| woosh01 x2, s_tdu2_logo (.cin 0x27) | 22 Cinematic | 2D stereo (see §4.2), gain 0.501 / 0.631 | (4 / 40 unused) |
| S_TimeStop / S_TimeStart, C_AVATAR | 22 Cinematic | 2D stereo | (10 or 4 / 40) |
| S_WALKMODE_Music(_01.._03) at (2.0,32.3,-11.1)… | 11 AmbienceInternalSfx | **3D**, preset Intro gain **0.3**, looped | 20 / 50 |
| FxS_WATER at (-1.14,31.25,0.75) | 11 AmbienceInternalSfx | **3D**, looped | 5 / 20 |
| Intro_Fiesta*_VOICE_STORYLINE | 0 VoiceStoryline | 2D center (mono pipeline: L = R = 0.501), preset gain **1.5**, ducks Cinematic to 0.75 and AmbIntSfx to 0.3 | – |

---------------------------------------------------------------------------------------------------
## 8. Implementation plan (OpenTDU2, SDL3; no change made to the sources)

1. **gs_audio: own mixer instead of one bound SDL_AudioStream per voice.** SDL_AudioStream only has a scalar gain;
   per-channel pan needs a mixer.
   - Open `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, {F32, 2|6|8 ch, 48000}, MixCallback, this)`.
   - Read the real channel count with `SDL_GetAudioDeviceFormat`.
   - Decode at `loadClip` time to F32 at the device rate with `SDL_ConvertAudioSamples`. Keep the source channel count
     (1 or 2).
   - In the callback, for each voice, read frames with a fractional step (`pitch * doppler`), using linear
     interpolation. Accumulate into a 5-channel spatial bus or a 2-channel stereo bus per pipeline, with per-channel
     gains ramped linearly over the buffer (no zipper noise).
   - Then apply the pipeline routing matrix (§2.1, dB → linear) to the device layout. Map the edAudio order to
     SDL's: SDL 5.1 = FL FR FC LFE BL BR, so edAudio {FL,FR,BL,BR,C,LFE} → SDL {0,1,4,5,2,3}. SDL 7.1 = FL FR FC LFE
     BL BR SL SR, so edAudio {FL,FR,BL,BR,SL,SR,C,LFE} → {0,1,4,5,6,7,2,3}.
   - Protect the voice parameters with a mutex (or double-buffered snapshot) between `tick()` and the callback.
2. **New GSAudio API**:
   - `setListener(pos, forward, velocity)`: apply §5 (flatten y, up = (0,1,0), right = up x fwd).
   - `play(clip, VoiceParams)` with `VoiceParams { client, volume, pitch, loop, startTime, bSpatial, bRelative,
     pos, vel, near, mid, far, midVol, attn, coneIn, coneOut, coneOutVol, doppler }`.
   - `setVoicePosition(handle, pos, vel)`.
   - Per frame in `tick()`: compute dist/pan/cone/doppler gains (§7). Keep `setVolume`/`stop` as they are.
3. **audiosetup.xmb loader** (the XmbDocument already exists): pipelines (routing matrices), clients (pipeline,
   IsMusic), mixers, mix presets (client/mixer gains), ducking presets (attack/sustain/sustainGain/release envelope
   per trigger client), game modes. API `setGameMode("AVATAR_CHOICE")` → preset Intro. Fallback when the file is
   missing: Cinematic = stereo, other spatial clients = Sfx matrix, all gains 1.
4. **CinematicSounds**:
   - Parse the full SND node (§3): Vol, Ptch, VolRnd, PtchRnd, cone, Clip Nr/Md/Fr/MdVol/Attn, client.
   - For `LSGL` voice .bfx, pick the node of the current language (prefix US_/FR_/…) instead of reading +0xac of the
     first node.
   - FX events from `cinematic.getFxEvents()` need the record flags and the matrix (row 3 = position). This means
     extending the .cin parser. Use 2D if the client pipeline is stereo or flag & 0x10.
   - Entity__Fx emitters: keep `orient_pos_scale.pos`; spatial when the client is Sfx.
   - `update()` gets the listener from the cinematic camera each frame:
     `gpAudio->setListener(camPos, camForward, (camPos - prevCamPos)/dt)`.
   - If the cinematic Micro != 0, use the matching actor's position and Z axis with velocity 0 instead (§5).
5. **Ambiance emitters** (later cinematics): two looped SC emitters, listener-relative at (-5,0.1,0.1) and
   (+5,0.1,0.1), each at Volume·0.5, with Clip near/far from the emitter (defaults 5/1000), started in sync.
6. **Debug and validation**:
   - an env switch `OTDU_AUDIO_3D=0/1` and a toggle for "Cinematic client 2D";
   - log the per-voice distance and gains;
   - check by ear against a capture of the original game (whooshes, S_TimeStop, house music while the camera moves
     between rooms). This is especially needed for the curve shape (§7 alternatives) and §4.2.

---------------------------------------------------------------------------------------------------
## 9. Address index

| VA | what |
|---|---|
| 0x00d95350 / 0x00d95770 / 0x00d94460 / 0x00d94e30 | GSAudio ctor / Init / Update / Terminate |
| 0x00d95640 | load Sound/audioSetup.xmb (→ 0xd93e20, 0xd954a0, 0xd94c90, 0xd94b10, 0xd94990, 0xd94830) |
| 0x00d944b0 | SetListener (listener struct 0x010013d0) |
| 0x00c3e530 (0xc3e74a) | camera → listener (gated by 0x010013c8) |
| 0x00b3b240 / 0x00b3b330 / 0x00b3b480 | cinematic actor update (micro flag +0xe0) / SetPos / SetMatrix → listener |
| 0x00b3ff00 / 0x00b3fa70 | cinematic start (Micro → player+0x84, listener flag = 0) / stop (flag = 1) |
| 0x00b406e0 | Entity__Cinematic loader (+0x5a0 Micro, +0x5d0/+0x5e0 Ambiance emitters) |
| 0x00b3af90 / 0x00b3a660 | SCSoundEmitter loader / ctor |
| 0x00b3ac00 / 0x00b3aa70 / 0x00b3a510 | SC emitter start (world) / start (ambiance, listener-relative) / stop |
| 0x00b3a180 / 0x00b3a230 | volume / pitch randomness |
| 0x00d918f0, 0x00b39a70 | CinematicSound pools ("CinematicSound" 0x64000, "CinematicSoundEmiter" 0x96000) |
| 0x00aff2bb / 0x00affdf0 | vehicle VST instance loader / per-frame spatial params |
| 0x00d69c60 / 0x00d69c80 / 0x00d69ca0 / 0x00d69cc0 | SETOUTPUT DOPPLER / SPATIAL FACTOR / NBSPEAKERS / DRIVER |
| 0x00f90ac0 | client name table (bfx +0xdc index) |
| 0x00f49278 | edFxSound debug format (parameter names) |
