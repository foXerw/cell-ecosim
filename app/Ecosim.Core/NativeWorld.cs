using System.Runtime.InteropServices;
using Ecosim.Core.Abi;
using Ecosim.Core.Interop;

namespace Ecosim.Core;

/// <summary>
/// Owns one native EcosimWorld handle.
///
/// Two rules make this class the safety net rather than a thin wrapper:
///  1. every native call happens inside <c>_gate</c>. The engine hands out
///     borrowed pointers into its own buffers and swaps them while stepping, so
///     serialising calls here is what makes "render straight into the bitmap"
///     race-free (design appendix A.2).
///  2. every return code becomes an exception carrying the engine's diagnostic.
/// </summary>
public sealed class NativeWorld : SafeHandle
{
    internal const int ExpectedAbiVersion = AbiConstants.ExpectedAbiVersion;

    private readonly object _gate = new();

    private NativeWorld(IntPtr handle) : base(handle, ownsHandle: true)
    {
    }

    public override bool IsInvalid => handle == IntPtr.Zero;

    internal object Gate => _gate;

    /// <summary>Must be held whenever borrowed native memory is read together
    /// with a render, so a step cannot slip between them.</summary>
    internal void Enter() => Monitor.Enter(_gate);

    internal void Exit() => Monitor.Exit(_gate);

    public static NativeWorld Create(WorldConfig cfg)
    {
        EnsureNativeAvailable();
        CheckAbiVersion();

        var dto = EcosimConfigDto.Create(cfg);
        IntPtr handle;
        try
        {
            handle = NativeMethods.ecosim_world_create(ref dto);
        }
        catch (EntryPointNotFoundException ex)
        {
            throw new EcosimException("原生引擎缺少 ecosim_world_create 导出", ex);
        }

        if (handle == IntPtr.Zero)
        {
            // Creation reports failure as NULL plus a diagnostic. The code is
            // ECOSIM_ERR_INVALID whether the config or the allocation failed.
            throw new EcosimException("world_create", AbiConstants.ErrInvalid,
                                      ErrorTranslation.LastMessage());
        }

        var world = new NativeWorld(handle) { _topology = cfg.Topology };
        return world;
    }

    public void Step(int nSteps)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            ErrorTranslation.ThrowIfFailed("world_step", NativeMethods.ecosim_world_step(handle, nSteps));
        }
    }

    public long Generation
    {
        get
        {
            lock (_gate)
            {
                ThrowIfDisposed();
                var value = NativeMethods.ecosim_generation(handle);
                if (value < 0)
                {
                    throw new EcosimException("generation", AbiConstants.ErrInvalid,
                                              ErrorTranslation.LastMessage());
                }
                return value;
            }
        }
    }

    public GridTopology Topology
    {
        get
        {
            lock (_gate)
            {
                ThrowIfDisposed();
                return _topology;
            }
        }
        set
        {
            lock (_gate)
            {
                ThrowIfDisposed();
                ErrorTranslation.ThrowIfFailed("set_topology",
                    NativeMethods.ecosim_set_topology(handle, (int)value));
                _topology = value;
            }
        }
    }

    private GridTopology _topology = GridTopology.Torus;

    protected override bool ReleaseHandle()
    {
        // The engine tolerates a NULL destroy, so a half-built handle cannot leak.
        NativeMethods.ecosim_world_destroy(handle);
        return true;
    }

    private void ThrowIfDisposed()
    {
        if (IsClosed) throw new ObjectDisposedException(nameof(NativeWorld), "ecosim 世界句柄已释放");
    }

    private static void EnsureNativeAvailable()
    {
        try
        {
            NativeMethods.ecosim_abi_version();
        }
        catch (DllNotFoundException ex)
        {
            var probed = string.Join("；", NativeLibraryLoader.ProbePaths());
            throw new EcosimException($"原生引擎未加载，已尝试以下路径：{probed}", ex);
        }
    }

    private static void CheckAbiVersion()
    {
        var actual = NativeMethods.ecosim_abi_version();
        var message = DescribeVersionCheck(ExpectedAbiVersion, actual);
        if (message.Length > 0) throw new EcosimException(message);
    }

    internal static string DescribeVersionCheck(int expected, int actual)
        => actual == expected
            ? string.Empty
            : $"原生引擎版本不匹配：需要 v{expected}，实际 v{actual}";
}
