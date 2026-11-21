using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Ecosim.App.Controls;

/// <summary>
/// Distribution of normalised energy across the living cells. Bars rather than
/// a curve: the interesting thing is where the population piles up, and that is
/// easier to read from heights than from a line.
/// </summary>
public sealed class EnergyHistogramChart : Control
{
    // Nullable on purpose, and checked in Render: a binding writes through the
    // property system, so it bypasses the CLR setter's null-coalescing and can
    // park a null here before the view model has produced its first histogram.
    // Declaring the type as non-nullable claimed a guarantee the property system
    // does not give, and the first frame paid for it with a NullReferenceException
    // inside the compositor - which takes the whole window down.
    public static readonly StyledProperty<int[]?> ValuesProperty =
        AvaloniaProperty.Register<EnergyHistogramChart, int[]?>(nameof(Values));

    static EnergyHistogramChart()
    {
        AffectsRender<EnergyHistogramChart>(ValuesProperty);
    }

    public int[]? Values
    {
        get => GetValue(ValuesProperty);
        set => SetValue(ValuesProperty, value);
    }

    public override void Render(DrawingContext context)
    {
        var bounds = Bounds;
        context.FillRectangle(new SolidColorBrush(Color.FromRgb(0x14, 0x18, 0x1D)),
                              new Rect(0, 0, bounds.Width, bounds.Height));

        var values = Values;
        if (values is null || values.Length == 0)
        {
            return;
        }

        int max = 1;
        foreach (var v in values) max = Math.Max(max, v);

        var brush = new SolidColorBrush(Color.FromRgb(0x6F, 0xD3, 0x6F));
        var baseline = new Pen(new SolidColorBrush(Color.FromRgb(0x2A, 0x2F, 0x36)), 1);
        const double pad = 6;
        var plot = new Rect(pad, pad, Math.Max(1, bounds.Width - pad * 2),
                            Math.Max(1, bounds.Height - pad * 2 - 12));
        var slot = plot.Width / values.Length;

        for (var i = 0; i < values.Length; ++i)
        {
            var height = plot.Height * values[i] / (double)max;
            if (height <= 0) continue;
            context.FillRectangle(brush, new Rect(plot.X + i * slot + 1, plot.Bottom - height,
                                                  Math.Max(1, slot - 2), height));
        }

        context.DrawLine(baseline, new Point(plot.X, plot.Bottom), new Point(plot.Right, plot.Bottom));

        var total = 0L;
        foreach (var v in values) total += v;
        var text = new FormattedText($"共 {total:N0} 个活细胞 ｜ 峰值 {max:N0}",
            System.Globalization.CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
            new Typeface("Consolas,Menlo,monospace"), 11,
            new SolidColorBrush(Color.FromRgb(0x9A, 0xA4, 0xB0)));
        context.DrawText(text, new Point(plot.X + 2, plot.Bottom + 1));
    }
}
