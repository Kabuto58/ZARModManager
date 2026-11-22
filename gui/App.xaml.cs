using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace ZARModManager;

public partial class App : Application
{
    /// <summary>True while rendering --snapshot images (an off-screen window does not run animations).</summary>
    public static bool Snapshotting { get; private set; }

    /// <summary>--snapshot ... --no-mods: render as if no mods were installed (for screenshots; the mods folder is untouched).</summary>
    public static bool SnapshotNoMods { get; private set; }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        DispatcherUnhandledException += (_, a) =>
        {
            MessageBox.Show(a.Exception.Message, "ZARModManager", MessageBoxButton.OK, MessageBoxImage.Error);
            a.Handled = true;
        };
        // (before the window: its view model reads the mods folder as soon as it is made)
        SnapshotNoMods = e.Args.Contains("--snapshot") && e.Args.Contains("--no-mods");
        var window = new MainWindow();
        MainWindow = window;
        // ZARModManager.exe --snapshot <folder> [<width> <height>] [--no-mods]: render every page to PNG and exit (for
        // checking the design; with a size, the window is laid out at 3/4 of it and rendered at 4/3 scale, so 1920 1080
        // gives 1080p images of a 1440 x 810 window, as it looks on a 1080p screen at 133% scaling)
        var args = e.Args.Where(a => a != "--no-mods").ToArray();
        if ((args.Length == 2 || args.Length == 4) && args[0] == "--snapshot")
        {
            Snapshotting = true;
            double scale = 1;
            if (args.Length == 4 && int.TryParse(args[2], out int pw) && int.TryParse(args[3], out int ph))
            {
                scale = 4.0 / 3.0;
                window.MinWidth = window.MinHeight = 0;
                window.Width = pw / scale;
                window.Height = ph / scale;
            }
            window.Left = -20000;
            window.ShowActivated = false;
            window.Show();
            Snapshot(window, args[1], scale);
            return;
        }
        window.Show();
    }

    static async void Snapshot(MainWindow w, string dir, double scale)
    {
        Directory.CreateDirectory(dir);
        await Task.Delay(900);
        foreach (var page in new[] { "Home", "Mods", "Logs", "Settings" })
        {
            w.ShowPage(page, animate: false);
            await Task.Delay(700);
            w.UpdateLayout();
            var root = (FrameworkElement)w.Content;
            var bmp = new RenderTargetBitmap((int)Math.Round(root.ActualWidth * scale), (int)Math.Round(root.ActualHeight * scale),
                                             96 * scale, 96 * scale, PixelFormats.Pbgra32);
            bmp.Render(root);
            var png = new PngBitmapEncoder();
            png.Frames.Add(BitmapFrame.Create(bmp));
            using var f = File.Create(Path.Combine(dir, page + ".png"));
            png.Save(f);
        }
        _ = Current.Dispatcher.BeginInvoke(DispatcherPriority.Background, () => Current.Shutdown());
    }
}
