using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Ecosim.Core;

namespace Ecosim.App.Controls;

/// <summary>
/// Population-over-time lines, one per species. Drawn straight with
/// DrawingContext rather than pulled in as a charting dependency: the shape is
/// three polylines and an axis, and owning it keeps the dependency list honest.
/// </summary>
public sealed class PopulationChart : Control
{
    public static readonly StyledProperty<IReadOnlyList<HistorySample>?> SamplesProperty =
        AvaloniaProperty.Register<PopulationChart, IReadOnlyList<HistorySample>?>(nameof(Samples));

    private static readonly (int Species, IBrush Brush, string Name)[] Series =
    {
        (1, new SolidColorBrush(Color.FromRgb(111, 211, 111)), "藻"),
        (2, new SolidColorBrush(Color.FromRgb(232, 200, 64)), "虫"),
        (3, new SolidColorBrush(Color.FromRgb(240, 96, 88)), "兽"),
    };

    static PopulationChart()
    {
        AffectsRender<PopulationChart>(SamplesProperty);
    }

    public IReadOnlyList<HistorySample>? Samples
    {
        get => GetValue(SamplesProperty);
        set => SetValue(SamplesProperty, value);
    }

    public override void Render(DrawingContext context)
    {
        var bounds = Bounds;
        var samples = Samples;

        var background = new SolidColorBrush(Color.FromRgb(0x14, 0x18, 0x1D));
        context.FillRectangle(background, new Rect(0, 0, bounds.Width, bounds.Height));

        var line = new Pen(new SolidColorBrush(Color.FromRgb(0x2A, 0x2F, 0x36)), 1);
        const double padding = 6;
        var plot = new Rect(padding, padding, Math.Max(1, bounds.Width - padding * 2),
                            Math.Max(1, bounds.Height - padding * 2));
        context.DrawLine(line, new Point(plot.X, plot.Bottom), new Point(plot.Right, plot.Bottom));

        if (samples is null || samples.Count < 2)
        {
            WriteNote(context, plot, "跑起来之后显示种群曲线");
            return;
        }

        double max = 1;
        foreach (var series in Series)
        {
            foreach (var sample in samples)
            {
                max = Math.Max(max, sample.CountOf(series.Species));
            }
        }

        foreach (var (species, brush, name) in Series)
        {
            var geometry = new StreamGeometry();
            using (var gctx = geometry.Open())
            {
                var started = false;
                for (var i = 0; i < samples.Count; ++i)
                {
                    var x = plot.X + plot.Width * i / (samples.Count - 1);
                    var y = plot.Bottom - plot.Height * samples[i].CountOf(species) / max;
                    var point = new Point(x, y);
                    if (started) gctx.LineTo(point);
                    else gctx.BeginFigure(point);
                    started = true;
                }
            }

            context.DrawGeometry(null, new Pen(brush, 1.4), geometry);
        }

        WriteNote(context, plot, $"{samples[0].Generation:N0}–{samples[^1].Generation:N0} 代 ｜ 峰值 {max:N0}");
    }

    private static void WriteNote(DrawingContext context, Rect plot, string text)
    {
        var formatted = new FormattedText(text,
            System.Globalization.CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            new Typeface("Consolas,Menlo,monospace"),
            11,
            new SolidColorBrush(Color.FromRgb(0x9A, 0xA4, 0xB0)));
        context.DrawText(formatted, new Point(plot.X + 2, plot.Bottom - 16));
    }
}
