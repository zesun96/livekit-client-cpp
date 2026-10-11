using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed record RemoteTrackPublication(string Sid, string Name, string MimeType,
    int Kind, int Source, uint Width, uint Height, bool IsMuted,
    bool IsSubscribed, int SubscriptionStatus, int Encryption)
{
    public RemoteTrackInfo? SubscribedTrack { get; init; }
}
public sealed record RemoteTrackInfo(string Sid, string Name, int Kind, int Source,
    int StreamState, uint Width, uint Height, bool Enabled);

public sealed partial class Room
{
    public Participant GetLocalParticipant() => Run(() =>
    {
        Native.Check(Native.lk_room_create_local_participant_snapshot(_handle, out var snapshot));
        try
        {
            var info = new Native.ParticipantSnapshotInfo();
            Native.lk_local_participant_snapshot_info_init(ref info);
            Native.Check(Native.lk_local_participant_snapshot_info(snapshot, ref info));
            return new Participant(
                Native.ReadString(snapshot, Native.lk_local_participant_snapshot_sid),
                Native.ReadString(snapshot, Native.lk_local_participant_snapshot_identity),
                Native.ReadString(snapshot, Native.lk_local_participant_snapshot_name),
                Native.ReadString(snapshot, Native.lk_local_participant_snapshot_metadata),
                info.AudioLevel, info.ConnectionQuality, info.IsSpeaking != 0, true,
                ReadAttributes(snapshot, Native.lk_local_participant_snapshot_attribute_count,
                    Native.lk_local_participant_snapshot_attribute_key,
                    Native.lk_local_participant_snapshot_attribute_value))
            {
                Permissions = ReadPermissions(snapshot, Native.lk_local_participant_snapshot_permissions),
            };
        }
        finally { Native.lk_local_participant_snapshot_destroy(snapshot); }
    });

    private static Participant CopyRemoteParticipant(IntPtr value)
    {
        var info = new Native.ParticipantSnapshotInfo();
        Native.lk_remote_participant_snapshot_info_init(ref info);
        Native.Check(Native.lk_remote_participant_snapshot_info(value, ref info));
        var count = Native.lk_remote_participant_snapshot_publication_count(value);
        if (count > int.MaxValue) throw new LiveKitException(-1, "Too many track publications");
        var publications = new List<RemoteTrackPublication>((int)count);
        for (nuint i = 0; i < count; i++)
        {
            Native.Check(Native.lk_remote_participant_snapshot_publication_at(value, i,
                out var publication));
            var pubInfo = new Native.RemoteTrackPublicationInfo();
            Native.lk_remote_track_publication_snapshot_info_init(ref pubInfo);
            Native.Check(Native.lk_remote_track_publication_snapshot_info(publication, ref pubInfo));
            publications.Add(new RemoteTrackPublication(
                Native.ReadString(publication, Native.lk_remote_track_publication_snapshot_sid),
                Native.ReadString(publication, Native.lk_remote_track_publication_snapshot_name),
                Native.ReadString(publication, Native.lk_remote_track_publication_snapshot_mime_type),
                pubInfo.Kind, pubInfo.Source, pubInfo.Width, pubInfo.Height,
                pubInfo.IsMuted != 0, pubInfo.HasSubscribedTrack != 0,
                pubInfo.SubscriptionStatus, pubInfo.Encryption)
            {
                SubscribedTrack = ReadSubscribedTrack(publication),
            });
        }
        return new Participant(
            Native.ReadString(value, Native.lk_remote_participant_snapshot_sid),
            Native.ReadString(value, Native.lk_remote_participant_snapshot_identity),
            Native.ReadString(value, Native.lk_remote_participant_snapshot_name),
            Native.ReadString(value, Native.lk_remote_participant_snapshot_metadata),
            info.AudioLevel, info.ConnectionQuality, info.IsSpeaking != 0, false,
            ReadAttributes(value, Native.lk_remote_participant_snapshot_attribute_count,
                Native.lk_remote_participant_snapshot_attribute_key,
                Native.lk_remote_participant_snapshot_attribute_value), publications)
        {
            Permissions = ReadPermissions(value, Native.lk_remote_participant_snapshot_permissions),
        };
    }

    private static RemoteTrackInfo? ReadSubscribedTrack(IntPtr publication)
    {
        Native.Check(Native.lk_remote_track_publication_snapshot_track(publication, out var track));
        if (track == IntPtr.Zero) return null;
        var info = new Native.RemoteTrackSnapshotInfo
        {
            StructSize = (nuint)Marshal.SizeOf<Native.RemoteTrackSnapshotInfo>(),
        };
        Native.Check(Native.lk_remote_track_snapshot_info(track, ref info));
        return new RemoteTrackInfo(Native.ReadString(track, Native.lk_remote_track_snapshot_sid),
            Native.ReadString(track, Native.lk_remote_track_snapshot_name),
            info.Kind, info.Source, info.StreamState, info.Width, info.Height,
            info.Enabled != 0);
    }

    private static ParticipantPermissions ReadPermissions(IntPtr snapshot,
        Native.PermissionsGetter getter)
    {
        var pointer = Marshal.AllocHGlobal(Marshal.SizeOf<Native.ParticipantPermissions>());
        try
        {
            Native.Check(getter(snapshot, pointer));
            return CopyPermissions(pointer);
        }
        finally { Marshal.FreeHGlobal(pointer); }
    }

    private static IReadOnlyDictionary<string, string> ReadAttributes(IntPtr snapshot,
        Native.CountGetter countGetter, Native.IndexedStringGetter keyGetter,
        Native.IndexedStringGetter valueGetter)
    {
        var count = countGetter(snapshot);
        if (count > int.MaxValue) throw new LiveKitException(-1, "Too many participant attributes");
        var result = new Dictionary<string, string>((int)count);
        for (nuint i = 0; i < count; i++)
            result[ReadIndexed(snapshot, i, keyGetter)] = ReadIndexed(snapshot, i, valueGetter);
        return result;
    }

    private static string ReadIndexed(IntPtr snapshot, nuint index, Native.IndexedStringGetter getter)
    {
        var size = getter(snapshot, index, null, 0);
        if (size == 0) return string.Empty;
        if (size > int.MaxValue) throw new LiveKitException(-1, "Attribute is too large");
        var bytes = new byte[(int)size];
        getter(snapshot, index, bytes, size);
        var length = Array.IndexOf(bytes, (byte)0);
        return System.Text.Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
    }

    public void SetLocalAttributes(IReadOnlyDictionary<string, string> attributes)
    {
        ArgumentNullException.ThrowIfNull(attributes);
        Run(() =>
        {
            using var arena = new NativeArena();
            var values = new Native.Attribute[attributes.Count];
            var index = 0;
            foreach (var pair in attributes)
                values[index++] = new Native.Attribute { Key = arena.Utf8(pair.Key),
                    Value = arena.Utf8(pair.Value) };
            var pointer = values.Length == 0 ? IntPtr.Zero : arena.StructArray(values);
            Native.Check(Native.lk_local_participant_set_attributes(_handle, pointer,
                (nuint)values.Length));
        });
    }
}

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate nuint CountGetter(IntPtr handle);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int PermissionsGetter(IntPtr snapshot, IntPtr permissions);

    [StructLayout(LayoutKind.Sequential)]
    internal struct ParticipantSnapshotInfo
    {
        internal nuint StructSize;
        internal float AudioLevel;
        internal int ConnectionQuality;
        internal int IsSpeaking;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RemoteTrackPublicationInfo
    {
        internal nuint StructSize;
        internal int Kind;
        internal int Source;
        internal uint Width;
        internal uint Height;
        internal int IsMuted;
        internal int IsSimulcasted;
        internal int SubscriptionAllowed;
        internal int SubscriptionStatus;
        internal int HasSubscriptionError;
        internal int SubscriptionError;
        internal int HasSubscribedTrack;
        internal int Encryption;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RemoteTrackSnapshotInfo
    {
        internal nuint StructSize;
        internal int Kind;
        internal int Source;
        internal int StreamState;
        internal uint Width;
        internal uint Height;
        internal int Enabled;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Attribute
    {
        internal IntPtr Key;
        internal IntPtr Value;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_local_participant_snapshot(RoomHandle room,
        out IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_local_participant_snapshot_destroy(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_local_participant_snapshot_info_init(ref ParticipantSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_participant_snapshot_info(IntPtr snapshot,
        ref ParticipantSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_participant_snapshot_permissions(IntPtr snapshot,
        IntPtr permissions);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_sid(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_identity(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_name(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_metadata(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_attribute_count(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_attribute_key(IntPtr snapshot,
        nuint index, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_snapshot_attribute_value(IntPtr snapshot,
        nuint index, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_remote_participant_snapshot_info_init(ref ParticipantSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_participant_snapshot_info(IntPtr snapshot,
        ref ParticipantSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_participant_snapshot_permissions(IntPtr snapshot,
        IntPtr permissions);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_attribute_count(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_attribute_key(IntPtr snapshot,
        nuint index, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_attribute_value(IntPtr snapshot,
        nuint index, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_publication_count(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_participant_snapshot_publication_at(IntPtr snapshot,
        nuint index, out IntPtr publication);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_remote_track_publication_snapshot_info_init(
        ref RemoteTrackPublicationInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_track_publication_snapshot_info(IntPtr publication,
        ref RemoteTrackPublicationInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_track_publication_snapshot_sid(IntPtr publication,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_track_publication_snapshot_name(IntPtr publication,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_track_publication_snapshot_mime_type(IntPtr publication,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_track_snapshot_info(IntPtr track,
        ref RemoteTrackSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_track_snapshot_sid(IntPtr track,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_track_snapshot_name(IntPtr track,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_participant_set_attributes(RoomHandle room,
        IntPtr attributes, nuint count);
}
