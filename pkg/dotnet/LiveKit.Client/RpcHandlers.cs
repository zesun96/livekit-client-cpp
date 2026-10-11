using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed record RpcRequest(string RequestId, string CallerIdentity, string Payload,
    uint ResponseTimeoutMs);

public sealed record RpcReply(string Payload, uint ErrorCode = 0,
    string ErrorMessage = "", string ErrorData = "")
{
    public static RpcReply Error(uint code, string message, string data = "") =>
        new(string.Empty, code, message, data);
}

public sealed partial class Room
{
    private readonly Dictionary<string, RpcHandlerRegistration> _rpcHandlers = new();
    private readonly List<RpcHandlerRegistration> _retiredRpcHandlers = new();

    public void RegisterRpcMethod(string method, Func<RpcRequest, RpcReply> handler)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(method);
        ArgumentNullException.ThrowIfNull(handler);
        Run(() =>
        {
            var registration = new RpcHandlerRegistration(handler);
            try
            {
                Native.Check(Native.lk_room_register_rpc_method_out(_handle, method,
                    registration.Callback, IntPtr.Zero));
                _rpcHandlers.Add(method, registration);
            }
            catch { registration.Dispose(); throw; }
        });
    }

    public void UnregisterRpcMethod(string method)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(method);
        Run(() =>
        {
            Native.Check(Native.lk_room_unregister_rpc_method(_handle, method));
            if (_rpcHandlers.Remove(method, out var registration))
                _retiredRpcHandlers.Add(registration);
        });
    }
}

internal sealed class RpcHandlerRegistration : IDisposable
{
    private readonly Func<RpcRequest, RpcReply> _handler;
    private readonly object _sync = new();
    private readonly Dictionary<int, IntPtr[]> _buffers = new();
    internal Native.RpcHandlerOut Callback { get; }

    internal RpcHandlerRegistration(Func<RpcRequest, RpcReply> handler)
    {
        _handler = handler;
        Callback = Invoke;
    }

    private void Invoke(IntPtr userData, IntPtr invocation, IntPtr result)
    {
        RpcReply reply;
        try
        {
            var raw = Marshal.PtrToStructure<Native.RpcInvocation>(invocation);
            reply = _handler(new RpcRequest(
                Marshal.PtrToStringUTF8(raw.RequestId) ?? string.Empty,
                Marshal.PtrToStringUTF8(raw.CallerIdentity) ?? string.Empty,
                Marshal.PtrToStringUTF8(raw.Payload) ?? string.Empty,
                raw.ResponseTimeoutMs)) ?? RpcReply.Error(1500, "RPC handler returned null");
        }
        catch (Exception error)
        {
            reply = RpcReply.Error(1500, error.Message);
        }

        lock (_sync)
        {
            var threadId = Environment.CurrentManagedThreadId;
            if (_buffers.Remove(threadId, out var previous)) Free(previous);
            var current = new[]
            {
                Marshal.StringToCoTaskMemUTF8(reply.Payload),
                Marshal.StringToCoTaskMemUTF8(reply.ErrorMessage),
                Marshal.StringToCoTaskMemUTF8(reply.ErrorData),
            };
            _buffers[threadId] = current;
            Marshal.StructureToPtr(new Native.RpcHandlerResult
            {
                Payload = current[0],
                ErrorCode = reply.ErrorCode,
                ErrorMessage = current[1],
                ErrorData = current[2],
            }, result, false);
        }
    }

    private static void Free(IntPtr[] pointers)
    {
        foreach (var pointer in pointers) Marshal.FreeCoTaskMem(pointer);
    }

    public void Dispose()
    {
        lock (_sync)
        {
            foreach (var pointers in _buffers.Values) Free(pointers);
            _buffers.Clear();
        }
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct RpcInvocation
    {
        internal IntPtr RequestId;
        internal IntPtr CallerIdentity;
        internal IntPtr Payload;
        internal uint ResponseTimeoutMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RpcHandlerResult
    {
        internal IntPtr Payload;
        internal uint ErrorCode;
        internal IntPtr ErrorMessage;
        internal IntPtr ErrorData;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void RpcHandlerOut(IntPtr userData, IntPtr invocation, IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_register_rpc_method_out(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string method, RpcHandlerOut handler,
        IntPtr userData);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_unregister_rpc_method(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string method);
}
