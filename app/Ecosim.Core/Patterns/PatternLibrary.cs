namespace Ecosim.Core;

public sealed record Pattern(string Id, string Name, string Rle);

/// <summary>
/// The pattern shelf. Two sources, one list: the repository's
/// data/patterns/*.rle when it can be found (walking up from the output
/// directory, like the native DLL probe), otherwise a small embedded set so
/// the app still works from an odd working directory. An external file with a
/// known id replaces the embedded one.
/// </summary>
public static class PatternLibrary
{
    private static readonly Pattern[] Embedded =
    {
        new("block", "方块（静物）", "x = 2, y = 2, rule = B3/S23\n2o$2o!\n"),
        new("beehive", "蜂巢（静物）", "x = 4, y = 3, rule = B3/S23\nb2o$o2bo$b2o!\n"),
        new("blinker", "闪光灯（周期 2）", "x = 3, y = 1, rule = B3/S23\n3o!\n"),
        new("glider", "滑翔机", "x = 3, y = 3, rule = B3/S23\nbob$2bo$3o!\n"),
        new("r_pentomino", "R-五联骨（玛土撒拉）", "x = 3, y = 3, rule = B3/S23\nb2o$2o$bo!\n"),
    };

    public static IReadOnlyList<Pattern> All { get; } = Load();

    public static Pattern? Find(string id)
        => All.FirstOrDefault(p => string.Equals(p.Id, id, StringComparison.OrdinalIgnoreCase));

    /// <summary>Where the repository's pattern files live, if visible from here.</summary>
    public static string? FindDataDirectory()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, "data", "patterns");
            if (Directory.Exists(candidate)) return candidate;
            dir = dir.Parent;
        }
        return null;
    }

    private static List<Pattern> Load()
    {
        var patterns = new List<Pattern>(Embedded);
        var dataDir = FindDataDirectory();
        if (dataDir is null) return patterns;

        foreach (var file in Directory.EnumerateFiles(dataDir, "*.rle"))
        {
            var id = Path.GetFileNameWithoutExtension(file);
            string text;
            try
            {
                text = File.ReadAllText(file);
            }
            catch (IOException)
            {
                continue;  // a locked or half-written file must not break the shelf
            }
            if (string.IsNullOrWhiteSpace(text)) continue;

            var index = patterns.FindIndex(p => p.Id == id);
            var pattern = new Pattern(id, id, text);
            if (index >= 0) patterns[index] = pattern;
            else patterns.Add(pattern);
        }
        return patterns;
    }
}
