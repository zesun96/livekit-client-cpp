const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const native_lib_dir = b.option([]const u8, "native-lib-dir", "Directory containing livekitclient import library/shared library");

    const translated = b.addTranslateC(.{
        .root_source_file = b.path("../../include/livekit/capi/livekit.h"),
        .target = target,
        .optimize = optimize,
    });
    const c_module = translated.addModule("livekit_c");

    const module = b.addModule("livekit", .{
        .root_source_file = b.path("src/livekit.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    module.addImport("livekit_c", c_module);
    module.linkSystemLibrary("livekitclient", .{});
    if (native_lib_dir) |path| module.addLibraryPath(.{ .cwd_relative = path });

    const tests = b.addTest(.{ .root_module = module });
    const run_tests = b.addRunArtifact(tests);
    const test_step = b.step("test", "Run Zig binding tests");
    test_step.dependOn(&run_tests.step);

    const native_test_module = b.createModule(.{
        .root_source_file = b.path("tests/native.zig"),
        .target = target,
        .optimize = optimize,
    });
    native_test_module.addImport("livekit", module);
    const native_tests = b.addTest(.{ .root_module = native_test_module });
    test_step.dependOn(&b.addRunArtifact(native_tests).step);

    const example_step = b.step("example", "Build Zig examples");
    inline for (.{ "room", "media_devices", "data_transfer", "stream_writer", "data_track_schema", "publish_audio", "publish_video", "token_source", "e2ee" }) |name| {
        const example_module = b.createModule(.{
            .root_source_file = b.path("examples/" ++ name ++ ".zig"),
            .target = target,
            .optimize = optimize,
        });
        example_module.addImport("livekit", module);
        const example = b.addExecutable(.{ .name = "livekit_" ++ name ++ "_zig", .root_module = example_module });
        example_step.dependOn(&b.addInstallArtifact(example, .{}).step);
    }

    const integration_module = b.createModule(.{
        .root_source_file = b.path("tests/two_peer_smoke.zig"),
        .target = target,
        .optimize = optimize,
    });
    integration_module.addImport("livekit", module);
    const integration_exe = b.addExecutable(.{ .name = "livekit_zig_two_peer_smoke", .root_module = integration_module });
    const integration_build = b.step("integration-build", "Build optional two-peer smoke test");
    integration_build.dependOn(&b.addInstallArtifact(integration_exe, .{}).step);
    const integration = b.step("integration", "Run two-peer smoke with LIVEKIT_URL and two tokens");
    integration.dependOn(&b.addRunArtifact(integration_exe).step);
}
