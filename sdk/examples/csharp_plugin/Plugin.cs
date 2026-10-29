// Example RBS Engine plugin in C#: a launch pad entity and a "pulse light" component.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Rbs;

namespace RbsPlugin;

/// <summary>Throws the player up into the air when they step on it.</summary>
public sealed class LaunchPad : Behaviour
{
    float power;
    float nextUse;

    public override void Spawn()
    {
        power = PropF("power", 750);
        Self.SetStr("sprite", "sprites/flare.png");
        Self.Set("sprite_size", 48);
        Self.SetVec("mins", new Vec3(-24, -4, -24));
        Self.SetVec("maxs", new Vec3(24, 12, 24));
        Self.SetStr("solid", "trigger");
        Self.Set("light_radius", 140);
        Self.SetVec("light_color", new Vec3(0.3f, 0.8f, 1.5f));
    }

    public override void Update(float dt)
    {
        float k = 0.7f + 0.3f * MathF.Sin(Engine.Time * 5f);
        Self.SetVec("light_color", new Vec3(0.3f * k, 0.8f * k, 1.5f * k));
    }

    public override void Touch(Entity other, Vec3 normal)
    {
        if (other.Id != Engine.Player.Id || Engine.Time < nextUse) return;
        nextUse = Engine.Time + 0.5f;
        var v = other.Vel;
        other.Vel = new Vec3(v.X * 1.2f, power, v.Z * 1.2f);
        Engine.Sound("sounds/jumppad.wav", Self.Pos, 1f, 1f);
        Engine.Particles(Self.Pos, new Vec3(0, 1, 0), 20, 0x60C0FFFF, 300, 4, 0.6f);
    }
}

/// <summary>Component: attaches a pulsing light to any entity.</summary>
public sealed class PulseLight : Behaviour
{
    public override void Spawn() => Self.Set("light_radius", PropF("radius", 200));
    public override void Update(float dt)
    {
        float k = 0.5f + 0.5f * MathF.Sin(Engine.Time * PropF("speed", 4));
        Self.SetVec("light_color", new Vec3(1.5f * k, 0.4f * k, 1.2f * k));
    }
}

public static unsafe class Plugin
{
    [UnmanagedCallersOnly(EntryPoint = "rbs_plugin_init", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Init(Api* api)
    {
        if (api == null || api->Version < 1) return 1;
        Engine.A = api;
        Engine.Log("C# example plugin loaded");
        Engine.RegisterClass("csharp_launch_pad",
            """{"desc": "Launch pad written in C# (sdk/examples/csharp_plugin).", "category": "triggers", "color": [80, 190, 255], "mins": [-24, -4, -24], "maxs": [24, 12, 24], "props": {"power": 750}}""",
            () => new LaunchPad());
        Engine.RegisterComponent("csharp_pulse_light",
            """{"desc": "Pulsing purple light (C# component).", "props": {"radius": 200, "speed": 4}}""",
            () => new PulseLight());
        return 0;
    }

    [UnmanagedCallersOnly(EntryPoint = "rbs_plugin_shutdown", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void Shutdown() { }
}
