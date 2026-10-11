const std = @import("std");
const livekit = @import("livekit");
const c = livekit.c;

pub fn main() !void {
    const allocator = std.heap.page_allocator;
    try livekit.init();
    defer livekit.shutdown() catch {};

    var devices: ?*c.lk_media_device_list_t = null;
    try livekit.check(c.lk_media_device_list_create(&devices));
    defer c.lk_media_device_list_destroy(devices);

    for (0..c.lk_media_device_list_count(devices)) |index| {
        var info: c.lk_media_device_info_t = undefined;
        try livekit.check(c.lk_media_device_list_info(devices, index, &info));
        const id = try livekit.readString(allocator, c.lk_media_device_list_id, .{ devices, index });
        defer allocator.free(id);
        const label = try livekit.readString(allocator, c.lk_media_device_list_label, .{ devices, index });
        defer allocator.free(label);
        std.debug.print("device kind={d} default={d} id={s} label={s}\n", .{ info.kind, info.is_default, id, label });
    }

    var screens: ?*c.lk_screen_source_list_t = null;
    try livekit.check(c.lk_screen_source_list_create(&screens));
    defer c.lk_screen_source_list_destroy(screens);

    for (0..c.lk_screen_source_list_count(screens)) |index| {
        var info: c.lk_screen_source_info_t = undefined;
        try livekit.check(c.lk_screen_source_list_info(screens, index, &info));
        const id = try livekit.readString(allocator, c.lk_screen_source_list_id, .{ screens, index });
        defer allocator.free(id);
        const label = try livekit.readString(allocator, c.lk_screen_source_list_label, .{ screens, index });
        defer allocator.free(label);
        std.debug.print("screen kind={d} {d}x{d} id={s} label={s}\n", .{ info.kind, info.width, info.height, id, label });
    }
}
