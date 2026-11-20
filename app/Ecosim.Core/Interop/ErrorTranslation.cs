using System.Runtime.InteropServices;

namespace Ecosim.Core.Interop;

/// <summary>Turns ECOSIM_ERR_* codes into exceptions. Callers must invoke this
/// on the same thread as the failed call: the diagnostic is thread-local.</summary>
internal static class ErrorTranslation
{
    internal static string LastMessage()
    {
        var pointer = NativeMethods.ecosim_last_error();
        return pointer == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(pointer) ?? string.Empty;
    }

    internal static void ThrowIfFailed(string operation, int code)
    {
        if (code == AbiConstants.Ok) return;
        throw new EcosimException(operation, code, LastMessage());
    }
}
