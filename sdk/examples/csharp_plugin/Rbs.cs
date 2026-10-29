// C# bindings for the RBS Engine plugin API (mirrors sdk/include/rbs_plugin.h).
// Field order of the structs below must match the C header exactly.
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Rbs;

[StructLayout(LayoutKind.Sequential)]
public struct Vec3
{
    public float X, Y, Z;
    public Vec3(float x, float y, float z) { X = x; Y = y; Z = z; }
    public static Vec3 operator +(Vec3 a, Vec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vec3 operator -(Vec3 a, Vec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vec3 operator *(Vec3 a, float s) => new(a.X * s, a.Y * s, a.Z * s);
    public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);
    public Vec3 Normalized => Length > 0 ? this * (1f / Length) : default;
    public override string ToString() => $"({X:0.0} {Y:0.0} {Z:0.0})";
}

[StructLayout(LayoutKind.Sequential)]
public struct TraceResult
{
    public float Fraction;
    public Vec3 Pos, Normal;
    public int Hit, StartSolid, Sky, Entity;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BehaviourDesc
{
    public void* User;
    public delegate* unmanaged[Cdecl]<void*, int, byte*, void*> Create;
    public delegate* unmanaged[Cdecl]<void*, int, void> Destroy;
    public delegate* unmanaged[Cdecl]<void*, int, void> Spawn;
    public delegate* unmanaged[Cdecl]<void*, int, float, void> Update;
    public delegate* unmanaged[Cdecl]<void*, int, int, Vec3, void> Touch;
    public delegate* unmanaged[Cdecl]<void*, int, int, void> Use;
    public delegate* unmanaged[Cdecl]<void*, int, float, int, Vec3, float> Damaged;
    public delegate* unmanaged[Cdecl]<void*, int, float, int, void> Pain;
    public delegate* unmanaged[Cdecl]<void*, int, int, float, void> Die;
    public delegate* unmanaged[Cdecl]<void*, int, int, void> Blocked;
    public byte* EditorJson;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct Api
{
    public uint Version, Size;
    public delegate* unmanaged[Cdecl]<byte*, void> Log;
    public delegate* unmanaged[Cdecl]<byte*, BehaviourDesc*, void> RegisterClass;
    public delegate* unmanaged[Cdecl]<byte*, BehaviourDesc*, void> RegisterComponent;
    public delegate* unmanaged[Cdecl]<byte*, void> Console;
    public delegate* unmanaged[Cdecl]<byte*, void> Lua;
    public delegate* unmanaged[Cdecl]<byte*, float> CvarGet;
    public delegate* unmanaged[Cdecl]<byte*, byte*, void> CvarSet;
    public delegate* unmanaged[Cdecl]<float> Time;
    public delegate* unmanaged[Cdecl]<float, float, float> Random;
    public delegate* unmanaged[Cdecl]<int> Player;
    public delegate* unmanaged[Cdecl]<byte*, Vec3, byte*, float, int> Spawn;
    public delegate* unmanaged[Cdecl]<int, void> Remove;
    public delegate* unmanaged[Cdecl]<int, int> Exists;
    public delegate* unmanaged[Cdecl]<int, byte*, float> GetFloat;
    public delegate* unmanaged[Cdecl]<int, byte*, float, void> SetFloat;
    public delegate* unmanaged[Cdecl]<int, byte*, Vec3> GetVec;
    public delegate* unmanaged[Cdecl]<int, byte*, Vec3, void> SetVec;
    public delegate* unmanaged[Cdecl]<int, byte*, byte*, int, int> GetString;
    public delegate* unmanaged[Cdecl]<int, byte*, byte*, void> SetString;
    public delegate* unmanaged[Cdecl]<int, byte*, byte*, int, int> GetProp;
    public delegate* unmanaged[Cdecl]<int, byte*, byte*, void> SetProp;
    public delegate* unmanaged[Cdecl]<byte*, int*, int, int> Find;
    public delegate* unmanaged[Cdecl]<byte*, int*, int, int> FindClass;
    public delegate* unmanaged[Cdecl]<Vec3, float, int*, int, int> FindRadius;
    public delegate* unmanaged[Cdecl]<int, Vec3, float, void> Walk;
    public delegate* unmanaged[Cdecl]<int, Vec3, float, float, void> Face;
    public delegate* unmanaged[Cdecl]<int, int, int> Visible;
    public delegate* unmanaged[Cdecl]<Vec3, Vec3, int, TraceResult> Trace;
    public delegate* unmanaged[Cdecl]<int, float, int, Vec3, void> Damage;
    public delegate* unmanaged[Cdecl]<Vec3, float, float, int, void> RadiusDamage;
    public delegate* unmanaged[Cdecl]<int, Vec3, Vec3, int, float, float, void> FireBullets;
    public delegate* unmanaged[Cdecl]<byte*, int, Vec3, Vec3, float, int> Projectile;
    public delegate* unmanaged[Cdecl]<Vec3, float, float, int, void> Explosion;
    public delegate* unmanaged[Cdecl]<int, int, void> Gib;
    public delegate* unmanaged[Cdecl]<Vec3, Vec3, int, void> Blood;
    public delegate* unmanaged[Cdecl]<byte*, Vec3, float, float, void> Sound;
    public delegate* unmanaged[Cdecl]<byte*, float, float, void> Sound2D;
    public delegate* unmanaged[Cdecl]<Vec3, Vec3, int, uint, float, float, float, void> Particles;
    public delegate* unmanaged[Cdecl]<Vec3, Vec3, float, float, void> Light;
    public delegate* unmanaged[Cdecl]<byte*, float, void> Message;
    public delegate* unmanaged[Cdecl]<byte*, int, void> FireTargets;
    public delegate* unmanaged[Cdecl]<byte*, byte*, int, void> Objective;
    public delegate* unmanaged[Cdecl]<byte*, void> ChangeMap;
    public delegate* unmanaged[Cdecl]<byte*, int, void> GiveWeapon;
    public delegate* unmanaged[Cdecl]<byte*, int, void> GiveAmmo;
}

/// <summary>Handle to an engine entity.</summary>
public readonly unsafe struct Entity
{
    public readonly int Id;
    public Entity(int id) { Id = id; }
    public bool Valid => Id != 0 && Engine.A->Exists(Id) != 0;

    public float Get(string prop) { using var p = new U8(prop); return Engine.A->GetFloat(Id, p.Ptr); }
    public void Set(string prop, float v) { using var p = new U8(prop); Engine.A->SetFloat(Id, p.Ptr, v); }
    public Vec3 GetVec(string prop) { using var p = new U8(prop); return Engine.A->GetVec(Id, p.Ptr); }
    public void SetVec(string prop, Vec3 v) { using var p = new U8(prop); Engine.A->SetVec(Id, p.Ptr, v); }
    public void SetStr(string prop, string v) { using var p = new U8(prop); using var s = new U8(v); Engine.A->SetString(Id, p.Ptr, s.Ptr); }

    public Vec3 Pos { get => GetVec("pos"); set => SetVec("pos", value); }
    public Vec3 Vel { get => GetVec("vel"); set => SetVec("vel", value); }
    public Vec3 Center => GetVec("center");
    public float Health { get => Get("health"); set => Set("health", value); }
    public float DistanceTo(Entity o) => (Pos - o.Pos).Length;
    public void Remove() => Engine.A->Remove(Id);
}

/// <summary>Base class for C# entity classes and components (like MonoBehaviour).</summary>
public abstract class Behaviour
{
    public Entity Self;
    public string Props = "{}";
    public virtual void Spawn() { }
    public virtual void Update(float dt) { }
    public virtual void Touch(Entity other, Vec3 normal) { }
    public virtual void Use(Entity activator) { }
    public virtual float Damaged(float amount, Entity attacker, Vec3 dir) => amount;
    public virtual void Pain(float amount, Entity attacker) { }
    public virtual void Die(Entity attacker, float amount) { }
    public virtual void Blocked(Entity other) { }

    /// <summary>Reads a number from the instance properties (flat JSON).</summary>
    public float PropF(string key, float def)
    {
        int p = Props.IndexOf("\"" + key + "\"", StringComparison.Ordinal);
        if (p < 0) return def;
        int c = Props.IndexOf(':', p);
        if (c < 0) return def;
        int s = c + 1, e = s;
        while (e < Props.Length && (char.IsWhiteSpace(Props[e]) || "+-.eE0123456789".IndexOf(Props[e]) >= 0)) e++;
        return float.TryParse(Props.AsSpan(s, e - s).Trim(), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var v) ? v : def;
    }
}

/// <summary>UTF-8 string marshalled to unmanaged memory for the duration of a call.</summary>
public unsafe readonly struct U8 : IDisposable
{
    public readonly byte* Ptr;
    public U8(string s) { Ptr = (byte*)Marshal.StringToCoTaskMemUTF8(s); }
    public void Dispose() => Marshal.FreeCoTaskMem((IntPtr)Ptr);
}

public static unsafe class Engine
{
    public static Api* A;
    static readonly List<Func<Behaviour>> Factories = new();

    public static float Time => A->Time();
    public static Entity Player => new(A->Player());
    public static float Random(float a, float b) => A->Random(a, b);
    public static void Log(string s) { using var p = new U8(s); A->Log(p.Ptr); }
    public static void Message(string s, float seconds = 3) { using var p = new U8(s); A->Message(p.Ptr, seconds); }
    public static void Sound(string path, Vec3 pos, float vol = 1, float pitch = 1) { using var p = new U8(path); A->Sound(p.Ptr, pos, vol, pitch); }
    public static void Light(Vec3 pos, Vec3 color, float radius, float seconds) => A->Light(pos, color, radius, seconds);
    public static void Particles(Vec3 pos, Vec3 dir, int count, uint rgba, float speed, float size, float life) =>
        A->Particles(pos, dir, count, rgba, speed, size, life);

    public static void RegisterClass(string name, string editorJson, Func<Behaviour> factory) => Register(name, editorJson, factory, false);
    public static void RegisterComponent(string name, string editorJson, Func<Behaviour> factory) => Register(name, editorJson, factory, true);

    static void Register(string name, string editorJson, Func<Behaviour> factory, bool component)
    {
        Factories.Add(factory);
        var d = new BehaviourDesc
        {
            User = (void*)(Factories.Count - 1),
            Create = &CbCreate, Destroy = &CbDestroy, Spawn = &CbSpawn, Update = &CbUpdate, Touch = &CbTouch,
            Use = &CbUse, Damaged = &CbDamaged, Pain = &CbPain, Die = &CbDie, Blocked = &CbBlocked,
            EditorJson = (byte*)Marshal.StringToCoTaskMemUTF8(editorJson), // lives as long as the plugin
        };
        using var n = new U8(name);
        if (component) A->RegisterComponent(n.Ptr, &d); else A->RegisterClass(n.Ptr, &d);
    }

    static Behaviour B(void* d) => (Behaviour)GCHandle.FromIntPtr((IntPtr)d).Target!;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void* CbCreate(void* user, int self, byte* props)
    {
        var b = Factories[(int)(nint)user]();
        b.Self = new Entity(self);
        b.Props = Marshal.PtrToStringUTF8((IntPtr)props) ?? "{}";
        return (void*)GCHandle.ToIntPtr(GCHandle.Alloc(b));
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbDestroy(void* d, int self) => GCHandle.FromIntPtr((IntPtr)d).Free();
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbSpawn(void* d, int self) => B(d).Spawn();
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbUpdate(void* d, int self, float dt) => B(d).Update(dt);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbTouch(void* d, int self, int other, Vec3 n) => B(d).Touch(new Entity(other), n);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbUse(void* d, int self, int a) => B(d).Use(new Entity(a));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static float CbDamaged(void* d, int self, float amt, int a, Vec3 dir) => B(d).Damaged(amt, new Entity(a), dir);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbPain(void* d, int self, float amt, int a) => B(d).Pain(amt, new Entity(a));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbDie(void* d, int self, int a, float amt) => B(d).Die(new Entity(a), amt);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void CbBlocked(void* d, int self, int o) => B(d).Blocked(new Entity(o));
}
