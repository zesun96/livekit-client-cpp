/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace livekit::capture {

enum class AudioDeviceKind {
	Input,
	Output,
};

struct AudioDeviceInfo {
	std::string id;
	std::string label;
	AudioDeviceKind kind = AudioDeviceKind::Input;
	bool is_default = false;
};

using AudioFrameCallback = std::function<void(
    const std::int16_t* samples, std::uint32_t sample_rate, std::uint32_t channels,
    std::uint32_t frames_per_channel, std::int64_t timestamp_us)>;

class AudioCaptureAdapter {
public:
	AudioCaptureAdapter(std::string device_id, AudioFrameCallback callback);
	~AudioCaptureAdapter();

	AudioCaptureAdapter(const AudioCaptureAdapter&) = delete;
	AudioCaptureAdapter& operator=(const AudioCaptureAdapter&) = delete;

	bool Start();
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	std::string DeviceId() const;
	bool SwitchDevice(std::string_view device_id);
	std::string LastError() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

std::vector<AudioDeviceInfo> EnumerateAudioDevices();

} // namespace livekit::capture
