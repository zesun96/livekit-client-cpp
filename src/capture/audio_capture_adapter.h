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
	AudioCaptureAdapter(std::string device_id, AudioFrameCallback callback,
	                    bool system_audio = false);
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

struct AudioPlaybackStats {
	std::uint64_t queued_frames = 0;
	std::uint64_t played_frames = 0;
	std::uint64_t dropped_frames = 0;
	std::uint64_t underrun_frames = 0;
	std::uint32_t buffered_duration_ms = 0;
	std::uint32_t device_latency_ms = 0;
	std::uint32_t estimated_delay_ms = 0;
};

class AudioPlaybackAdapter {
public:
	explicit AudioPlaybackAdapter(std::string device_id = {});
	~AudioPlaybackAdapter();

	AudioPlaybackAdapter(const AudioPlaybackAdapter&) = delete;
	AudioPlaybackAdapter& operator=(const AudioPlaybackAdapter&) = delete;

	bool Start();
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	std::string DeviceId() const;
	bool SwitchDevice(std::string_view device_id);
	bool QueueFrame(const std::int16_t* samples, std::uint32_t sample_rate, std::uint32_t channels,
	                std::uint32_t frames_per_channel);
	bool SetVolume(float volume);
	float Volume() const noexcept;
	void SetMuted(bool muted) noexcept;
	bool IsMuted() const noexcept;
	AudioPlaybackStats Stats() const noexcept;
	std::string LastError() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

std::vector<AudioDeviceInfo> EnumerateAudioDevices();

} // namespace livekit::capture
