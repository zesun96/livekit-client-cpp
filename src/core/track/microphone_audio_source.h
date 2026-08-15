/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#ifndef LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H
#define LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H

#include "audio_source.h"

#include "api/audio/audio_device.h"
#include "api/audio/audio_device_defines.h"
#include "api/scoped_refptr.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace livekit::core {

class MicrophoneAudioSource final : public MicrophoneAudioSourceInterface,
                                    public AudioSource,
                                    private webrtc::AudioTransport {
public:
	explicit MicrophoneAudioSource(MicrophoneCaptureOptions options);
	~MicrophoneAudioSource() override;

	bool CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
	                  uint32_t samples_per_channel) override;
	bool Start() override;
	void Stop() override;
	bool IsCapturing() const override;
	std::string DeviceId() const override;
	bool SwitchDevice(const std::string& device_id) override;
	void SetMuted(bool muted) override;
	bool IsMuted() const override;

private:
	int32_t RecordedDataIsAvailable(const void* audio_samples, size_t samples_per_channel,
	                                size_t bytes_per_sample, size_t channels, uint32_t sample_rate,
	                                uint32_t total_delay_ms, int32_t clock_drift,
	                                uint32_t current_mic_level, bool key_pressed,
	                                uint32_t& new_mic_level) override;
	int32_t NeedMorePlayData(size_t samples_per_channel, size_t bytes_per_sample, size_t channels,
	                         uint32_t sample_rate, void* audio_samples, size_t& samples_out,
	                         int64_t* elapsed_time_ms, int64_t* ntp_time_ms) override;
	void PullRenderData(int bits_per_sample, int sample_rate, size_t channels,
	                    size_t frames_per_channel, void* audio_data, int64_t* elapsed_time_ms,
	                    int64_t* ntp_time_ms) override;

	template <typename Function> auto Invoke(Function&& function) {
		using Result = std::invoke_result_t<Function>;
		auto promise = std::make_shared<std::promise<Result>>();
		auto future = promise->get_future();
		{
			std::lock_guard<std::mutex> guard(queue_mutex_);
			tasks_.emplace_back([function = std::forward<Function>(function), promise]() mutable {
				try {
					if constexpr (std::is_void_v<Result>) {
						function();
						promise->set_value();
					} else {
						promise->set_value(function());
					}
				} catch (...) {
					promise->set_exception(std::current_exception());
				}
			});
		}
		queue_condition_.notify_one();
		return future.get();
	}

	void Run(std::stop_token stop_token);
	bool StartOnControlThread(const std::string& device_id);
	void StopOnControlThread();
	bool SelectDevice(const std::string& device_id, std::string& resolved_id);

	MicrophoneCaptureOptions options_;
	std::jthread control_thread_;
	std::mutex queue_mutex_;
	std::condition_variable queue_condition_;
	std::deque<std::function<void()>> tasks_;
	webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_;
	mutable std::mutex state_mutex_;
	std::string device_id_;
	std::atomic_bool capturing_{false};
	std::atomic_bool muted_{false};
};

} // namespace livekit::core

#endif // LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H
