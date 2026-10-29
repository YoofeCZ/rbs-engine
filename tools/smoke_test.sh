#!/usr/bin/env bash
# Headless smoke test (Linux, needs xvfb-run): starts the demo map, the editor and both
# example mods, creates a new game project and exports it as a standalone game, saves a
# screenshot of each and fails on script errors, missing assets or plugins that did not load.
#   tools/smoke_test.sh [path/to/rbs] [output folder]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/rbs}"
OUT="${2:-$ROOT/build/smoke}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
status=0
BAD='Lua error|^Lua:|Lua prelude failed|init.lua not found|Script initialisation failed|Map not found|Cannot load map|Shader (files missing|failed)|^Plugin |Missing (texture|image|model|sound|loop sound)|Unknown (entity class|component|weapon)|disabled after error|unknown property|invalid editor_json'

# run <name> <WxH> [args...]; RUN_BIN / RUN_DIR pick another executable / working folder
run() {
    local name=$1 size=$2
    shift 2
    echo "== $name: $(basename "${RUN_BIN:-$BIN}") $*"
    if ! (cd "${RUN_DIR:-.}" && timeout 900 xvfb-run -a -s "-screen 0 ${size}x24" "${RUN_BIN:-$BIN}" "$@" --screenshot "$OUT/$name.png") >"$OUT/$name.log" 2>&1; then
        echo "   FAILED: rbs exited with an error (see $OUT/$name.log)"
        status=1
    fi
    if [ ! -s "$OUT/$name.png" ]; then
        echo "   FAILED: no screenshot"
        status=1
    fi
    if grep -E "$BAD" "$OUT/$name.log"; then
        echo "   FAILED: errors in the log above"
        status=1
    fi
}

run game 1280x720 --map e1m1 --frames 60
run editor 1600x900 --editor e1m1 --frames 30
run lua_mod 1280x720 --mod example_mod --map mod_arena --frames 60
run plugins 1280x720 --mod plugin_demo --map plugin_test --frames 60

# whichever example plugins were built must have loaded
for lib in "$ROOT"/mods/plugin_demo/plugins/*.{so,dylib}; do
    [ -e "$lib" ] || continue
    name=$(basename "$lib")
    if ! grep -q "Loading plugin plugins/$name" "$OUT/plugins.log"; then
        echo "   FAILED: plugin $name was not loaded"
        status=1
    fi
done
grep "\[plugin\]" "$OUT/plugins.log" || true

# a new game project, exported as a standalone game that has to start its own map on its
# own data, whatever folder it is started from
PROJECT=smoke_project
GAME_DIR="$(mktemp -d)/game"
rm -rf "$ROOT/mods/$PROJECT"
run new_project 1600x900 --exec "new_project $PROJECT Smoke Project; build_game $GAME_DIR noeditor" --frames 30
rm -rf "$ROOT/mods/$PROJECT"
if ! grep -q "Built Smoke Project into" "$OUT/new_project.log"; then
    echo "   FAILED: the project was not built"
    status=1
elif [ ! -x "$GAME_DIR/Smoke_Project" ] || [ ! -f "$GAME_DIR/launch.json" ]; then
    echo "   FAILED: the build has no Smoke_Project executable or launch.json"
    status=1
else
    RUN_BIN="$GAME_DIR/Smoke_Project" RUN_DIR=/ run standalone 1280x720 --map start --frames 60
    if ! grep -q "Data root: $(cd "$GAME_DIR" && pwd -P)" "$OUT/standalone.log"; then
        echo "   FAILED: the standalone game did not use its own data folder"
        status=1
    fi
fi
rm -rf "$(dirname "$GAME_DIR")"

[ $status -eq 0 ] && echo "Smoke test passed, screenshots in $OUT"
exit $status
