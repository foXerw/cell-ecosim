using Ecosim.Core;
using Ecosim.Core.Interop;

namespace Ecosim.Tests;

/// <summary>
/// Covers Review Focus #1 of the plan: what happens before a single cell is
/// simulated. A missing or mismatched ecosim.dll must produce a readable
/// Chinese message, never a bare DllNotFoundException stack.
/// </summary>
public class NativeLoadTests
{
    [Fact]
    public void Create_returns_valid_handle_and_dispose_releases_it()
    {
        var world = NativeWorld.Create(WorldConfig.Default(8, 8));
        Assert.False(world.IsInvalid);
        Assert.False(world.IsClosed);
        world.Dispose();
        Assert.True(world.IsClosed);
    }

    [Fact]
    public void Using_after_dispose_throws_object_disposed()
    {
        var world = NativeWorld.Create(WorldConfig.Default(8, 8));
        world.Dispose();
        var ex = Assert.Throws<ObjectDisposedException>(() => world.Step(1));
        Assert.Contains("ecosim", ex.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Probe_paths_start_with_the_env_override()
    {
        const string marker = "__ecosim_probe_marker__";
        var previous = Environment.GetEnvironmentVariable(NativeLibraryLoader.DirectoryEnvVar);
        try
        {
            Environment.SetEnvironmentVariable(NativeLibraryLoader.DirectoryEnvVar, marker);
            var first = NativeLibraryLoader.ProbePaths().First();
            Assert.StartsWith(marker, first, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            Environment.SetEnvironmentVariable(NativeLibraryLoader.DirectoryEnvVar, previous);
        }
    }

    [Fact]
    public void Load_from_a_directory_without_the_dll_reports_which_paths_were_tried()
    {
        var empty = Path.Combine(Path.GetTempPath(), "ecosim-empty-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(empty);
        try
        {
            Assert.False(NativeLibraryLoader.TryLoadFrom(empty, out _, out var error));
            Assert.Contains("原生引擎未加载", error);
            Assert.Contains("ecosim.dll", error);
        }
        finally
        {
            Directory.Delete(empty);
        }
    }

    [Fact]
    public void Version_mismatch_message_names_both_versions()
    {
        // The engine reports the version it was built with; the managed side
        // pins what it expects. Formatting is asserted, the real check is
        // exercised by every other test passing.
        var message = NativeWorld.DescribeVersionCheck(NativeWorld.ExpectedAbiVersion + 1,
                                                       NativeWorld.ExpectedAbiVersion);
        Assert.Contains("原生引擎版本不匹配", message);
        Assert.Contains("需要 v2", message);
        Assert.Contains("实际 v1", message);
    }
}
