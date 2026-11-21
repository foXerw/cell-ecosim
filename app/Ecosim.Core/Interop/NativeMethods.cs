using System.Runtime.InteropServices;
using Ecosim.Core.Abi;

namespace Ecosim.Core.Interop;

/// <summary>
/// The only place in the codebase that knows P/Invoke exists. Everything above
/// it (World, the app, the tests) talks to <see cref="NativeWorld"/> instead.
/// </summary>
internal static class NativeMethods
{
    private const string Library = "ecosim";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_abi_version();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_abi_struct_size(int which);

    // Returned pointer is thread-local storage owned by the engine: copy it out
    // immediately, never keep it.
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr ecosim_last_error();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr ecosim_world_create(ref EcosimConfigDto cfg);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern void ecosim_world_destroy(IntPtr world);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_world_step(IntPtr world, int n_steps);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_set_topology(IntPtr world, int topology);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern long ecosim_generation(IntPtr world);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_world_grid_view(IntPtr world, ref EcosimGridViewDto view);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_render_frame(IntPtr world, IntPtr bgra, int row_pitch,
                                                ref EcosimPaletteDto palette);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_render_species(IntPtr species, int w, int h, IntPtr bgra,
                                                  int row_pitch, ref EcosimPaletteDto palette);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_render_planes(IntPtr species, IntPtr energy, IntPtr age,
                                                  IntPtr resource, int w, int h, IntPtr bgra,
                                                  int row_pitch, ref EcosimPaletteDto palette);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_paint(IntPtr world, EcosimPaintOpDto[] ops, int count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_clear(IntPtr world, int fill_species);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_load_species(IntPtr world, byte[] species, int count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_randomize(IntPtr world, ref EcosimSeedConfigDto seed_cfg);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_stats(IntPtr world, ref EcosimStatsDto stats);

    // [In, Out] is load-bearing: arrays default to [In] only, so without it
    // the engine's events land in a temporary copy and the caller reads back
    // zeroed structs - which decode as "kind = Extinction", the value 0.
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_snapshot_count(IntPtr world);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_snapshot_info(IntPtr world, int index, out ulong generation);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_snapshot_capture(IntPtr world);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_snapshot_restore(IntPtr world, int index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_event_poll(IntPtr world,
                                               [In, Out] EcosimEventDto[] out_events, int cap,
                                               out int out_count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_param_count();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_param_desc(int index, ref EcosimParamDescDto desc);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_params_get(IntPtr world, ref EcosimParamsDto parms);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_params_set(IntPtr world, ref EcosimParamsDto parms);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_param_get(IntPtr world, int index, out int value);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_param_set(IntPtr world, int index, int value);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong ecosim_hash(IntPtr world);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_history_count(IntPtr world);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_history_get(IntPtr world, int index,
                                                ref EcosimHistorySampleDto sample);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_rle_size(IntPtr world, out int needed);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_to_rle(IntPtr world, byte[] buffer, int capacity,
                                          out int written);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_from_rle(IntPtr world, byte[] rle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int ecosim_from_rle_at(IntPtr world, byte[] rle, int origin_x,
                                               int origin_y);
}
