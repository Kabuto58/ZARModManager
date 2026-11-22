using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace ZARModManager.Core;

/// <summary>Where the game is and whether it is running. The folder comes from Steam (its library folders and the
/// game's app manifest), unless the user picked one in Settings.</summary>
public static class Game
{
    public const string AppId = "2747660";
    public const string ExeName = "zombieassault.exe";
    public const string Title = "Zombie Assault: Resurgence";

    public static bool IsRunning => Process.GetProcessesByName("zombieassault").Length > 0;

    public static string? Locate()
    {
        var custom = AppSettings.Load().GamePath;
        if (IsGameFolder(custom)) return custom;
        return Detect();
    }

    public static bool IsGameFolder(string? dir) => !string.IsNullOrWhiteSpace(dir) && File.Exists(Path.Combine(dir, ExeName));

    public static string? Detect()
    {
        foreach (var lib in SteamLibraries())
        {
            string acf = Path.Combine(lib, "steamapps", $"appmanifest_{AppId}.acf");
            string dir = Path.Combine(lib, "steamapps", "common", "Zombie Assault");
            if (File.Exists(acf))
            {
                var m = Regex.Match(File.ReadAllText(acf), "\"installdir\"\\s+\"([^\"]+)\"");
                if (m.Success) dir = Path.Combine(lib, "steamapps", "common", m.Groups[1].Value);
            }
            if (IsGameFolder(dir)) return RealCase(dir);
        }
        return null;
    }

    static IEnumerable<string> SteamLibraries()
    {
        var libs = new List<string>();
        try
        {
            if (Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) is string steam)
            {
                steam = steam.Replace('/', '\\');
                libs.Add(steam);
                string vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
                if (File.Exists(vdf))
                    foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
                        libs.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
            }
        }
        catch { /* no Steam in the registry: the default folder below */ }
        libs.Add(@"C:\Program Files (x86)\Steam");
        return libs.Distinct(StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>The path with each folder's real capitalisation (Steam reports its own in lower case).</summary>
    static string RealCase(string path)
    {
        try
        {
            var full = Path.GetFullPath(path);
            string root = Path.GetPathRoot(full)!.ToUpperInvariant();
            string current = root;
            foreach (var part in full[root.Length..].Split(Path.DirectorySeparatorChar, StringSplitOptions.RemoveEmptyEntries))
            {
                var match = Directory.EnumerateFileSystemEntries(current, part).FirstOrDefault();
                current = match ?? Path.Combine(current, part);
            }
            return current;
        }
        catch { return path; }
    }

    public static void LaunchThroughSteam() =>
        Process.Start(new ProcessStartInfo($"steam://rungameid/{AppId}") { UseShellExecute = true });

    public static void OpenFolder(string path) =>
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{path}\"") { UseShellExecute = true });
}

/// <summary>The front end's own settings (%AppData%\ZARModManager\settings.json).</summary>
public sealed class AppSettings
{
    public string? GamePath { get; set; }

    static string FilePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZARModManager", "settings.json");

    static string OldFilePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZALoader", "settings.json");

    public static AppSettings Load()
    {
        string path = File.Exists(FilePath) || !File.Exists(OldFilePath) ? FilePath : OldFilePath;  // (before 1.2: ZALoader)
        try { return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path)) ?? new(); }
        catch { return new(); }
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}
