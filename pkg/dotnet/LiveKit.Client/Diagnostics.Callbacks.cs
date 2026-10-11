using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace LiveKit.Client;

public enum LogSource { LiveKit = 0, WebRtc = 1, WebSocket = 2 }
public sealed record LogRecord(LogLevel Level, LogSource Source, string Message,
    string File, int Line);

[Flags]
public enum TraceCategory : ulong
{
    Lifecycle = 1UL << 0, Signaling = 1UL << 1, Transport = 1UL << 2,
    Track = 1UL << 3, Data = 1UL << 4, Rpc = 1UL << 5, E2ee = 1UL << 6,
    All = (1UL << 7) - 1,
}

public enum TracePhase { Instant = 0, DurationBegin = 1, DurationEnd = 2,
    AsyncBegin = 3, AsyncEnd = 4 }
public sealed record TraceRecord(TracePhase Phase, TraceCategory Category,
    string Name, ulong TimestampUs, ulong ThreadId, ulong CorrelationId);
public sealed record TraceOptions(bool Enabled, TraceCategory Categories = TraceCategory.All);

public static partial class Diagnostics
{
    private static readonly object CallbackSync = new();
    private static readonly Native.LogRecordCallback LogCallbackRoot = OnLogRecord;
    private static readonly Native.TraceRecordCallback TraceCallbackRoot = OnTraceRecord;
    private static Action<LogRecord>? _logCallback;
    private static Action<TraceRecord>? _traceCallback;
    private static int _queuedLogRecords;
    private static int _queuedTraceRecords;

    public static TraceOptions GetTraceOptions()
    {
        var native = new Native.TraceOptions();
        Native.lk_trace_options_init(ref native);
        Native.Check(Native.lk_trace_get_options(ref native));
        return new TraceOptions(native.Enabled != 0, (TraceCategory)native.CategoryMask);
    }

    public static void SetTraceOptions(TraceOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var native = new Native.TraceOptions();
        Native.lk_trace_options_init(ref native);
        native.Enabled = options.Enabled ? 1 : 0;
        native.CategoryMask = (ulong)options.Categories;
        Native.Check(Native.lk_trace_set_options(ref native));
    }

    /// <summary>Sets the process-wide log sink. Pass null to unregister it.</summary>
    public static void SetLogCallback(Action<LogRecord>? callback)
    {
        lock (CallbackSync)
        {
            Native.Check(Native.lk_log_set_callback(callback is null ? IntPtr.Zero :
                Marshal.GetFunctionPointerForDelegate(LogCallbackRoot), IntPtr.Zero));
            Volatile.Write(ref _logCallback, callback);
        }
    }

    /// <summary>Sets the process-wide trace sink. Pass null to unregister it.</summary>
    public static void SetTraceCallback(Action<TraceRecord>? callback)
    {
        lock (CallbackSync)
        {
            Native.Check(Native.lk_trace_set_callback(callback is null ? IntPtr.Zero :
                Marshal.GetFunctionPointerForDelegate(TraceCallbackRoot), IntPtr.Zero));
            Volatile.Write(ref _traceCallback, callback);
        }
    }

    private static void OnLogRecord(IntPtr userData, IntPtr pointer)
    {
        try
        {
            var callback = Volatile.Read(ref _logCallback);
            if (callback is null || pointer == IntPtr.Zero) return;
            if (Interlocked.Increment(ref _queuedLogRecords) > 1024)
            {
                Interlocked.Decrement(ref _queuedLogRecords);
                return;
            }
            var native = Marshal.PtrToStructure<Native.LogRecord>(pointer);
            var copy = new LogRecord((LogLevel)native.Level, (LogSource)native.Source,
                Marshal.PtrToStringUTF8(native.Message) ?? string.Empty,
                Marshal.PtrToStringUTF8(native.File) ?? string.Empty, native.Line);
            ThreadPool.QueueUserWorkItem(_ =>
            {
                try { callback(copy); }
                catch { /* Exceptions must never cross native callback threads. */ }
                finally { Interlocked.Decrement(ref _queuedLogRecords); }
            });
        }
        catch { Interlocked.Decrement(ref _queuedLogRecords); }
    }

    private static void OnTraceRecord(IntPtr userData, IntPtr pointer)
    {
        try
        {
            var callback = Volatile.Read(ref _traceCallback);
            if (callback is null || pointer == IntPtr.Zero) return;
            if (Interlocked.Increment(ref _queuedTraceRecords) > 1024)
            {
                Interlocked.Decrement(ref _queuedTraceRecords);
                return;
            }
            var native = Marshal.PtrToStructure<Native.TraceRecord>(pointer);
            var copy = new TraceRecord((TracePhase)native.Phase,
                (TraceCategory)native.Category,
                Marshal.PtrToStringUTF8(native.Name) ?? string.Empty,
                native.TimestampUs, native.ThreadId, native.CorrelationId);
            ThreadPool.QueueUserWorkItem(_ =>
            {
                try { callback(copy); }
                catch { }
                finally { Interlocked.Decrement(ref _queuedTraceRecords); }
            });
        }
        catch { Interlocked.Decrement(ref _queuedTraceRecords); }
    }
}

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void LogRecordCallback(IntPtr userData, IntPtr record);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void TraceRecordCallback(IntPtr userData, IntPtr record);

    [StructLayout(LayoutKind.Sequential)]
    internal struct LogRecord
    {
        internal nuint StructSize;
        internal int Level;
        internal int Source;
        internal IntPtr Message;
        internal IntPtr File;
        internal int Line;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TraceRecord
    {
        internal nuint StructSize;
        internal int Phase;
        internal ulong Category;
        internal IntPtr Name;
        internal ulong TimestampUs;
        internal ulong ThreadId;
        internal ulong CorrelationId;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TraceOptions
    {
        internal nuint StructSize;
        internal int Enabled;
        internal ulong CategoryMask;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_trace_options_init(ref TraceOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_trace_get_options(ref TraceOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_trace_set_options(ref TraceOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_log_set_callback(IntPtr callback, IntPtr userData);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_trace_set_callback(IntPtr callback, IntPtr userData);
}
