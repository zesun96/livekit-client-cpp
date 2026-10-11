using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed record StreamProgress(ulong BytesSent, ulong? TotalSize);
public enum StreamCompletionStatus { Completed = 0, Cancelled = 1, Failed = 2 }
public sealed record StreamCompletion(StreamCompletionStatus Status, string StreamId,
    ulong BytesSent, ulong? TotalSize, string Reason, int ErrorDomain, int ErrorCode);

internal sealed class StreamCallbackRegistration
{
    private readonly Native.StreamProgressCallback? _progress;
    private readonly Native.StreamCompletionCallback? _completion;

    internal IntPtr ProgressPointer => _progress is null ? IntPtr.Zero :
        Marshal.GetFunctionPointerForDelegate(_progress);
    internal IntPtr CompletionPointer => _completion is null ? IntPtr.Zero :
        Marshal.GetFunctionPointerForDelegate(_completion);

    internal StreamCallbackRegistration(Action<StreamProgress>? progress,
        Action<StreamCompletion>? completion, Action<Action> dispatch)
    {
        if (progress is not null)
            _progress = (_, sent, hasTotal, total) =>
            {
                var value = new StreamProgress(sent, hasTotal != 0 ? total : null);
                dispatch(() => progress(value));
            };
        if (completion is not null)
            _completion = (_, pointer) =>
            {
                try
                {
                    var raw = Marshal.PtrToStructure<Native.StreamCompletionView>(pointer);
                    var value = new StreamCompletion((StreamCompletionStatus)raw.Status,
                        Marshal.PtrToStringUTF8(raw.StreamId) ?? string.Empty,
                        raw.BytesSent, raw.HasTotalSize != 0 ? raw.TotalSize : null,
                        Marshal.PtrToStringUTF8(raw.Reason) ?? string.Empty,
                        raw.ErrorDomain, raw.ErrorCode);
                    dispatch(() => completion(value));
                }
                catch (Exception error) { dispatch(() => throw error); }
            };
    }
}
