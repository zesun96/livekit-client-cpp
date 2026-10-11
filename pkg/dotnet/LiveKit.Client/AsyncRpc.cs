using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace LiveKit.Client;

public sealed partial class Room
{
    private readonly object _asyncRpcSync = new();
    private readonly HashSet<PendingRpc> _pendingRpc = new();

    /// <summary>Performs RPC without blocking the calling thread.</summary>
    public Task<RpcResponse> PerformRpcAsync(string destinationIdentity, string method,
        string payload, uint responseTimeoutMs = 10_000)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(destinationIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(method);
        ArgumentNullException.ThrowIfNull(payload);
        return Run(() =>
        {
            var pending = new PendingRpc();
            pending.Callback = (_, _, result) =>
            {
                try { pending.Completion.TrySetResult(CopyRpcResponse(result)); }
                catch (Exception error) { pending.Completion.TrySetException(error); }
                finally
                {
                    lock (_asyncRpcSync) _pendingRpc.Remove(pending);
                }
            };
            var options = new Native.RpcPerformOptions();
            Native.lk_rpc_perform_options_init(ref options);
            options.DestinationIdentity = Marshal.StringToCoTaskMemUTF8(destinationIdentity);
            options.Method = Marshal.StringToCoTaskMemUTF8(method);
            options.Payload = Marshal.StringToCoTaskMemUTF8(payload);
            options.ResponseTimeoutMs = responseTimeoutMs;
            lock (_asyncRpcSync) _pendingRpc.Add(pending);
            try
            {
                Native.Check(Native.lk_room_perform_rpc_async(_handle, ref options,
                    pending.Callback, IntPtr.Zero));
                return pending.Completion.Task;
            }
            catch
            {
                lock (_asyncRpcSync) _pendingRpc.Remove(pending);
                throw;
            }
            finally
            {
                Marshal.FreeCoTaskMem(options.DestinationIdentity);
                Marshal.FreeCoTaskMem(options.Method);
                Marshal.FreeCoTaskMem(options.Payload);
            }
        });
    }

    private static RpcResponse CopyRpcResponse(IntPtr result) =>
        new(Native.lk_rpc_result_ok(result) != 0,
            Native.ReadString(result, Native.lk_rpc_result_payload),
            Native.lk_rpc_result_error_code(result),
            Native.ReadString(result, Native.lk_rpc_result_error_message),
            Native.ReadString(result, Native.lk_rpc_result_error_data));

    private void CompletePendingRpcOnDispose()
    {
        lock (_asyncRpcSync)
        {
            foreach (var pending in _pendingRpc)
                pending.Completion.TrySetException(new ObjectDisposedException(nameof(Room)));
            _pendingRpc.Clear();
        }
    }

    private sealed class PendingRpc
    {
        internal readonly TaskCompletionSource<RpcResponse> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal Native.RpcCompletionCallback Callback = null!;
    }
}

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void RpcCompletionCallback(IntPtr userData, IntPtr room, IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_perform_rpc_async(RoomHandle room,
        ref RpcPerformOptions options, RpcCompletionCallback callback, IntPtr userData);
}
