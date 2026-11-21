using System.Runtime.InteropServices;
using Ecosim.Core;
using Ecosim.Core.Abi;
using Ecosim.Core.Interop;

namespace Ecosim.Tests;

/// <summary>
/// The ABI is only safe if both sides agree byte for byte. These tests are the
/// managed half of the static_asserts in native/src/abi/ecosim_abi.cpp: if a
/// field is reordered on one side, this fails instead of memory corrupting.
/// </summary>
public class AbiLayoutTests
{
    [Theory]
    [InlineData(AbiConstants.StructConfig, typeof(EcosimConfigDto))]
    [InlineData(AbiConstants.StructGridView, typeof(EcosimGridViewDto))]
    [InlineData(AbiConstants.StructPalette, typeof(EcosimPaletteDto))]
    [InlineData(AbiConstants.StructPaintOp, typeof(EcosimPaintOpDto))]
    [InlineData(AbiConstants.StructStats, typeof(EcosimStatsDto))]
    [InlineData(AbiConstants.StructSeed, typeof(EcosimSeedConfigDto))]
    [InlineData(AbiConstants.StructParams, typeof(EcosimParamsDto))]
    [InlineData(AbiConstants.StructParamDesc, typeof(EcosimParamDescDto))]
    public void ManagedStructSizeMatchesNative(int which, Type dto)
    {
        var native = NativeMethods.ecosim_abi_struct_size(which);
        Assert.True(native > 0, $"native does not know struct id {which}");
        Assert.Equal(native, Marshal.SizeOf(dto));
    }

    [Fact]
    public void ParamsLayoutMatchesNative()
    {
        Assert.Equal(140, Marshal.SizeOf<EcosimParamsDto>());
        Assert.Equal(48, Marshal.SizeOf<EcosimParamDescDto>());
        Assert.Equal(12, Marshal.OffsetOf<EcosimParamsDto>("metabolism_scale_permille").ToInt32());
        Assert.Equal(76, Marshal.OffsetOf<EcosimParamsDto>("repro_scale_permille").ToInt32());
    }

    [Fact]
    public void ConfigFieldOffsetsMatchNative()
    {
        // pack=4：5×int32(0..19) | birth_mask[9]@24 | survive_mask[9]@33 | pad | uint64@44 → 52
        Assert.Equal(52, Marshal.SizeOf<EcosimConfigDto>());
        Assert.Equal(20, Marshal.OffsetOf<EcosimConfigDto>("threads").ToInt32());
        Assert.Equal(24, Marshal.OffsetOf<EcosimConfigDto>("birth_mask").ToInt32());
        Assert.Equal(33, Marshal.OffsetOf<EcosimConfigDto>("survive_mask").ToInt32());
        Assert.Equal(44, Marshal.OffsetOf<EcosimConfigDto>("rng_seed").ToInt32());
    }

    [Fact]
    public void StatsAndGridViewHotFieldOffsetsMatchNative()
    {
        Assert.Equal(4, Marshal.OffsetOf<EcosimStatsDto>("generation").ToInt32());
        Assert.Equal(24, Marshal.OffsetOf<EcosimStatsDto>("species_count").ToInt32());
        Assert.Equal(16, Marshal.OffsetOf<EcosimGridViewDto>("species").ToInt32());
        Assert.Equal(88, Marshal.SizeOf<EcosimStatsDto>());
        Assert.Equal(48, Marshal.SizeOf<EcosimGridViewDto>());
    }

    [Fact]
    public void AbiVersion_matches_constant()
    {
        Assert.Equal(AbiConstants.ExpectedAbiVersion, NativeMethods.ecosim_abi_version());
    }
}
