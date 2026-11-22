using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;
using ZARModManager.Core;

namespace ZARModManager.ViewModels;

public enum Tone { Neutral, Ok, Warn, Danger }

public sealed class Toast(Tone tone, string title, string body)
{
    public Tone Tone { get; } = tone;
    public string Title { get; } = title;
    public string Body { get; } = body;
}

public sealed class LogLine(string text, Tone tone)
{
    public string Text { get; } = text;
    public Tone Tone { get; } = tone;
}

/// <summary>One installed mod, as listed on the Mods page.</summary>
public sealed class ModItem : ObservableObject
{
    readonly MainViewModel _owner;
    bool _enabled, _confirming;

    public ModItem(MainViewModel owner, Mods.Installed m, Mods.Bundled? bundled)
    {
        _owner = owner;
        FileName = m.Info.FileName;
        Name = m.Info.Name;
        Version = m.Info.Version;
        Description = m.Info.Description;
        HasSettings = m.HasSettings;
        HasManifest = m.Info.HasManifest;
        _enabled = m.Enabled;
        Bundled = bundled;
        UpdateAvailable = bundled is not null && Mods.Differs(m, bundled);
        RemoveCommand = new RelayCommand(Remove);
        CancelRemoveCommand = new RelayCommand(() => Confirming = false);
        UpdateCommand = new RelayCommand(() => owner.InstallBundled(bundled!));
    }

    public string FileName { get; }
    public string Name { get; }
    public string Version { get; }
    public string Description { get; }
    public bool HasSettings { get; }
    public bool HasManifest { get; }
    public Mods.Bundled? Bundled { get; }
    public bool UpdateAvailable { get; }
    public string Meta => FileName + (HasSettings ? "  ·  saved settings" : "") + (HasManifest ? "" : "  ·  no manifest");

    public bool Enabled
    {
        get => _enabled;
        set { if (Set(ref _enabled, value)) _owner.SetEnabled(this, value); }
    }
    public bool Confirming { get => _confirming; set => Set(ref _confirming, value); }

    public ICommand RemoveCommand { get; }
    public ICommand CancelRemoveCommand { get; }
    public ICommand UpdateCommand { get; }

    void Remove()
    {
        if (!Confirming) { Confirming = true; return; }  // the first click asks
        _owner.Remove(this);
    }
}

/// <summary>A mod shipped with ZARModManager that is not installed yet.</summary>
public sealed class BundledItem(MainViewModel owner, Mods.Bundled b)
{
    public string Name { get; } = b.Info.Name;
    public string Version { get; } = b.Info.Version;
    public string Description { get; } = b.Info.Description;
    public ICommand InstallCommand { get; } = new RelayCommand(() => owner.InstallBundled(b));
}

public sealed class MainViewModel : ObservableObject
{
    readonly DispatcherTimer _poll;
    string? _gameDir;
    bool _gameRunning, _loading = true, _logFollow = true, _busy;
    RuntimeState _runtime;
    string? _installedVersion;
    long _logLength = -1;

    public MainViewModel()
    {
        LaunchCommand = new RelayCommand(_ => Launch(), _ => !_busy);
        InstallRuntimeCommand = new RelayCommand(InstallRuntime);
        RemoveRuntimeCommand = new RelayCommand(RemoveRuntime);
        RefreshCommand = new RelayCommand(Refresh);
        ImportCommand = new RelayCommand(PickAndImport);
        OpenModsFolderCommand = new RelayCommand(() => { if (_gameDir is { } g) { Directory.CreateDirectory(Core.Mods.Folder(g)); Game.OpenFolder(Core.Mods.Folder(g)); } });
        OpenGameFolderCommand = new RelayCommand(() => { if (_gameDir is { } g) Game.OpenFolder(g); });
        BrowseGameCommand = new RelayCommand(BrowseGame);
        DetectGameCommand = new RelayCommand(() => { var s = AppSettings.Load(); s.GamePath = null; s.Save(); Refresh(); Notify(Tone.Neutral, "Game folder", GameFound ? "Detected through Steam." : "Not found through Steam."); });
        CopyLogCommand = new RelayCommand(() => { try { Clipboard.SetText(string.Join(Environment.NewLine, LogLines.Select(l => l.Text))); Notify(Tone.Ok, "Copied", "The log is on the clipboard."); } catch { } });
        DismissToastCommand = new RelayCommand(p => { if (p is Toast t) Toasts.Remove(t); });

        _poll = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.5) };
        _poll.Tick += (_, _) => Poll();
        _poll.Start();
        Refresh();
    }

    // ------------------------------------------------------------------------------------------ state --
    public ObservableCollection<ModItem> Mods { get; } = new();
    public ObservableCollection<BundledItem> Available { get; } = new();
    public ObservableCollection<LogLine> LogLines { get; } = new();
    public ObservableCollection<Toast> Toasts { get; } = new();

    public string? GameDir { get => _gameDir; private set { if (Set(ref _gameDir, value)) { Raise(nameof(GameFound)); Raise(nameof(GameDirText)); } } }
    public bool GameFound => _gameDir is not null;
    public string GameDirText => _gameDir ?? "Not found";
    public bool GameRunning { get => _gameRunning; private set { if (Set(ref _gameRunning, value)) RaiseStatus(); } }
    public bool IsLoading { get => _loading; private set => Set(ref _loading, value); }
    public bool LogFollow { get => _logFollow; set => Set(ref _logFollow, value); }
    public bool Busy { get => _busy; private set { Set(ref _busy, value); CommandManager.InvalidateRequerySuggested(); } }

    public RuntimeState RuntimeState { get => _runtime; private set { if (Set(ref _runtime, value)) RaiseStatus(); } }
    public string InstalledVersion => _installedVersion is { } v ? "v" + v : RuntimeState is RuntimeState.UpdateAvailable or RuntimeState.UpToDate ? "older build" : "—";
    public string BundledVersion => Runtime.BundledVersion is { } v ? "v" + v : "—";
    public string AppVersion => "v" + (typeof(MainViewModel).Assembly.GetName().Version?.ToString(3) ?? "1.0.0");

    public int EnabledCount => Mods.Count(m => m.Enabled);
    public int DisabledCount => Mods.Count(m => !m.Enabled);
    public bool HasMods => Mods.Count > 0;
    public bool HasAvailable => Available.Count > 0;
    public bool HasLog => LogLines.Count > 0;

    public string GameStatus => !GameFound ? "Not found" : GameRunning ? "Running" : "Ready";
    public Tone GameTone => !GameFound ? Tone.Danger : GameRunning ? Tone.Ok : Tone.Neutral;
    public string LoaderStatus => RuntimeState switch
    {
        RuntimeState.NoGame => "Waiting for the game",
        RuntimeState.NotInstalled => "Not installed",
        RuntimeState.UpToDate => "Installed" + (_installedVersion is { } iv ? " v" + iv : ""),
        RuntimeState.UpdateAvailable => "Update to " + BundledVersion,
        RuntimeState.Foreign => "Another version.dll",
        _ => ""
    };
    public Tone LoaderTone => RuntimeState switch
    {
        RuntimeState.UpToDate => Tone.Ok,
        RuntimeState.NotInstalled or RuntimeState.UpdateAvailable => Tone.Warn,
        _ => Tone.Danger
    };
    public string LaunchLabel => GameRunning ? "GAME RUNNING" : RuntimeState switch
    {
        RuntimeState.NotInstalled => "INSTALL & LAUNCH",
        RuntimeState.UpdateAvailable => "UPDATE & LAUNCH",
        _ => "LAUNCH GAME"
    };
    public bool CanInstallRuntime => GameFound && RuntimeState is RuntimeState.NotInstalled or RuntimeState.UpdateAvailable;
    public bool CanRemoveRuntime => GameFound && RuntimeState is RuntimeState.UpToDate or RuntimeState.UpdateAvailable;

    void RaiseStatus()
    {
        foreach (var n in new[] { nameof(GameStatus), nameof(GameTone), nameof(LoaderStatus), nameof(LoaderTone), nameof(LaunchLabel),
                                  nameof(CanInstallRuntime), nameof(CanRemoveRuntime), nameof(InstalledVersion) })
            Raise(n);
    }
    void RaiseCounts()
    {
        foreach (var n in new[] { nameof(EnabledCount), nameof(DisabledCount), nameof(HasMods), nameof(HasAvailable) }) Raise(n);
    }

    // ------------------------------------------------------------------------------------------ commands --
    public ICommand LaunchCommand { get; }
    public ICommand InstallRuntimeCommand { get; }
    public ICommand RemoveRuntimeCommand { get; }
    public ICommand RefreshCommand { get; }
    public ICommand ImportCommand { get; }
    public ICommand OpenModsFolderCommand { get; }
    public ICommand OpenGameFolderCommand { get; }
    public ICommand BrowseGameCommand { get; }
    public ICommand DetectGameCommand { get; }
    public ICommand CopyLogCommand { get; }
    public ICommand DismissToastCommand { get; }

    public void Notify(Tone tone, string title, string body)
    {
        var t = new Toast(tone, title, body);
        Toasts.Add(t);
        while (Toasts.Count > 4) Toasts.RemoveAt(0);
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(tone == Tone.Danger ? 7 : 4.5) };
        timer.Tick += (_, _) => { timer.Stop(); Toasts.Remove(t); };
        timer.Start();
    }

    public async void Refresh()
    {
        GameDir = Game.Locate();
        GameRunning = Game.IsRunning;
        RuntimeState = Runtime.State(_gameDir);
        _installedVersion = Runtime.InstalledVersion(_gameDir);
        RaiseStatus();
        string? dir = _gameDir;
        var scanned = dir is null || App.SnapshotNoMods ? new List<Core.Mods.Installed>() : await Task.Run(() => Core.Mods.Scan(dir));
        if (IsLoading) await Task.Delay(220);  // first load: let the skeleton show rather than flash
        Mods.Clear();
        var bundled = Core.Mods.BundledMods;
        foreach (var m in scanned)
            Mods.Add(new ModItem(this, m, bundled.FirstOrDefault(b => b.Info.FileName.Equals(m.Info.FileName, StringComparison.OrdinalIgnoreCase))));
        Available.Clear();
        foreach (var b in bundled.Where(b => !scanned.Any(m => m.Info.FileName.Equals(b.Info.FileName, StringComparison.OrdinalIgnoreCase))))
            Available.Add(new BundledItem(this, b));
        IsLoading = false;
        RaiseCounts();
        ReadLog(force: true);
    }

    void Poll()
    {
        bool running = Game.IsRunning;
        if (running != GameRunning) { GameRunning = running; RuntimeState = Runtime.State(_gameDir); }
        ReadLog(force: false);
    }

    void Launch()
    {
        if (_gameDir is not { } g) { Notify(Tone.Danger, "Game not found", "Set the game folder in Settings."); return; }
        if (GameRunning) { Notify(Tone.Neutral, "Already running", "Restart the game to load changed mods."); return; }
        try
        {
            Busy = true;
            if (RuntimeState is RuntimeState.NotInstalled or RuntimeState.UpdateAvailable) InstallRuntimeQuiet(g);
            if (RuntimeState == RuntimeState.Foreign) { Notify(Tone.Danger, "Not launched", "The game folder has a version.dll that is not ZARModManager's."); return; }
            Game.LaunchThroughSteam();
            Notify(Tone.Ok, "Launching", $"Starting {Game.Title} through Steam with {EnabledCount} mod{(EnabledCount == 1 ? "" : "s")}.");
        }
        catch (Exception e) { Notify(Tone.Danger, "Launch failed", e.Message); }
        finally { Busy = false; }
    }

    void InstallRuntimeQuiet(string g)
    {
        Runtime.Install(g);
        RuntimeState = Runtime.State(g);
        _installedVersion = Runtime.InstalledVersion(g);
        RaiseStatus();
    }

    void InstallRuntime()
    {
        if (_gameDir is not { } g) return;
        try { InstallRuntimeQuiet(g); Notify(Tone.Ok, "Loader installed", $"ZARModManager {InstalledVersion} is in the game folder."); }
        catch (Exception e) { Notify(Tone.Danger, "Install failed", e.Message); }
    }

    void RemoveRuntime()
    {
        if (_gameDir is not { } g) return;
        var r = MessageBox.Show("Remove the loader from the game folder?\n\nYour mods and their settings stay in ZAMods.", "ZARModManager",
                                MessageBoxButton.OKCancel, MessageBoxImage.Question);
        if (r != MessageBoxResult.OK) return;
        try { Runtime.Uninstall(g); Refresh(); Notify(Tone.Ok, "Loader removed", "The game now starts without mods."); }
        catch (Exception e) { Notify(Tone.Danger, "Remove failed", e.Message); }
    }

    void BrowseGame()
    {
        var dlg = new OpenFolderDialog { Title = $"Select the {Game.Title} folder (the one with {Game.ExeName})" };
        if (dlg.ShowDialog() != true) return;
        if (!Game.IsGameFolder(dlg.FolderName)) { Notify(Tone.Danger, "Not the game folder", $"{Game.ExeName} is not in that folder."); return; }
        var s = AppSettings.Load();
        s.GamePath = dlg.FolderName;
        s.Save();
        Refresh();
        Notify(Tone.Ok, "Game folder set", dlg.FolderName);
    }

    void PickAndImport()
    {
        var dlg = new OpenFileDialog { Filter = "ZARModManager mods (*.lua)|*.lua|Older ZARModManager mods (*.zamod)|*.zamod", Multiselect = true, Title = "Install mods" };
        if (dlg.ShowDialog() == true) Import(dlg.FileNames);
    }

    public void Import(IEnumerable<string> files)
    {
        if (_gameDir is not { } g) { Notify(Tone.Danger, "Game not found", "Set the game folder in Settings first."); return; }
        int n = 0;
        foreach (var f in files)
        {
            try { var info = Core.Mods.Import(g, f); n++; Notify(Tone.Ok, "Mod installed", $"{info.Name} {info.Version}".Trim()); }
            catch (Exception e) { Notify(Tone.Danger, Path.GetFileName(f), e.Message); }
        }
        if (n > 0) { Refresh(); if (GameRunning) Notify(Tone.Warn, "Restart the game", "New mods load the next time the game starts."); }
    }

    public void InstallBundled(Core.Mods.Bundled b)
    {
        if (_gameDir is not { } g) return;
        try { Core.Mods.InstallBundled(g, b); Refresh(); Notify(Tone.Ok, "Mod installed", $"{b.Info.Name} {b.Info.Version}".Trim()); }
        catch (Exception e) { Notify(Tone.Danger, "Install failed", e.Message); }
    }

    public void SetEnabled(ModItem m, bool enabled)
    {
        if (_gameDir is not { } g) return;
        try
        {
            Core.Mods.SetEnabled(g, m.FileName, enabled);
            RaiseCounts();
            Notify(Tone.Neutral, enabled ? "Enabled" : "Disabled", m.Name + (GameRunning ? " (from the next game start)" : ""));
        }
        catch (Exception e) { Notify(Tone.Danger, "Could not change " + m.Name, e.Message); Refresh(); }
    }

    public void Remove(ModItem m)
    {
        if (_gameDir is not { } g) return;
        try { Core.Mods.Remove(g, m.FileName); Mods.Remove(m); RaiseCounts(); Refresh(); Notify(Tone.Neutral, "Removed", m.Name); }
        catch (Exception e) { Notify(Tone.Danger, "Could not remove " + m.Name, e.Message); }
    }

    // ------------------------------------------------------------------------------------------ log --
    void ReadLog(bool force)
    {
        if (_gameDir is not { } g) return;
        string path = Path.Combine(Core.Mods.Folder(g), "loader.log");
        try
        {
            if (!File.Exists(path)) { if (LogLines.Count > 0) { LogLines.Clear(); Raise(nameof(HasLog)); } return; }
            long len = new FileInfo(path).Length;
            if (!force && (!LogFollow || len == _logLength)) return;
            _logLength = len;
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var sr = new StreamReader(fs);
            var lines = sr.ReadToEnd().Split('\n', StringSplitOptions.RemoveEmptyEntries).TakeLast(800).ToList();
            LogLines.Clear();
            foreach (var raw in lines)
            {
                string l = raw.TrimEnd('\r');
                if (l.Contains("] stats:")) continue;  // periodic memory stats: noise here
                Tone tone = l.Contains("FAULT") || l.Contains("FAILURE") || l.Contains("failed") || l.Contains("error:") ? Tone.Danger
                          : l.Contains("notify (warning)") || l.Contains("disabled") ? Tone.Warn
                          : l.Contains("loaded into") || l.Contains("mod loaded") || l.Contains("notify (ok)") ? Tone.Ok : Tone.Neutral;
                LogLines.Add(new LogLine(l, tone));
            }
            Raise(nameof(HasLog));
            LogChanged?.Invoke();
        }
        catch { /* busy: next poll */ }
    }
    public event Action? LogChanged;
}
