using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
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
        CanvasHost.SizeChanged += (_, e) =>
        {
            vm.Canvas.SetCanvasSize(e.NewSize);
            SyncTransform();
            vm.Redraw();
        };
        vm.PropertyChanged += OnViewModelChanged;
        vm.Canvas.PropertyChanged += OnCanvasChanged;

        CanvasImage.Source = vm.CanvasBitmap;
        SyncTransform();
        vm.Redraw();
    }

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainViewModel.CanvasBitmap))
        {
            CanvasImage.Source = _vm?.CanvasBitmap;
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

    private void Apply(System.Collections.Generic.IReadOnlyList<PaintOp>? ops)
    {
        if (_vm is null || ops is null || ops.Count == 0) return;
        _vm.World.Paint(ops);
        _vm.Redraw();
    }
}
