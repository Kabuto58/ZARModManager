using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using ZARModManager.ViewModels;

namespace ZARModManager;

public partial class MainWindow : Window
{
    readonly MainViewModel _vm = new();
    readonly Dictionary<string, FrameworkElement> _pages;
    FrameworkElement? _current;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        _pages = new() { ["Home"] = PageHome, ["Mods"] = PageMods, ["Logs"] = PageLogs, ["Settings"] = PageSettings };
        _current = PageHome;
        _vm.LogChanged += () => { if (_vm.LogFollow && LogList.Items.Count > 0) LogList.ScrollIntoView(LogList.Items[^1]); };
        StateChanged += (_, _) => Root.Margin = WindowState == WindowState.Maximized ? new Thickness(7) : new Thickness(0);  // a maximised chrome-less window overhangs the screen
        DragEnter += OnDrag; DragOver += OnDrag;
        DragLeave += (_, _) => DropOverlay.Visibility = Visibility.Collapsed;
        Drop += OnDrop;
    }

    // ------------------------------------------------------------------------------------------ pages --
    void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (sender is RadioButton { Tag: string page } && _pages is not null) ShowPage(page, animate: true);
    }

    /// <summary>Switches page: the new one fades in while rising 14 px (transform and opacity only).</summary>
    public void ShowPage(string page, bool animate = true)
    {
        if (!_pages.TryGetValue(page, out var next)) return;
        PageTitle.Text = page.ToUpperInvariant();
        if (next == _current && next.Visibility == Visibility.Visible) return;
        if (_current is not null) _current.Visibility = Visibility.Collapsed;
        next.Visibility = Visibility.Visible;
        _current = next;
        foreach (var nav in new[] { NavHome, NavMods, NavLogs, NavSettings })
            if ((string)nav.Tag == page && nav.IsChecked != true) nav.IsChecked = true;
        if (!animate) { next.Opacity = 1; return; }
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        next.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220)));
        Movable(next).BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(14, 0, TimeSpan.FromMilliseconds(320)) { EasingFunction = ease });
    }

    void GoMods_Click(object sender, RoutedEventArgs e) => ShowPage("Mods");
    void GoSettings_Click(object sender, RoutedEventArgs e) => ShowPage("Settings");

    /// <summary>Library rows arrive in a short cascade (40 ms apart).</summary>
    void Row_Loaded(object sender, RoutedEventArgs e)
    {
        if (sender is not Border row) return;
        if (App.Snapshotting) { row.Opacity = 1; return; }
        var container = ItemsControl.ContainerFromElement(ModList, row) as FrameworkElement;
        int index = container is null ? 0 : Math.Max(0, ModList.ItemContainerGenerator.IndexFromContainer(container));
        var delay = TimeSpan.FromMilliseconds(Math.Min(index, 12) * 40);
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        row.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(260)) { BeginTime = delay });
        Movable(row).BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(10, 0, TimeSpan.FromMilliseconds(340)) { BeginTime = delay, EasingFunction = ease });
    }

    /// <summary>The element's own, animatable TranslateTransform. Transforms declared in templates are frozen (shared by
    /// every item), so each element gets a fresh one before it is animated.</summary>
    static TranslateTransform Movable(UIElement e)
    {
        if (e.RenderTransform is TranslateTransform { IsFrozen: false } t) return t;
        var fresh = new TranslateTransform();
        e.RenderTransform = fresh;
        return fresh;
    }

    // ------------------------------------------------------------------------------------------ drag & drop --
    static string[] DroppedMods(DragEventArgs e) =>
        e.Data.GetData(DataFormats.FileDrop) is string[] files ? files.Where(Core.Mods.IsModFile).ToArray() : [];

    void OnDrag(object sender, DragEventArgs e)
    {
        bool ok = DroppedMods(e).Length > 0;
        e.Effects = ok ? DragDropEffects.Copy : DragDropEffects.None;
        DropOverlay.Visibility = ok ? Visibility.Visible : Visibility.Collapsed;
        e.Handled = true;
    }

    void OnDrop(object sender, DragEventArgs e)
    {
        DropOverlay.Visibility = Visibility.Collapsed;
        var mods = DroppedMods(e);
        if (mods.Length == 0) { _vm.Notify(Tone.Warn, "Nothing to install", "Only mods (.lua files) can be dropped here."); return; }
        _vm.Import(mods);
        ShowPage("Mods");
    }

    // ------------------------------------------------------------------------------------------ caption --
    void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    void Maximize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    void Close_Click(object sender, RoutedEventArgs e) => Close();
}
