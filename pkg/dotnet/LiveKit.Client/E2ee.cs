using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public enum FrameCryptorDirection { Sender = 0, Receiver = 1 }
public sealed record FrameCryptor(string TrackId, string ParticipantIdentity,
    int Kind, FrameCryptorDirection Direction, bool Enabled, nuint KeyIndex, int State);

public sealed partial class Room
{
    public byte[] ExportE2eeSharedKey(nuint index = 0) => Run(() =>
    {
        var size = Native.lk_room_e2ee_export_shared_key(_handle, index, null, 0);
        if (size == 0) return Array.Empty<byte>();
        if (size > int.MaxValue) throw new LiveKitException(-1, "E2EE key is too large");
        var key = new byte[(int)size];
        Native.lk_room_e2ee_export_shared_key(_handle, index, key, size);
        return key;
    });

    public void RatchetE2eeSharedKey(nuint index = 0) =>
        Run(() => Native.Check(Native.lk_room_e2ee_ratchet_shared_key(_handle, index)));

    public void RemoveE2eeSharedKey(nuint index = 0) =>
        Run(() => Native.Check(Native.lk_room_e2ee_remove_shared_key(_handle, index)));

    public void SetE2eeParticipantKey(string participantIdentity, byte[] key, nuint index = 0)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentNullException.ThrowIfNull(key);
        Run(() => Native.Check(Native.lk_room_e2ee_set_participant_key(_handle,
            participantIdentity, key, (nuint)key.Length, index)));
    }

    public byte[] ExportE2eeParticipantKey(string participantIdentity, nuint index = 0)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        return Run(() =>
        {
            var size = Native.lk_room_e2ee_export_participant_key(_handle,
                participantIdentity, index, null, 0);
            if (size == 0) return Array.Empty<byte>();
            if (size > int.MaxValue) throw new LiveKitException(-1, "E2EE key is too large");
            var key = new byte[(int)size];
            Native.lk_room_e2ee_export_participant_key(_handle,
                participantIdentity, index, key, size);
            return key;
        });
    }

    public void RatchetE2eeParticipantKey(string participantIdentity, nuint index = 0)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        Run(() => Native.Check(Native.lk_room_e2ee_ratchet_participant_key(_handle,
            participantIdentity, index)));
    }

    public void RemoveE2eeParticipantKey(string participantIdentity, nuint index = 0)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        Run(() => Native.Check(Native.lk_room_e2ee_remove_participant_key(_handle,
            participantIdentity, index)));
    }

    public void ClearE2eeKeys() => Run(() => Native.Check(Native.lk_room_e2ee_clear_keys(_handle)));

    public nuint E2eeDataKeyIndex => Run(() => Native.lk_room_e2ee_data_key_index(_handle));

    public void SetE2eeDataKeyIndex(nuint index) =>
        Run(() => Native.Check(Native.lk_room_e2ee_set_data_key_index(_handle, index)));

    public void SetFrameCryptorEnabled(string trackId, FrameCryptorDirection direction, bool enabled)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(trackId);
        Run(() => Native.Check(Native.lk_room_e2ee_set_frame_cryptor_enabled(_handle,
            trackId, (int)direction, enabled ? 1 : 0)));
    }

    public void SetFrameCryptorKeyIndex(string trackId, FrameCryptorDirection direction, nuint index)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(trackId);
        Run(() => Native.Check(Native.lk_room_e2ee_set_frame_cryptor_key_index(_handle,
            trackId, (int)direction, index)));
    }

    public nuint SetE2eeParticipantEnabled(string participantIdentity, bool enabled)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        return Run(() =>
        {
            Native.Check(Native.lk_room_e2ee_set_participant_enabled(_handle,
                participantIdentity, enabled ? 1 : 0, out var updated));
            return updated;
        });
    }

    public IReadOnlyList<FrameCryptor> GetFrameCryptors() => Run(() =>
    {
        Native.Check(Native.lk_frame_cryptor_list_create(_handle, out var list));
        try
        {
            var count = Native.lk_frame_cryptor_list_count(list);
            if (count > int.MaxValue) throw new LiveKitException(-1, "Too many frame cryptors");
            var result = new List<FrameCryptor>((int)count);
            for (nuint i = 0; i < count; i++)
            {
                var info = new Native.FrameCryptorInfo();
                Native.lk_frame_cryptor_info_init(ref info);
                Native.Check(Native.lk_frame_cryptor_list_info(list, i, ref info));
                result.Add(new FrameCryptor(
                    ReadFrameCryptorString(list, i, Native.lk_frame_cryptor_list_track_id),
                    ReadFrameCryptorString(list, i, Native.lk_frame_cryptor_list_participant_identity),
                    info.Kind, (FrameCryptorDirection)info.Direction,
                    info.Enabled != 0, info.KeyIndex, info.State));
            }
            return result;
        }
        finally { Native.lk_frame_cryptor_list_destroy(list); }
    });

    private static string ReadFrameCryptorString(IntPtr list, nuint index,
        Native.IndexedStringGetter getter)
    {
        var size = getter(list, index, null, 0);
        if (size == 0) return string.Empty;
        if (size > int.MaxValue) throw new LiveKitException(-1, "Frame cryptor string is too large");
        var bytes = new byte[(int)size];
        getter(list, index, bytes, size);
        var length = Array.IndexOf(bytes, (byte)0);
        return Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct FrameCryptorInfo
    {
        internal nuint StructSize;
        internal int Kind;
        internal int Direction;
        internal int Enabled;
        internal nuint KeyIndex;
        internal int State;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_participant_enabled(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity, int enabled,
        out nuint updatedCount);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_frame_cryptor_list_create(RoomHandle room, out IntPtr list);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_frame_cryptor_list_destroy(IntPtr list);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_frame_cryptor_list_count(IntPtr list);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_frame_cryptor_info_init(ref FrameCryptorInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_frame_cryptor_list_info(IntPtr list, nuint index,
        ref FrameCryptorInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_frame_cryptor_list_track_id(IntPtr list, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_frame_cryptor_list_participant_identity(IntPtr list,
        nuint index, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_e2ee_export_shared_key(RoomHandle room, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_ratchet_shared_key(RoomHandle room, nuint index);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_remove_shared_key(RoomHandle room, nuint index);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_participant_key(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity, byte[] key,
        nuint size, nuint index);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_e2ee_export_participant_key(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_ratchet_participant_key(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity, nuint index);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_remove_participant_key(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity, nuint index);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_clear_keys(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_e2ee_data_key_index(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_data_key_index(RoomHandle room, nuint index);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_frame_cryptor_enabled(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackId, int direction, int enabled);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_frame_cryptor_key_index(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackId, int direction, nuint index);
}
