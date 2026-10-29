# Builds the example plugins on Windows (run from a "Developer PowerShell for VS").
# Copies the DLLs into mods\plugin_demo\plugins\.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $here "..\mods\plugin_demo\plugins"
New-Item -ItemType Directory -Force -Path $out | Out-Null

Write-Host "== C"
if (Get-Command cl -ErrorAction SilentlyContinue) {
  Push-Location (Join-Path $here "examples\c_plugin")
  cl /nologo /LD /O2 /I..\..\include hello.c /Fe:"$out\c_hello.dll"
  Pop-Location
} else { Write-Host "   cl.exe not found (open a Developer PowerShell), skipped" }

Write-Host "== C++"
if (Get-Command cmake -ErrorAction SilentlyContinue) {
  cmake -S (Join-Path $here "examples\cpp_plugin") -B (Join-Path $here "examples\cpp_plugin\build")
  cmake --build (Join-Path $here "examples\cpp_plugin\build") --config Release
}

Write-Host "== Rust"
if (Get-Command cargo -ErrorAction SilentlyContinue) {
  Push-Location (Join-Path $here "examples\rust_plugin")
  cargo build --release
  Copy-Item target\release\rust_plugin.dll $out
  Pop-Location
} else { Write-Host "   cargo not found, skipped" }

Write-Host "== C#"
if (Get-Command dotnet -ErrorAction SilentlyContinue) {
  Push-Location (Join-Path $here "examples\csharp_plugin")
  dotnet publish -c Release -r win-x64
  Copy-Item bin\Release\net8.0\win-x64\publish\csharp_plugin.dll $out
  Pop-Location
} else { Write-Host "   dotnet not found, skipped" }
Write-Host "Done. Run: rbs.exe --mod plugin_demo"
