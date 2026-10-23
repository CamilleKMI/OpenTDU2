#!/bin/sh
# OpenTDU2 world viewer: free-fly camera over an island (developer tool,
# game mode WORLD_VIEWER).
#
#   ./launch_world_viewer.sh [ibiza|hawai|casino] [extra opentdu2 options...]
#
# Default island: ibiza. Examples:
#   ./launch_world_viewer.sh casino
#   ./launch_world_viewer.sh hawai -game /Users/me/TestDrive2
#   ./launch_world_viewer.sh ibiza -spot EPH_L3DI -weather 18
#
# Game data (the folder that contains Euro/Bnk): -game <folder> if given,
# else $OTDU2_GAME_DIR, else the first folder containing Euro/Bnk among this
# repository's parent folders, a "TestDrive2" folder next to them, and
# ~/TestDrive2 (otherwise opentdu2 does its own search and reports it).
#
# Controls: right mouse button (or left button outside the panel) held to
# look, Tab to capture the mouse, W/S/A/D or arrows to move, E/Space up,
# Q/Ctrl down, Shift = speed x2, mouse wheel = base speed, F5/F6/F7 = Ibiza /
# Hawai / Casino, F2 = panel, F1 = debug UI.
set -e
cd "$(dirname "$0")"
ROOT="$(pwd)"

ISLAND="ibiza"
case "$1" in
    ibiza|Ibiza|IBIZA) ISLAND="ibiza"; shift ;;
    hawai|Hawai|HAWAI|hawaii|Hawaii|HAWAII|oahu|Oahu) ISLAND="hawai"; shift ;;
    casino|Casino|CASINO) ISLAND="casino"; shift ;;
    -h|--help)
        sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
        exit 0 ;;
    -*|"") ;;
    *)
        echo "unknown island '$1' (ibiza, hawai, casino)" >&2
        exit 1 ;;
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
echo "OpenTDU2 world viewer: island '$ISLAND'${GAME_DIR:+, game data '$GAME_DIR'}"
if [ -n "$GAME_DIR" ]; then
    exec "$BIN" -game "$GAME_DIR" -gm WORLD_VIEWER -island "$ISLAND" "$@"
else
    exec "$BIN" -gm WORLD_VIEWER -island "$ISLAND" "$@"
fi
