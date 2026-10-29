# Builds the example plugins on Windows (run from a "Developer PowerShell for VS") and
# copies the DLLs into mods\plugin_demo\plugins\. Languages whose toolchain is not
# installed are skipped; the script fails if an installed toolchain fails to build.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $here "..\mods\plugin_demo\plugins"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ok = 0
$failed = @()

function Step([string]$name, [scriptblock]$build) {
    Write-Host "== $name"
    try {
        & $build
        if ($LASTEXITCODE -ne 0) { throw "exit code $LASTEXITCODE" }
        $script:ok++
    } catch {
        Write-Warning "$name plugin failed: $_"
        $script:failed += $name
    }
}

if (Get-Command cl -ErrorAction SilentlyContinue) {
    Step "C" {
        # object file and import library go to build\, only the DLL into the mod
        $tmp = Join-Path $here "examples\c_plugin\build"
        New-Item -ItemType Directory -Force -Path $tmp | Out-Null
        cl /nologo /LD /O2 /I"$here\include" "$here\examples\c_plugin\hello.c" /Fo"$tmp\hello.obj" /Fe"$out\c_hello.dll" /link /IMPLIB:"$tmp\c_hello.lib"
    }
} else { Write-Host "== C: cl.exe not found (open a Developer PowerShell for VS), skipped" }

if (Get-Command cmake -ErrorAction SilentlyContinue) {
    Step "C++" {
        cmake -S (Join-Path $here "examples\cpp_plugin") -B (Join-Path $here "examples\cpp_plugin\build")
        if ($LASTEXITCODE -eq 0) { cmake --build (Join-Path $here "examples\cpp_plugin\build") --config Release }
    }
} else { Write-Host "== C++: cmake not found, skipped" }

if (Get-Command cargo -ErrorAction SilentlyContinue) {
    Step "Rust" {
        Push-Location (Join-Path $here "examples\rust_plugin")
        try {
            cargo build --release
            if ($LASTEXITCODE -eq 0) { Copy-Item target\release\rust_plugin.dll $out -ErrorAction Stop }
        } finally { Pop-Location }
    }
} else { Write-Host "== Rust: cargo not found, skipped (https://rustup.rs)" }

if (Get-Command dotnet -ErrorAction SilentlyContinue) {
    Step "C#" {
        Push-Location (Join-Path $here "examples\csharp_plugin")
        try {
            # explicit output folder: a Developer PowerShell sets Platform=x64, which moves bin\Release\...
            dotnet publish -c Release -r win-x64 -o bin\publish
            if ($LASTEXITCODE -eq 0) { Copy-Item bin\publish\csharp_plugin.dll $out -ErrorAction Stop }
        } finally { Pop-Location }
    }
} else { Write-Host "== C#: dotnet not found, skipped (https://dotnet.microsoft.com - .NET 8 SDK)" }

Write-Host ""
Write-Host "Built $ok plugin(s) into $out"
if ($failed.Count -gt 0) {
    Write-Host "Failed: $($failed -join ', ')"
    exit 1
}
Write-Host "Run:  rbs.exe --mod plugin_demo"
exit 0
