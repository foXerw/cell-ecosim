namespace Ecosim.Core;

/// <summary>
/// Every failure that crosses the native border arrives as this, with the
/// engine's own diagnostic attached instead of a bare return code.
/// </summary>
public class EcosimException : Exception
{
    /// <summary>The ECOSIM_ERR_* code, or -1 when the failure is managed-side.</summary>
    public int Code { get; }

    public EcosimException(string message) : base(message)
    {
        Code = AbiConstants.ErrInvalid;
    }

    public EcosimException(string message, Exception inner) : base(message, inner)
    {
        Code = AbiConstants.ErrInvalid;
    }

    internal EcosimException(string operation, int code, string? nativeMessage)
        : base(Describe(operation, code, nativeMessage))
    {
        Code = code;
    }

    private static string Describe(string operation, int code, string? nativeMessage)
    {
        var detail = string.IsNullOrWhiteSpace(nativeMessage) ? AbiConstants.Message(code) : nativeMessage;
        return $"{operation} 失败（{AbiConstants.Message(code)}）：{detail}";
    }
}
