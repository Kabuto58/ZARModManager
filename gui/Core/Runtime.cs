using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace ZARModManager.Core;

public enum RuntimeState { NoGame, NotInstalled, UpToDate, UpdateAvailable, Foreign }

/// <summary>The loader runtime: shipped inside this exe, installed into the game folder as version.dll (which the game
/// loads by itself). A version.dll that is not ZARModManager's is never replaced.</summary>
public static class Runtime
{
    const string Resource = "runtime/ZARModManagerRuntime.dll";

    static byte[]? s_bundled;
    public static byte[]? Bundled => s_bundled ??= ReadResource(Resource);

    public static string? BundledVersion => Bundled is { } b ? VersionOf(b) : null;

    public static string TargetPath(string gameDir) => Path.Combine(gameDir, "version.dll");

    public static RuntimeState State(string? gameDir)
    {
        if (gameDir is null) return RuntimeState.NoGame;
        string target = TargetPath(gameDir);
        if (!File.Exists(target)) return RuntimeState.NotInstalled;
        byte[] installed;
        try { installed = File.ReadAllBytes(target); } catch { return RuntimeState.UpToDate; }
        if (!IsOurs(installed)) return RuntimeState.Foreign;
        if (Bundled is null) return RuntimeState.UpToDate;
        return SHA256.HashData(installed).AsSpan().SequenceEqual(SHA256.HashData(Bundled)) ? RuntimeState.UpToDate : RuntimeState.UpdateAvailable;
    }

    public static string? InstalledVersion(string? gameDir)
    {
        if (gameDir is null) return null;
        string target = TargetPath(gameDir);
        try { return File.Exists(target) ? VersionOf(File.ReadAllBytes(target)) : null; } catch { return null; }
    }

    public static void Install(string gameDir)
    {
        if (Bundled is null) throw new InvalidOperationException("This build has no loader runtime inside it.");
        if (Game.IsRunning) throw new InvalidOperationException("Close the game first: Windows keeps the loader file locked while it runs.");
        if (State(gameDir) == RuntimeState.Foreign) throw new InvalidOperationException("The game folder has a version.dll that is not ZARModManager's: it is left alone.");
        File.WriteAllBytes(TargetPath(gameDir), Bundled);
        Directory.CreateDirectory(Path.Combine(gameDir, "ZAMods"));
    }

    public static void Uninstall(string gameDir)
    {
        if (Game.IsRunning) throw new InvalidOperationException("Close the game first.");
        string target = TargetPath(gameDir);
        if (!File.Exists(target)) return;
        if (!IsOurs(File.ReadAllBytes(target))) throw new InvalidOperationException("That version.dll is not ZARModManager's: it is left alone.");
        File.Delete(target);
    }

    static bool IsOurs(byte[] dll) => IndexOf(dll, "ZARModManager "u8) >= 0 || IndexOf(dll, "ZALoader "u8) >= 0;  // (runtimes before 1.2: ZALoader)

    /// <summary>The runtime's version: its file version resource, else the "ZARModManager x.y.z loaded" string it logs.</summary>
    static string? VersionOf(byte[] dll)
    {
        string tmp = Path.Combine(Path.GetTempPath(), $"zarmm-ver-{Guid.NewGuid():N}.dll");
        try
        {
            File.WriteAllBytes(tmp, dll);
            var v = FileVersionInfo.GetVersionInfo(tmp).ProductVersion;
            if (!string.IsNullOrWhiteSpace(v)) return v;
        }
        catch { }
        finally { try { File.Delete(tmp); } catch { } }
        return null;
    }

    static int IndexOf(byte[] haystack, ReadOnlySpan<byte> needle) => haystack.AsSpan().IndexOf(needle);

    internal static byte[]? ReadResource(string name)
    {
        using var s = typeof(Runtime).Assembly.GetManifestResourceStream(name);
        if (s is null) return null;
        using var ms = new MemoryStream();
        s.CopyTo(ms);
        return ms.ToArray();
    }

    internal static IEnumerable<string> ResourceNames(string prefix) =>
        typeof(Runtime).Assembly.GetManifestResourceNames().Where(n => n.StartsWith(prefix, StringComparison.Ordinal));

    internal static string Utf8(byte[] b) => Encoding.UTF8.GetString(b);
}
