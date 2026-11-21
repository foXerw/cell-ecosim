namespace Ecosim.Core;

/// <summary>
/// Mirrors the macros and enums of native/include/ecosim_abi.h. Values must be
/// copied verbatim from the header, never inferred.
/// </summary>
public static class AbiConstants
{
    /// <summary>The ABI revision this managed build was written against.</summary>
    public const int ExpectedAbiVersion = 1;

    public const int MaxDim = 8192;
    public const int MaxSpecies = 16;

    public const int Ok = 0;
    public const int ErrInvalid = -1;
    public const int ErrSize = -2;
    public const int ErrState = -3;
    public const int ErrRange = -4;

    // EcosimStructId
    public const int StructConfig = 1;
    public const int StructGridView = 2;
    public const int StructPalette = 3;
    public const int StructPaintOp = 4;
    public const int StructStats = 5;
    public const int StructSeed = 6;
    public const int StructParams = 7;
    public const int StructParamDesc = 8;

    public static string Message(int code) => code switch
    {
        Ok => "OK",
        ErrInvalid => "ECOSIM_ERR_INVALID",
        ErrSize => "ECOSIM_ERR_SIZE",
        ErrState => "ECOSIM_ERR_STATE",
        ErrRange => "ECOSIM_ERR_RANGE",
        _ => $"ECOSIM_ERR_{code}",
    };
}
