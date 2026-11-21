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

    /// <summary>Bulk-loads a species plane, e.g. when carrying cells across a
    /// rule-mode switch. Energy and age are reseeded by the engine.</summary>
    public void LoadSpecies(byte[] species)
    {
        ArgumentNullException.ThrowIfNull(species);
        lock (_gate)
        {
            ThrowIfDisposed();
            ErrorTranslation.ThrowIfFailed("load_species",
                NativeMethods.ecosim_load_species(handle, species, species.Length));
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
    /// <summary>
    /// Copies the planes out in one locked section. `includeHeat` also takes
    /// energy/age/resource, which the heat layers need and the species layer
    /// does not - three more buffers per frame is not something to pay for
    /// when nobody is looking at them.
    /// </summary>
    public SpeciesSnapshot Capture(bool includeHeat = false)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var view = EcosimGridViewDto.Create();
            ErrorTranslation.ThrowIfFailed("grid_view",
                NativeMethods.ecosim_world_grid_view(handle, ref view));
            var cells = (long)view.w * view.h;
            var species = new byte[cells];
            Marshal.Copy(view.species, species, 0, species.Length);

            byte[]? energy = null, age = null, resource = null;
            if (includeHeat)
            {
                energy = new byte[cells * 2];
                age = new byte[cells * 2];
                resource = new byte[cells];
                Copy(view.energy, energy);
                Copy(view.age, age);
                Marshal.Copy(view.resource, resource, 0, resource.Length);
            }

            var stats = EcosimStatsDto.Create();
            ErrorTranslation.ThrowIfFailed("stats", NativeMethods.ecosim_stats(handle, ref stats));
            return new SpeciesSnapshot(species, view.w, view.h, (long)stats.generation,
                                       stats.alive_count, energy, age, resource);
        }
    }

    private static void Copy(IntPtr source, byte[] destination)
    {
        // u16 planes copied as bytes: the renderer wants a pointer, not a
        // managed reinterpretation every frame.
        var shortBuffer = new short[destination.Length / 2];
        Marshal.Copy(source, shortBuffer, 0, shortBuffer.Length);
        Buffer.BlockCopy(shortBuffer, 0, destination, 0, destination.Length);
    }

    /// <summary>Renders a snapshot. Takes no lock: the data is immutable and
    /// the engine function needs no world.</summary>
    public static void RenderSnapshot(SpeciesSnapshot snapshot, IntPtr bgra, int rowPitch,
                                      in Palette palette)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var heat = palette.NeedsHeatPlanes;
        if (heat && !snapshot.HasHeatPlanes)
        {
            throw new EcosimException("热力叠加层需要带能量/年龄/resource 的快照，" +
                                      "请让时钟以 CaptureHeat 模式采集");
        }

        var pin = GCHandle.Alloc(snapshot.Species, GCHandleType.Pinned);
        GCHandle energyPin = default, agePin = default, resourcePin = default;
        try
        {
            IntPtr energyPtr = IntPtr.Zero, agePtr = IntPtr.Zero, resourcePtr = IntPtr.Zero;
            if (heat)
            {
                energyPin = GCHandle.Alloc(snapshot.Energy!, GCHandleType.Pinned);
                agePin = GCHandle.Alloc(snapshot.Age!, GCHandleType.Pinned);
                resourcePin = GCHandle.Alloc(snapshot.Resource!, GCHandleType.Pinned);
                energyPtr = energyPin.AddrOfPinnedObject();
                agePtr = agePin.AddrOfPinnedObject();
                resourcePtr = resourcePin.AddrOfPinnedObject();
            }
            var dto = palette.ToDto();
            ErrorTranslation.ThrowIfFailed("render_planes",
                NativeMethods.ecosim_render_planes(pin.AddrOfPinnedObject(), energyPtr, agePtr,
                                                   resourcePtr, snapshot.Width, snapshot.Height,
                                                   bgra, rowPitch, ref dto));
        }
        finally
        {
            pin.Free();
            if (heat)
            {
                energyPin.Free();
                agePin.Free();
                resourcePin.Free();
            }
        }
    }

    /// <summary>The engine's parameter table. Static because it describes the
    /// build, not a world.</summary>
    public static IReadOnlyList<ParamDescription> DescribeParameters()
    {
        var count = NativeMethods.ecosim_param_count();
        if (count <= 0) return Array.Empty<ParamDescription>();

        var list = new List<ParamDescription>(count);
        for (var i = 0; i < count; ++i)
        {
            var dto = EcosimParamDescDto.Create();
            ErrorTranslation.ThrowIfFailed("param_desc",
                NativeMethods.ecosim_param_desc(i, ref dto));
            list.Add(new ParamDescription(
                dto.index, dto.group, dto.species, dto.kind, dto.min_value, dto.max_value,
                dto.default_value,
                Marshal.PtrToStringUTF8(dto.name) ?? "",
                Marshal.PtrToStringUTF8(dto.label) ?? ""));
        }
        return list;
    }

    public int GetParam(int index)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            ErrorTranslation.ThrowIfFailed("param_get",
                NativeMethods.ecosim_param_get(handle, index, out var value));
            return value;
        }
    }

    public void SetParam(int index, int value)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            ErrorTranslation.ThrowIfFailed("param_set",
                NativeMethods.ecosim_param_set(handle, index, value));
        }
    }

    /// <summary>Reads the tail of the engine's population history. Oldest
    /// first. Pulling the whole ring every frame would be 4096 marshalling
    /// calls, so the caller names how many samples it wants.</summary>
    public IReadOnlyList<HistorySample> History(int last)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var total = NativeMethods.ecosim_history_count(handle);
            if (total < 0) total = 0;
            var take = Math.Min(Math.Max(last, 0), total);
            if (take == 0) return Array.Empty<HistorySample>();

            var list = new List<HistorySample>(take);
            // Ask the engine for the newest `take` samples: indices
            // [total - take, total).
            for (var i = total - take; i < total; ++i)
            {
                var dto = EcosimHistorySampleDto.Create();
                ErrorTranslation.ThrowIfFailed("history_get",
                    NativeMethods.ecosim_history_get(handle, i, ref dto));
                list.Add(new HistorySample((long)dto.generation, dto.alive_count,
                                           (int[])dto.species_count.Clone()));
            }
            return list;
        }
    }

    /// <summary>Drains the engine's event queue (oldest first).</summary>
    public IReadOnlyList<SimEvent> PollEvents(int max = 64)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            var buffer = EcosimEventDto.CreateBuffer(max);
            ErrorTranslation.ThrowIfFailed("event_poll",
                NativeMethods.ecosim_event_poll(handle, buffer, max, out var taken));
            var list = new List<SimEvent>(taken);
            for (var i = 0; i < taken; ++i)
            {
                list.Add(new SimEvent((long)buffer[i].generation,
                                      (SimEventKind)buffer[i].kind,
                                      buffer[i].species,
                                      buffer[i].count));
            }
            return list;
        }
    }

    public int HistoryLength
    {
        get
        {
            lock (_gate)
            {
                ThrowIfDisposed();
                return Math.Max(0, NativeMethods.ecosim_history_count(handle));
            }
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
