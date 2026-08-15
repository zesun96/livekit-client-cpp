/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "microphone_audio_source.h"

#include "../detail/global_task_queue.h"

#include "api/audio/create_audio_device_module.h"
#include "api/environment/environment_factory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <objbase.h>
#endif

namespace livekit::core {
namespace {

constexpr uint32_t kMicrophoneSampleRate = 48000;
constexpr uint32_t kMicrophoneChannels = 1;

} // namespace

MicrophoneAudioSource::MicrophoneAudioSource(MicrophoneCaptureOptions options)
    : AudioSource(options.processing, kMicrophoneSampleRate, kMicrophoneChannels,
                  options.queue_size_ms, GetGlobalTaskQueueFactory()),
      options_(std::move(options)), control_thread_([this](std::stop_token token) { Run(token); }) {
}

MicrophoneAudioSource::~MicrophoneAudioSource() {
	Stop();
	control_thread_.request_stop();
	queue_condition_.notify_one();
	if (control_thread_.joinable()) {
		control_thread_.join();
	}
}

bool MicrophoneAudioSource::CaptureFrame(void* audio_data, uint32_t sample_rate,
                                         uint32_t num_channels, uint32_t samples_per_channel) {
	return AudioSource::CaptureFrame(audio_data, sample_rate, num_channels, samples_per_channel);
}

void MicrophoneAudioSource::Run(std::stop_token stop_token) {
#if defined(_WIN32)
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool uninitialize_com = com_result == S_OK || com_result == S_FALSE;
#endif
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(queue_mutex_);
			queue_condition_.wait(lock,
			                      [&] { return stop_token.stop_requested() || !tasks_.empty(); });
			if (tasks_.empty()) {
				if (stop_token.stop_requested()) {
					break;
				}
				continue;
			}
			task = std::move(tasks_.front());
			tasks_.pop_front();
		}
		task();
	}
	StopOnControlThread();
#if defined(_WIN32)
	if (uninitialize_com) {
		CoUninitialize();
	}
#endif
}

bool MicrophoneAudioSource::SelectDevice(const std::string& requested_id,
                                         std::string& resolved_id) {
	if (requested_id.empty()) {
		if (audio_device_->SetRecordingDevice(webrtc::AudioDeviceModule::kDefaultDevice) != 0) {
			return false;
		}
		resolved_id.clear();
		return true;
	}
	const int16_t count = audio_device_->RecordingDevices();
	for (int16_t index = 0; index < count; ++index) {
		std::array<char, webrtc::kAdmMaxDeviceNameSize> label{};
		std::array<char, webrtc::kAdmMaxGuidSize> id{};
		if (audio_device_->RecordingDeviceName(static_cast<uint16_t>(index), label.data(),
		                                       id.data()) == 0 &&
		    requested_id == id.data()) {
			if (audio_device_->SetRecordingDevice(static_cast<uint16_t>(index)) != 0) {
				return false;
			}
			resolved_id = requested_id;
			return true;
		}
	}
	return false;
}

bool MicrophoneAudioSource::StartOnControlThread(const std::string& requested_id) {
	if (capturing_.load()) {
		return true;
	}
	const auto environment = webrtc::CreateEnvironment();
	audio_device_ = webrtc::CreateAudioDeviceModule(
	    environment, webrtc::AudioDeviceModule::kPlatformDefaultAudio, true);
	if (audio_device_ == nullptr || audio_device_->Init() != 0) {
		audio_device_ = nullptr;
		return false;
	}
	std::string resolved_id;
	if (!SelectDevice(requested_id, resolved_id)) {
		StopOnControlThread();
		return false;
	}
	if (audio_device_->InitMicrophone() != 0 || audio_device_->SetStereoRecording(false) != 0) {
		StopOnControlThread();
		return false;
	}
	if (audio_device_->RegisterAudioCallback(this) != 0 || audio_device_->InitRecording() != 0 ||
	    audio_device_->StartRecording() != 0) {
		StopOnControlThread();
		return false;
	}
	{
		std::lock_guard<std::mutex> guard(state_mutex_);
		device_id_ = std::move(resolved_id);
	}
	capturing_.store(true);
	return true;
}

void MicrophoneAudioSource::StopOnControlThread() {
	if (audio_device_ != nullptr) {
		if (audio_device_->Recording()) {
			audio_device_->StopRecording();
		}
		audio_device_->RegisterAudioCallback(nullptr);
		audio_device_->Terminate();
		audio_device_ = nullptr;
	}
	capturing_.store(false);
}

bool MicrophoneAudioSource::Start() {
	std::string device_id;
	{
		std::lock_guard<std::mutex> guard(state_mutex_);
		device_id = device_id_.empty() ? options_.device_id : device_id_;
	}
	return Invoke([this, device_id] { return StartOnControlThread(device_id); });
}

void MicrophoneAudioSource::Stop() {
	Invoke([this] { StopOnControlThread(); });
}

bool MicrophoneAudioSource::IsCapturing() const { return capturing_.load(); }

std::string MicrophoneAudioSource::DeviceId() const {
	std::lock_guard<std::mutex> guard(state_mutex_);
	return device_id_;
}

bool MicrophoneAudioSource::SwitchDevice(const std::string& device_id) {
	if (device_id.empty()) {
		return false;
	}
	return Invoke([this, device_id] {
		std::string previous_id;
		{
			std::lock_guard<std::mutex> guard(state_mutex_);
			if (device_id == device_id_) {
				return true;
			}
			previous_id = device_id_;
		}
		const bool was_capturing = capturing_.load();
		StopOnControlThread();
		if (!was_capturing) {
			std::lock_guard<std::mutex> guard(state_mutex_);
			device_id_ = device_id;
			return true;
		}
		if (StartOnControlThread(device_id)) {
			return true;
		}
		StartOnControlThread(previous_id);
		return false;
	});
}

void MicrophoneAudioSource::SetMuted(bool muted) { muted_.store(muted); }

bool MicrophoneAudioSource::IsMuted() const { return muted_.load(); }

int32_t MicrophoneAudioSource::RecordedDataIsAvailable(const void* audio_samples,
                                                       size_t samples_per_channel,
                                                       size_t bytes_per_sample, size_t channels,
                                                       uint32_t sample_rate, uint32_t, int32_t,
                                                       uint32_t current_mic_level, bool,
                                                       uint32_t& new_mic_level) {
	new_mic_level = current_mic_level;
	if (audio_samples == nullptr || samples_per_channel == 0 ||
	    sample_rate != kMicrophoneSampleRate || (channels != 1 && channels != 2) ||
	    bytes_per_sample != sizeof(int16_t)) {
		return -1;
	}
	thread_local std::vector<int16_t> mono;
	if (muted_.load()) {
		mono.assign(samples_per_channel, 0);
	} else if (channels == 2) {
		const auto* stereo = static_cast<const int16_t*>(audio_samples);
		mono.resize(samples_per_channel);
		for (size_t index = 0; index < samples_per_channel; ++index) {
			mono[index] = static_cast<int16_t>(
			    (static_cast<int32_t>(stereo[index * 2]) + stereo[index * 2 + 1]) / 2);
		}
	} else {
		return AudioSource::CaptureFrame(const_cast<void*>(audio_samples), sample_rate, 1,
		                                 static_cast<uint32_t>(samples_per_channel))
		           ? 0
		           : -1;
	}
	return AudioSource::CaptureFrame(mono.data(), sample_rate, 1,
	                                 static_cast<uint32_t>(samples_per_channel))
	           ? 0
	           : -1;
}

int32_t MicrophoneAudioSource::NeedMorePlayData(size_t samples_per_channel, size_t bytes_per_sample,
                                                size_t, uint32_t, void* audio_samples,
                                                size_t& samples_out, int64_t* elapsed_time_ms,
                                                int64_t* ntp_time_ms) {
	if (audio_samples != nullptr) {
		std::memset(audio_samples, 0, samples_per_channel * bytes_per_sample);
	}
	samples_out = samples_per_channel;
	if (elapsed_time_ms != nullptr) {
		*elapsed_time_ms = 0;
	}
	if (ntp_time_ms != nullptr) {
		*ntp_time_ms = 0;
	}
	return 0;
}

void MicrophoneAudioSource::PullRenderData(int bits_per_sample, int, size_t channels,
                                           size_t frames_per_channel, void* audio_data,
                                           int64_t* elapsed_time_ms, int64_t* ntp_time_ms) {
	if (audio_data != nullptr && bits_per_sample > 0) {
		std::memset(audio_data, 0, frames_per_channel * channels * bits_per_sample / 8);
	}
	if (elapsed_time_ms != nullptr) {
		*elapsed_time_ms = 0;
	}
	if (ntp_time_ms != nullptr) {
		*ntp_time_ms = 0;
	}
}

MicrophoneAudioSourceInterface* CreateMicrophoneAudioSource(MicrophoneCaptureOptions options) {
	if (options.queue_size_ms == 0 || options.queue_size_ms % 10 != 0) {
		return nullptr;
	}
	auto source = std::make_unique<MicrophoneAudioSource>(std::move(options));
	if (!source->Start()) {
		return nullptr;
	}
	return source.release();
}

} // namespace livekit::core
