#!/bin/sh
# OpenTDU2 free drive: drive any installed car on the roads of an island,
# switch car at any time (developer / sandbox tool, game mode FREE_DRIVE,
# like OpenTDU's sandbox; the story FREERIDE is a separate mode).
#
#   ./launch_free_drive.sh [ibiza|hawai|casino] [car] [extra opentdu2 options...]
#
# Default island: ibiza, default car: California. The car is a database
# _car_file_name (Mustang_Fastback_68), an _id_car (371) or a word of its
# name (mustang, esprit, "lancia delta"). Examples:
#   ./launch_free_drive.sh
#   ./launch_free_drive.sh casino
#   ./launch_free_drive.sh ibiza esprit -time 19:30
#   ./launch_free_drive.sh mustang          # Ibiza, Ford Mustang
#   ./launch_free_drive.sh hawai 371 -game /Users/me/TestDrive2 -spot ECAS
#
# Game data (the folder that contains Euro/Bnk): -game <folder> if given,
# else $OTDU2_GAME_DIR, else the first folder containing Euro/Bnk among this
# repository's parent folders, a "TestDrive2" folder next to them, and
# ~/TestDrive2 (otherwise opentdu2 does its own search and reports it).
#
# Controls (no numpad needed): Up / W throttle, Down / S brake / reverse,
# Left / Right / A / D steer, Left Shift handbrake, C camera, 4 accessories
# (then 8 roof / 2 windows / 4 / 6 blinkers), F3 back on the road, F4 /
# Shift+F4 next / previous car, F8 colour, F9 lights, F5 / F6 / F7 Ibiza /
# Hawai / Casino, F2 panel (island, cars, colours, time / weather, spots),
# F1 debug UI. On a Mac laptop, F keys may need the fn key.
set -e
cd "$(dirname "$0")"
ROOT="$(pwd)"

ISLAND="ibiza"
case "$1" in
    ibiza|Ibiza|IBIZA) ISLAND="ibiza"; shift ;;
    hawai|Hawai|HAWAI|hawaii|Hawaii|HAWAII|oahu|Oahu) ISLAND="hawai"; shift ;;
    casino|Casino|CASINO) ISLAND="casino"; shift ;;
    -h|--help)
        sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
        exit 0 ;;
    *) ;; # not an island: the car (default island)
esac

# Next argument: the car (anything that is not an option).
CAR=""
case "$1" in
    -*|"") ;;
    *) CAR="$1"; shift ;;
esac

BIN="$ROOT/build/bin/opentdu2"
if [ ! -x "$BIN" ]; then
    echo "$BIN not found: build first with ./build_macos.sh (or cmake --build build)" >&2
    exit 1
fi

# -game passed by the user: keep it.
HAS_GAME=0
for arg in "$@"; do
    if [ "$arg" = "-game" ]; then
        HAS_GAME=1
    fi
done

GAME_DIR=""
if [ "$HAS_GAME" = "0" ]; then
    if [ -n "$OTDU2_GAME_DIR" ] && [ -d "$OTDU2_GAME_DIR/Euro/Bnk" ]; then
        GAME_DIR="$OTDU2_GAME_DIR"
    else
        DIR="$ROOT"
        while [ -z "$GAME_DIR" ]; do
            if [ -d "$DIR/Euro/Bnk" ]; then
                GAME_DIR="$DIR"
            elif [ -d "$DIR/TestDrive2/Euro/Bnk" ]; then
                GAME_DIR="$DIR/TestDrive2"
            fi
            PARENT="$(dirname "$DIR")"
            if [ "$PARENT" = "$DIR" ]; then
                break
            fi
            DIR="$PARENT"
        done
        if [ -z "$GAME_DIR" ] && [ -n "$HOME" ] && [ -d "$HOME/TestDrive2/Euro/Bnk" ]; then
            GAME_DIR="$HOME/TestDrive2"
        fi
    fi
fi

cd "$ROOT/build/bin"
echo "OpenTDU2 free drive: island '$ISLAND'${CAR:+, car '$CAR'}${GAME_DIR:+, game data '$GAME_DIR'}"
if [ -n "$GAME_DIR" ]; then
    set -- -game "$GAME_DIR" "$@"
fi
if [ -n "$CAR" ]; then
    exec "$BIN" "$@" -gm FREE_DRIVE -island "$ISLAND" -car "$CAR"
else
    exec "$BIN" "$@" -gm FREE_DRIVE -island "$ISLAND"
fi
