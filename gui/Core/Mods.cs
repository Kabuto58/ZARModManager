using System.IO;
using System.Text.RegularExpressions;

namespace ZARModManager.Core;

/// <summary>A mod (.lua): a Lua script that declares itself with za.mod{name, version, description, ...}.
/// Installed mods live in &lt;game&gt;\ZAMods; disabled ones are moved to ZAMods\disabled (the loader only reads
/// ZAMods\*.lua). A mod's saved settings (&lt;name&gt;.settings) stay in ZAMods either way.</summary>
public sealed record ModInfo(string FileName, string Name, string Version, string Description, bool HasManifest)
{
    public string Stem => Path.GetFileNameWithoutExtension(FileName);

    static readonly Regex Field = new("(name|version|description)\\s*=\\s*(['\"])(.*?)\\2", RegexOptions.Singleline);

    public static ModInfo Parse(string fileName, string source)
    {
        int at = source.IndexOf("za.mod", StringComparison.Ordinal);
        string name = Path.GetFileNameWithoutExtension(fileName), version = "", description = "";
        if (at >= 0)
        {
            string head = source.Substring(at, Math.Min(4000, source.Length - at));
            foreach (Match m in Field.Matches(head))
            {
                string v = m.Groups[3].Value;
                switch (m.Groups[1].Value)
                {
                    case "name" when name == Path.GetFileNameWithoutExtension(fileName): name = v; break;
                    case "version" when version == "": version = v; break;
                    case "description" when description == "": description = v; break;
                }
            }
        }
        return new ModInfo(fileName, name, version, description, at >= 0);
    }
}

public static class Mods
{
    public static string Folder(string gameDir) => Path.Combine(gameDir, "ZAMods");
    public static string DisabledFolder(string gameDir) => Path.Combine(Folder(gameDir), "disabled");

    public sealed record Installed(ModInfo Info, bool Enabled, bool HasSettings, string Path);

    public static List<Installed> Scan(string gameDir)
    {
        var list = new List<Installed>();
        string on = Folder(gameDir), off = DisabledFolder(gameDir);
        foreach (var (dir, enabled) in new[] { (on, true), (off, false) })
        {
            if (!Directory.Exists(dir)) continue;
            Migrate(dir);
            foreach (var file in Directory.GetFiles(dir, "*.lua"))
            {
                string text;
                try { text = File.ReadAllText(file); } catch { continue; }
                if (!IsMod(text)) continue;  // a .lua file without za.mod{...} is not a mod
                var info = ModInfo.Parse(Path.GetFileName(file), text);
                bool settings = File.Exists(Path.Combine(on, info.Stem + ".settings"));
                list.Add(new Installed(info, enabled, settings, file));
            }
        }
        return list.OrderBy(m => m.Info.Name, StringComparer.OrdinalIgnoreCase).ToList();
    }

    public static void SetEnabled(string gameDir, string fileName, bool enabled)
    {
        string on = Path.Combine(Folder(gameDir), fileName), off = Path.Combine(DisabledFolder(gameDir), fileName);
        if (enabled && File.Exists(off)) File.Move(off, on, overwrite: true);
        else if (!enabled && File.Exists(on))
        {
            Directory.CreateDirectory(DisabledFolder(gameDir));
            File.Move(on, off, overwrite: true);
        }
    }

    /// <summary>Copies a package into ZAMods (replacing one with the same file name). Returns its manifest.</summary>
    public static ModInfo Import(string gameDir, string sourcePath)
    {
        if (!IsModFile(sourcePath)) throw new InvalidOperationException("Only mods (.lua) can be installed.");
        var size = new FileInfo(sourcePath).Length;
        if (size > 4 * 1024 * 1024) throw new InvalidOperationException("That file is too large to be a mod.");
        string text = File.ReadAllText(sourcePath);
        if (!IsMod(text)) throw new InvalidOperationException("That .lua file is not a ZARModManager mod (it has no za.mod{...}).");
        var info = ModInfo.Parse(Path.GetFileName(sourcePath), text);
        Directory.CreateDirectory(Folder(gameDir));
        string name = Path.GetFileNameWithoutExtension(sourcePath) + ".lua";  // (a pre-0.8 .zamod is installed as .lua)
        string disabled = Path.Combine(DisabledFolder(gameDir), name);
        if (File.Exists(disabled)) File.Delete(disabled);  // a re-import is enabled again
        File.Copy(sourcePath, Path.Combine(Folder(gameDir), name), overwrite: true);
        return info;
    }

    /// <summary>A file the launcher accepts as a mod: .lua, or .zamod from before 0.8 (the same Lua).</summary>
    public static bool IsModFile(string path) =>
        path.EndsWith(".lua", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".zamod", StringComparison.OrdinalIgnoreCase);

    /// <summary>A mod declares itself with za.mod{...}; any other Lua file is not one.</summary>
    public static bool IsMod(string text) => text.Contains("za.mod", StringComparison.Ordinal);

    /// <summary>Renames pre-0.8 mods (&lt;name&gt;.zamod) to &lt;name&gt;.lua; one with a .lua of the same name is left.</summary>
    static void Migrate(string dir)
    {
        foreach (var old in Directory.GetFiles(dir, "*.zamod"))
        {
            string lua = Path.ChangeExtension(old, ".lua");
            try { if (!File.Exists(lua)) File.Move(old, lua); } catch { }
        }
    }

    public static void Remove(string gameDir, string fileName)
    {
        foreach (var p in new[] { Path.Combine(Folder(gameDir), fileName), Path.Combine(DisabledFolder(gameDir), fileName) })
            if (File.Exists(p)) File.Delete(p);
    }

    // ------------------------------------------------------------------------------------------ bundled mods --
    public sealed record Bundled(ModInfo Info, byte[] Bytes);

    static List<Bundled>? s_bundled;
    public static IReadOnlyList<Bundled> BundledMods => s_bundled ??= Runtime.ResourceNames("mods/")
        .Select(n => (n, b: Runtime.ReadResource(n)!))
        .Select(x => new Bundled(ModInfo.Parse(x.n["mods/".Length..], Runtime.Utf8(x.b)), x.b))
        .OrderBy(b => b.Info.Name, StringComparer.OrdinalIgnoreCase).ToList();

    public static void InstallBundled(string gameDir, Bundled mod)
    {
        Directory.CreateDirectory(Folder(gameDir));
        string disabled = Path.Combine(DisabledFolder(gameDir), mod.Info.FileName);
        string target = File.Exists(disabled) ? disabled : Path.Combine(Folder(gameDir), mod.Info.FileName);
        File.WriteAllBytes(target, mod.Bytes);
    }

    /// <summary>True when the installed copy differs from the one shipped with this ZARModManager.</summary>
    public static bool Differs(Installed installed, Bundled bundled)
    {
        try { return !File.ReadAllBytes(installed.Path).AsSpan().SequenceEqual(bundled.Bytes); }
        catch { return false; }
    }
}
