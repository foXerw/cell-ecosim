using System.ComponentModel;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
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
            SyncTransform();
            vm.Redraw();
        };
        vm.PropertyChanged += OnViewModelChanged;
        AttachCanvas(vm);

        CanvasImage.Source = vm.CanvasBitmap;
        SyncTransform();
        vm.Redraw();
    }

    /// <summary>The canvas view model is replaced when the world is rebuilt, so
    /// the transform subscription has to move with it.</summary>
    private void AttachCanvas(MainViewModel vm)
    {
        vm.Canvas.PropertyChanged += OnCanvasChanged;
    }

    private void OnPresetClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (_vm is null || sender is not Button { Tag: string tag }) return;
        if (!int.TryParse(tag, out var size)) return;
        _vm.NewWidth = size;
        _vm.NewHeight = size;
        if (_vm.NewWorldCommand.CanExecute(null)) _vm.NewWorldCommand.Execute(null);
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
            SyncTransform();
            _vm.Redraw();
        }
    }

    private void OnCanvasChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(CanvasViewModel.Viewport)) SyncTransform();
        if (e.PropertyName == nameof(CanvasViewModel.HoveredCell) ||
            e.PropertyName == nameof(CanvasViewModel.Viewport))
        {
            UpdateHoverText();
        }
    }

    private void SyncTransform()
    {
        if (_vm is null) return;
        var vp = _vm.Canvas.Viewport;
        CanvasImage.RenderTransform =
            new MatrixTransform(new Matrix(vp.CellSize, 0, 0, vp.CellSize, vp.OffsetX, vp.OffsetY));
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
        Apply(_vm.Canvas.PointerPressed(point, pan));
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
        Apply(_vm.Canvas.PointerMoved(point, pressed));
        UpdateHoverText();
    }

    private void OnCanvasPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        _vm?.Canvas.PointerReleased();
    }

    private void OnCanvasWheel(object? sender, PointerWheelEventArgs e)
    {
        if (_vm is null) return;
        _vm.ZoomAt(e.GetPosition(CanvasHost), e.Delta.Y > 0 ? 1.25 : 1 / 1.25);
        SyncTransform();
        UpdateHoverText();
        e.Handled = true;
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

    private void Apply(System.Collections.Generic.IReadOnlyList<PaintOp>? ops)
    {
        if (_vm is null || ops is null || ops.Count == 0) return;
        _vm.World.Paint(ops);
        _vm.Redraw();
    }
}
