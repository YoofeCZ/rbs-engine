# Native plugin demo

Entity classes and components written in **C, C++, Rust and C#**, all running side by side
with the Lua game code. The sources are in [`sdk/examples`](../../sdk/examples).

1. Build the plugins (only the languages you have installed are built):
   - Linux / macOS: `sdk/build_examples.sh`
   - Windows: `sdk\build_examples.ps1` in a *Developer PowerShell for VS*
2. Start the demo: `rbs --mod plugin_demo` (or **Mods → plugin_demo → New game**)

| Class / component | Language | Source |
|---|---|---|
| `c_beacon` | C | `sdk/examples/c_plugin/hello.c` |
| `cpp_turret`, `cpp_spinner` | C++ | `sdk/examples/cpp_plugin/turret.cpp` |
| `rust_heal_station`, `rust_hover` | Rust | `sdk/examples/rust_plugin/src/lib.rs` |
| `csharp_launch_pad`, `csharp_pulse_light` | C# (NativeAOT) | `sdk/examples/csharp_plugin/Plugin.cs` |

The compiled libraries go to `plugins/` and are not stored in git.
