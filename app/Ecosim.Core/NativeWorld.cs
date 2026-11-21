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

    public void Paint(IReadOnlyList<PaintOp> ops)
    {
        ArgumentNullException.ThrowIfNull(ops);
        lock (_gate)
        {
            ThrowIfDisposed();
            if (ops.Count == 0) return;
            var dtos = new EcosimPaintOpDto[ops.Count];
            for (var i = 0; i < ops.Count; ++i) dtos[i] = EcosimPaintOpDto.From(ops[i]);
            ErrorTranslation.ThrowIfFailed("paint",
                NativeMethods.ecosim_paint(handle, dtos, dtos.Length));
        }
    }

    /// <summary>Renders straight into memory the caller owns. The app uses this
    /// to write into a locked bitmap's back buffer with zero copies.</summary>
    public void RenderToPointer(IntPtr bgra, int rowPitch, in Palette palette)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var dto = palette.ToDto();
            ErrorTranslation.ThrowIfFailed("render_frame",
                NativeMethods.ecosim_render_frame(handle, bgra, rowPitch, ref dto));
        }
    }

    public unsafe void RenderFrame(Memory<byte> destination, int rowPitch, in Palette palette)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            using var pin = destination.Pin();
            var dto = palette.ToDto();
            ErrorTranslation.ThrowIfFailed("render_frame",
                NativeMethods.ecosim_render_frame(handle, (IntPtr)pin.Pointer, rowPitch, ref dto));
        }
    }

    public SimStats Stats()
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var dto = EcosimStatsDto.Create();
            ErrorTranslation.ThrowIfFailed("stats", NativeMethods.ecosim_stats(handle, ref dto));
            return new SimStats((long)dto.generation, dto.alive_count, dto.w, dto.h,
                                (int[])dto.species_count.Clone());
        }
    }

    public void Clear(int fillSpecies)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            ErrorTranslation.ThrowIfFailed("clear",
                NativeMethods.ecosim_clear(handle, fillSpecies));
        }
    }

    public void Randomize(in SeedConfig config)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var dto = EcosimSeedConfigDto.Create(config);
            ErrorTranslation.ThrowIfFailed("randomize",
                NativeMethods.ecosim_randomize(handle, ref dto));
        }
    }

    /// <summary>Copies the species plane plus stats out in one locked section.
    /// The result is immutable, so everything downstream can work lock-free.</summary>
    public SpeciesSnapshot Capture()
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var view = EcosimGridViewDto.Create();
            ErrorTranslation.ThrowIfFailed("grid_view",
                NativeMethods.ecosim_world_grid_view(handle, ref view));
            var bytes = new byte[(long)view.w * view.h];
            Marshal.Copy(view.species, bytes, 0, bytes.Length);

            var stats = EcosimStatsDto.Create();
            ErrorTranslation.ThrowIfFailed("stats", NativeMethods.ecosim_stats(handle, ref stats));
            return new SpeciesSnapshot(bytes, view.w, view.h, (long)stats.generation,
                                       stats.alive_count);
        }
    }

    public ulong Hash()
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            return NativeMethods.ecosim_hash(handle);
        }
    }

    public string ToRle()
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            ErrorTranslation.ThrowIfFailed("rle_size",
                NativeMethods.ecosim_rle_size(handle, out var needed));
            var buffer = new byte[needed];
            ErrorTranslation.ThrowIfFailed("to_rle",
                NativeMethods.ecosim_to_rle(handle, buffer, needed, out var written));
            return System.Text.Encoding.UTF8.GetString(buffer, 0, Math.Max(written - 1, 0));
        }
    }

    public void FromRleAt(string text, int originX, int originY)
    {
        ArgumentNullException.ThrowIfNull(text);
        lock (_gate)
        {
            ThrowIfDisposed();
            var bytes = System.Text.Encoding.UTF8.GetBytes(text + "\0");
            ErrorTranslation.ThrowIfFailed("from_rle",
                NativeMethods.ecosim_from_rle_at(handle, bytes, originX, originY));
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
