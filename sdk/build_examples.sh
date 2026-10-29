#!/usr/bin/env bash
# Builds every example plugin whose toolchain is installed and copies the libraries
# into mods/plugin_demo/plugins/. Run from anywhere:  sdk/build_examples.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../mods/plugin_demo/plugins"
mkdir -p "$OUT"
case "$(uname -s)" in
  Darwin) EXT=dylib; RID=osx-$( [ "$(uname -m)" = arm64 ] && echo arm64 || echo x64 ) ;;
  *)      EXT=so;    RID=linux-$( [ "$(uname -m)" = aarch64 ] && echo arm64 || echo x64 ) ;;
esac
ok=0

echo "== C"
if command -v cc >/dev/null; then
  cc -shared -fPIC -O2 -I"$HERE/include" "$HERE/examples/c_plugin/hello.c" -o "$OUT/c_hello.$EXT" -lm && ok=$((ok+1))
else echo "   cc not found, skipped"; fi

echo "== C++"
if command -v cmake >/dev/null; then
  cmake -S "$HERE/examples/cpp_plugin" -B "$HERE/examples/cpp_plugin/build" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build "$HERE/examples/cpp_plugin/build" --config Release && ok=$((ok+1))
else echo "   cmake not found, skipped"; fi

echo "== Rust"
if command -v cargo >/dev/null; then
  (cd "$HERE/examples/rust_plugin" && cargo build --release) &&
  cp "$HERE/examples/rust_plugin/target/release/librust_plugin.$EXT" "$OUT/" && ok=$((ok+1))
else echo "   cargo not found, skipped (https://rustup.rs)"; fi

echo "== C# (NativeAOT)"
if command -v dotnet >/dev/null; then
  (cd "$HERE/examples/csharp_plugin" && DOTNET_CLI_TELEMETRY_OPTOUT=1 dotnet publish -c Release -r "$RID") &&
  cp "$HERE/examples/csharp_plugin/bin/Release/net8.0/$RID/publish/csharp_plugin.$EXT" "$OUT/" && ok=$((ok+1))
else echo "   dotnet not found, skipped (https://dotnet.microsoft.com - .NET 8 SDK)"; fi

echo
echo "Built $ok plugin(s) into $OUT"
echo "Run:  rbs --mod plugin_demo"
