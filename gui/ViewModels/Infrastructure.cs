using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;

namespace ZARModManager.ViewModels;

public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;
    protected void Raise([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        Raise(name);
        return true;
    }
}

public sealed class RelayCommand(Action<object?> run, Func<object?, bool>? can = null) : ICommand
{
    public RelayCommand(Action run) : this(_ => run()) { }
    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }
    public bool CanExecute(object? p) => can?.Invoke(p) ?? true;
    public void Execute(object? p) => run(p);
}

/// <summary>A mod's enabled state as a status-dot tone.</summary>
public sealed class EnabledTone : IValueConverter
{
    public object Convert(object value, Type t, object parameter, CultureInfo c) => value is true ? Tone.Ok : Tone.Neutral;
    public object ConvertBack(object value, Type t, object parameter, CultureInfo c) => throw new NotSupportedException();
}

/// <summary>true -> Visible; "Invert" as the parameter flips it. Numbers count as true when non-zero.</summary>
public sealed class VisibleWhen : IValueConverter
{
    public object Convert(object value, Type t, object parameter, CultureInfo c)
    {
        bool on = value switch { bool b => b, int i => i != 0, string s => !string.IsNullOrEmpty(s), null => false, _ => true };
        if (parameter as string == "Invert") on = !on;
        return on ? Visibility.Visible : Visibility.Collapsed;
    }
    public object ConvertBack(object value, Type t, object parameter, CultureInfo c) => throw new NotSupportedException();
}
