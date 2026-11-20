namespace Ecosim.Core;

/// <summary>One decoded pattern: its own box, its rule header, its live cells.</summary>
public sealed record RleDocument(int Width, int Height, string? Rule,
                                 IReadOnlyList<(int X, int Y)> Cells);

/// <summary>
/// Managed RLE reader/writer, kept independent of the native one so the tests
/// can prove the two agree. Same grammar as Golly's single-state RLE:
/// digits run-length the following 'b' (dead) or 'o' (alive), '$' ends a row,
/// '!' ends the pattern, '#' starts a comment line.
/// </summary>
public static class RleCodec
{
    private const int MaxRun = 1 << 24;

    public static RleDocument Decode(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            throw new FormatException("RLE 文本为空");
        }

        var i = SkipWhitespace(text, 0);
        while (i < text.Length && text[i] == '#') i = SkipLine(text, i);
        i = SkipWhitespace(text, i);

        string? rule = null;
        if (i < text.Length && (text[i] == 'x' || text[i] == 'X'))
        {
            var end = text.IndexOf('\n', i);
            if (end >= 0)
            {
                var header = text.Substring(i, end - i);
                var ruleIndex = header.IndexOf("rule", StringComparison.OrdinalIgnoreCase);
                if (ruleIndex >= 0)
                {
                    var eq = header.IndexOf('=', ruleIndex);
                    if (eq >= 0) rule = header[(eq + 1)..].Trim();
                }
                i = end + 1;
            }
        }

        var cells = new List<(int X, int Y)>();
        var x = 0;
        var y = 0;
        var maxX = 0;
        var maxY = 0;
        long count = 0;
        var hasCount = false;
        var done = false;

        void Track(int px, int py)
        {
            if (px + 1 > maxX) maxX = px + 1;
            if (py + 1 > maxY) maxY = py + 1;
        }

        for (; i < text.Length && !done; ++i)
        {
            var c = text[i];
            if (c is >= '0' and <= '9')
            {
                count = count * 10 + (c - '0');
                hasCount = true;
                if (count > MaxRun) throw new FormatException("RLE 游程长度超出合理范围");
                continue;
            }
            if (c == '#')
            {
                i = SkipLine(text, i) - 1;
                continue;
            }
            if (char.IsWhiteSpace(c)) continue;

            var n = hasCount ? (int)count : 1;
            count = 0;
            hasCount = false;

            if (c is 'b' or 'o')
            {
                if (c == 'o')
                {
                    for (var k = 0; k < n; ++k)
                    {
                        cells.Add((x, y));
                        Track(x, y);
                        ++x;
                    }
                }
                else
                {
                    for (var k = 0; k < n; ++k) Track(x++, y);
                }
                continue;
            }
            if (c == '$')
            {
                y += n;
                x = 0;
                continue;
            }
            if (c == '!')
            {
                done = true;
                continue;
            }

            throw new FormatException($"RLE 中出现无法识别的字符 '{c}'");
        }

        if (!done && (x > 0 || y > 0)) throw new FormatException("RLE 缺少结束符 '!'");

        return new RleDocument(Math.Max(maxX, 1), Math.Max(maxY, 1), rule, cells);
    }

    public static string Encode(int width, int height, IEnumerable<(int X, int Y)> cells,
                                string? rule = "B3/S23")
    {
        var grid = new HashSet<(int, int)>();
        var minX = int.MaxValue;
        var minY = int.MaxValue;
        var maxX = int.MinValue;
        var maxY = int.MinValue;
        foreach (var (x, y) in cells)
        {
            grid.Add((x, y));
            minX = Math.Min(minX, x);
            minY = Math.Min(minY, y);
            maxX = Math.Max(maxX, x);
            maxY = Math.Max(maxY, y);
        }
        if (grid.Count == 0)
        {
            minX = minY = maxX = maxY = 0;
        }

        var sb = new System.Text.StringBuilder();
        sb.Append("x = ").Append(maxX - minX + 1).Append(", y = ").Append(maxY - minY + 1);
        if (!string.IsNullOrWhiteSpace(rule)) sb.Append(", rule = ").Append(rule);
        sb.Append('\n');

        for (var y = minY; y <= maxY; ++y)
        {
            var run = 0;
            var letter = 'b';
            for (var x = minX; x <= maxX; ++x)
            {
                var current = grid.Contains((x, y)) ? 'o' : 'b';
                if (current != letter && run > 0)
                {
                    AppendRun(sb, run, letter);
                    run = 0;
                }
                letter = current;
                ++run;
            }
            AppendRun(sb, run, letter);
            sb.Append(y < maxY ? "$\n" : "!\n");
        }

        _ = width;
        _ = height;
        return sb.ToString();
    }

    private static void AppendRun(System.Text.StringBuilder sb, int count, char letter)
    {
        if (count <= 0) return;
        if (count > 1) sb.Append(count);
        sb.Append(letter);
    }

    private static int SkipLine(string text, int from)
    {
        var j = from;
        while (j < text.Length && text[j] != '\n') ++j;
        return j < text.Length ? j + 1 : j;
    }

    private static int SkipWhitespace(string text, int from)
    {
        var j = from;
        while (j < text.Length && char.IsWhiteSpace(text[j])) ++j;
        return j;
    }
}
