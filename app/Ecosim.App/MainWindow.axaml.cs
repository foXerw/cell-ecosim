using System.ComponentModel;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Platform.Storage;
using Ecosim.App.ViewModels;
using Ecosim.Core;

namespace Ecosim.App;

public partial class MainWindow : Window
{
    private MainViewModel? _vm;

    public MainWindow()
    {
        InitializeComponent();

        try
        {
            _vm = new MainViewModel();
            DataContext = _vm;
        }
        catch (EcosimException ex)
        {
            // The engine is missing, unloadable, or built against a different
            // ABI. Say so in the window instead of dying with a stack trace.
            ToolBar.IsEnabled = false;
            ErrorPanel.IsVisible = true;
            ErrorText.Text = ex.Message;
            return;
        }

        var vm = _vm;
        vm.StorageProviderFactory = () => Task.FromResult<IStorageProvider?>(StorageProvider);

        CanvasHost.SizeChanged += (_, e) =>
        {
            vm.Canvas.SetCanvasSize(e.NewSize);
            vm.Redraw();
        };
        vm.PropertyChanged += OnViewModelChanged;

        // Mutating the WriteableBitmap's pixels does not mark the Image visual
        // dirty in Avalonia 12's composition renderer; without this the canvas
        // shows a stale texture until something else invalidates the window.
        vm.FramePainted += (_, _) => CanvasImage.InvalidateVisual();
        AttachCanvas(vm);

        // The view model may already have painted during its own construction
        // (a startup heat layer does exactly that), so the bitmap has to be
        // handed over here rather than waiting for a change notification that
        // already fired before this view subscribed.
        CanvasImage.Source = vm.CanvasBitmap;
        vm.Redraw();
    }

    /// <summary>The canvas view model is replaced when the world is rebuilt, so
    /// the subscription has to move with it.</summary>
    private void AttachCanvas(MainViewModel vm)
    {
        vm.Canvas.PropertyChanged += OnCanvasChanged;
    }

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (_vm is null) return;
        if (e.PropertyName == nameof(MainViewModel.CanvasBitmap))
        {
            CanvasImage.Source = _vm.CanvasBitmap;
        }
        else if (e.PropertyName == nameof(MainViewModel.Canvas))
        {
            AttachCanvas(_vm);
            _vm.Canvas.SetCanvasSize(CanvasHost.Bounds.Size);
            _vm.Redraw();
        }
    }

    private void OnCanvasChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(CanvasViewModel.HoveredCell) or nameof(CanvasViewModel.Viewport))
        {
            UpdateHoverText();
        }
    }

    private void UpdateHoverText()
    {
        if (_vm is null) return;
        HoverText.Text = $"{_vm.Canvas.HoverText} ｜ 缩放 {_vm.Canvas.ZoomPercent}% ｜ " +
                         "拖拽绘制 · 中键或右键拖拽平移 · 滚轮缩放";
    }

    private void OnCanvasPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (_vm is null) return;
        var point = e.GetPosition(CanvasHost);
        var props = e.GetCurrentPoint(CanvasHost).Properties;
        var pan = props.IsMiddleButtonPressed || props.IsRightButtonPressed;
        Guard(() => Apply(_vm.Canvas.PointerPressed(point, pan)));
        e.Pointer.Capture(CanvasHost);
        e.Handled = true;
    }

    private void OnCanvasPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_vm is null) return;
        var point = e.GetPosition(CanvasHost);
        var props = e.GetCurrentPoint(CanvasHost).Properties;
        var pressed = props.IsLeftButtonPressed || props.IsMiddleButtonPressed ||
                      props.IsRightButtonPressed;
        Guard(() =>
        {
            Apply(_vm.Canvas.PointerMoved(point, pressed));
            UpdateHoverText();
        });
    }

    private void OnCanvasPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        _vm?.Canvas.PointerReleased();
    }

    private void OnCanvasWheel(object? sender, PointerWheelEventArgs e)
    {
        if (_vm is null) return;
        _vm.ZoomAt(e.GetPosition(CanvasHost), e.Delta.Y > 0 ? 1.25 : 1 / 1.25);
        UpdateHoverText();
        e.Handled = true;
    }

    /// <summary>
    /// A pointer gesture must never take the process down. Engine errors become
    /// a status-line notice instead - fast strokes routinely run off the edge of
    /// the grid, and that is a normal thing for a person to do.
    /// </summary>
    private void Guard(Action action)
    {
        try
        {
            action();
        }
        catch (EcosimException ex)
        {
            if (_vm is not null) _vm.Notice = $"操作未生效：{ex.Message}";
        }
    }

    private void Apply(System.Collections.Generic.IReadOnlyList<PaintOp>? ops)
    {
        if (_vm is null || ops is null || ops.Count == 0) return;
        _vm.World.Paint(ops);
        _vm.Redraw();
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (_vm is null || e.Handled) return;
        // Typing in a box must never trigger a shortcut.
        if (e.Source is TextBox or NumericUpDown) return;

        var ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
        switch (e.Key)
        {
            case Key.Space when !ctrl:
                Execute(_vm.ToggleRunCommand);
                break;
            case Key.S when !ctrl:
                Execute(_vm.StepCommand);
                break;
            case Key.S when ctrl:
                Execute(_vm.SaveRleCommand);
                break;
            case Key.O when ctrl:
                Execute(_vm.OpenRleCommand);
                break;
            case Key.G when ctrl:
                Execute(_vm.ToggleTopologyCommand);
                break;
            case Key.R:
                Execute(_vm.RandomizeCommand);
                break;
            case Key.C:
                Execute(_vm.ClearCommand);
                break;
            case Key.F:
                Execute(_vm.FitViewCommand);
                break;
            case Key.Add or Key.OemPlus:
                _vm.Speed = _vm.Speed + 5;
                break;
            case Key.Subtract or Key.OemMinus:
                _vm.Speed = Math.Max(1, _vm.Speed - 5);
                break;
            case Key.D1:
                _vm.Canvas.Species = 1;
                break;
            case Key.D2:
                _vm.Canvas.Species = 2;
                break;
            case Key.D3:
                _vm.Canvas.Species = 3;
                break;
            default:
                return;
        }
        e.Handled = true;
    }

    private static void Execute(ICommand command)
    {
        if (command.CanExecute(null)) command.Execute(null);
    }

    private void OnPresetClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (_vm is null || sender is not Button { Tag: string tag }) return;
        if (!int.TryParse(tag, out var size)) return;
        _vm.NewWidth = size;
        _vm.NewHeight = size;
        if (_vm.NewWorldCommand.CanExecute(null)) _vm.NewWorldCommand.Execute(null);
    }
}
