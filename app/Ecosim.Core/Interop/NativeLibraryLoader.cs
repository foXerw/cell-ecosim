using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Ecosim.Core.Interop;

/// <summary>
/// Decides which ecosim.dll the process binds to. Resolution order:
/// ECOSIM_NATIVE_DIR, then the application directory, then the repository's
/// build/native found by walking up from the output directory (so `dotnet test`
/// works straight after a native build without any copying step).
/// </summary>
internal static class NativeLibraryLoader
{
    public const string LibraryName = "ecosim";
    public const string FileName = "ecosim.dll";
    public const string DirectoryEnvVar = "ECOSIM_NATIVE_DIR";

    // CA2255 discourages module initialisers in libraries, but this is the one
    // case that needs the earlier hook: the resolver must be registered before
    // the first DllImport call anywhere in the process.
#pragma warning disable CA2255
    [ModuleInitializer]
    internal static void Install()
        => NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
#pragma warning restore CA2255

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != LibraryName) return IntPtr.Zero;
        foreach (var path in ProbePaths())
        {
            if (NativeLibrary.TryLoad(path, out var handle)) return handle;
        }
        return IntPtr.Zero;
    }

    internal static IEnumerable<string> ProbePaths()
    {
        var fromEnv = Environment.GetEnvironmentVariable(DirectoryEnvVar);
        if (!string.IsNullOrWhiteSpace(fromEnv)) yield return Path.Combine(fromEnv, FileName);

        yield return Path.Combine(AppContext.BaseDirectory, FileName);

        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, "build", "native", FileName);
            if (File.Exists(candidate)) yield return candidate;
            dir = dir.Parent;
        }
    }

    internal static bool TryLoadFrom(string directory, out IntPtr handle, out string error)
    {
        var path = Path.Combine(directory, FileName);
        if (NativeLibrary.TryLoad(path, out handle))
        {
            error = string.Empty;
            return true;
        }
        error = $"原生引擎未加载：找不到或无法载入 {path}";
        handle = IntPtr.Zero;
        return false;
    }
}
