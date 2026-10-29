#!/usr/bin/env bash
# Builds every example plugin whose toolchain is installed and copies the libraries
# into mods/plugin_demo/plugins/. Languages without a toolchain are skipped; the script
# fails if an installed toolchain fails to build. Run from anywhere:  sdk/build_examples.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../mods/plugin_demo/plugins"
mkdir -p "$OUT"
case "$(uname -s)" in
  Darwin) EXT=dylib; RID=osx-$( [ "$(uname -m)" = arm64 ] && echo arm64 || echo x64 ) ;;
  *)      EXT=so;    RID=linux-$( [ "$(uname -m)" = aarch64 ] && echo arm64 || echo x64 ) ;;
esac
ok=0
failed=""
result() {  # name, exit status
  if [ "$2" -eq 0 ]; then ok=$((ok+1)); else failed="$failed $1"; echo "   $1 plugin FAILED"; fi
}

echo "== C"
if command -v cc >/dev/null; then
  cc -shared -fPIC -O2 -I"$HERE/include" "$HERE/examples/c_plugin/hello.c" -o "$OUT/c_hello.$EXT" -lm
  result C $?
else echo "   cc not found, skipped"; fi

echo "== C++"
if command -v cmake >/dev/null; then
  cmake -S "$HERE/examples/cpp_plugin" -B "$HERE/examples/cpp_plugin/build" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build "$HERE/examples/cpp_plugin/build" --config Release
  result C++ $?
else echo "   cmake not found, skipped"; fi

echo "== Rust"
if command -v cargo >/dev/null; then
  (cd "$HERE/examples/rust_plugin" && cargo build --release) &&
  cp "$HERE/examples/rust_plugin/target/release/librust_plugin.$EXT" "$OUT/"
  result Rust $?
else echo "   cargo not found, skipped (https://rustup.rs)"; fi

echo "== C# (NativeAOT)"
if command -v dotnet >/dev/null; then
  # explicit output folder: the default one depends on the Platform environment variable
  (cd "$HERE/examples/csharp_plugin" && DOTNET_CLI_TELEMETRY_OPTOUT=1 dotnet publish -c Release -r "$RID" -o bin/publish) &&
  cp "$HERE/examples/csharp_plugin/bin/publish/csharp_plugin.$EXT" "$OUT/"
  result C# $?
else echo "   dotnet not found, skipped (https://dotnet.microsoft.com - .NET 8 SDK)"; fi

echo
echo "Built $ok plugin(s) into $OUT"
if [ -n "$failed" ]; then
  echo "Failed:$failed"
  exit 1
fi
echo "Run:  rbs --mod plugin_demo"
