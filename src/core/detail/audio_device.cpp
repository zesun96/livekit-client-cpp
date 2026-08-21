/**
 *
 * Copyright (c) 2025 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "audio_device.h"

#include "../../capture/audio_capture_adapter.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
const int kSampleRate = 48000;
const int kChannels = 2;
const int kBytesPerSample = kChannels * sizeof(int16_t);
const int kSamplesPer10Ms = kSampleRate / 100;
} // namespace

namespace livekit {
namespace core {

AudioDevice::AudioDevice(webrtc::TaskQueueFactory* task_queue_factory)
    : task_queue_factory_(task_queue_factory), data_(kSamplesPer10Ms * kChannels),
      playback_(std::make_unique<capture::AudioPlaybackAdapter>()) {}

AudioDevice::~AudioDevice() { Terminate(); }

int32_t AudioDevice::ActiveAudioLayer(AudioLayer* audioLayer) const {
	*audioLayer = AudioLayer::kDummyAudio;
	return 0;
}

int32_t AudioDevice::RegisterAudioCallback(webrtc::AudioTransport* transport) {
	webrtc::MutexLock lock(&mutex_);
	audio_transport_ = transport;
	return 0;
}

void AudioDevice::DeliverRenderData(const std::int16_t* samples, std::uint32_t sample_rate,
                                    std::uint32_t channels,
                                    std::uint32_t frames_per_channel) noexcept {
	if (samples == nullptr || sample_rate == 0 || channels == 0 || frames_per_channel == 0) {
		return;
	}
	try {
		webrtc::MutexLock lock(&render_data_mutex_);
		render_data_.assign(samples, samples + frames_per_channel * channels);
		render_sample_rate_ = sample_rate;
		render_channels_ = channels;
		++render_sequence_;
	} catch (...) {
		// Decoded audio callbacks cannot propagate allocation failures into libwebrtc.
	}
}

bool AudioDevice::ReadRenderData(uint64_t& sequence, std::vector<std::int16_t>& samples,
                                 std::uint32_t& sample_rate, std::uint32_t& channels) const {
	webrtc::MutexLock lock(&render_data_mutex_);
	if (sequence == render_sequence_ || render_data_.empty()) {
		return false;
	}
	samples = render_data_;
	sample_rate = render_sample_rate_;
	channels = render_channels_;
	sequence = render_sequence_;
	return true;
}

int32_t AudioDevice::Init() {
	webrtc::MutexLock lock(&mutex_);
	if (initialized_)
		return 0;

	audio_queue_ = task_queue_factory_->CreateTaskQueue("AudioDevice",
	                                                    webrtc::TaskQueueFactory::Priority::NORMAL);

	audio_task_ = webrtc::RepeatingTaskHandle::Start(audio_queue_.get(), [this]() {
		webrtc::MutexLock lock(&mutex_);
		if (!playing_ || audio_transport_ == nullptr) {
			return webrtc::TimeDelta::Millis(10);
		}
		int64_t elapsed_time_ms = -1;
		int64_t ntp_time_ms = -1;
		size_t samples_out = 0;
		audio_transport_->NeedMorePlayData(kSamplesPer10Ms, kBytesPerSample, kChannels, kSampleRate,
		                                   data_.data(), samples_out, &elapsed_time_ms,
		                                   &ntp_time_ms);
		samples_out = std::min(samples_out, static_cast<std::size_t>(kSamplesPer10Ms));
		if (samples_out > 0 && playback_) {
			DeliverRenderData(data_.data(), kSampleRate, kChannels,
			                  static_cast<std::uint32_t>(samples_out));
			playback_->QueueFrame(data_.data(), kSampleRate, kChannels,
			                      static_cast<std::uint32_t>(samples_out));
		}

		return webrtc::TimeDelta::Millis(10);
	});

	initialized_ = true;
	return 0;
}

int32_t AudioDevice::Terminate() {
	{
		webrtc::MutexLock lock(&mutex_);
		if (!initialized_)
			return 0;

		initialized_ = false;
		playing_ = false;
		playout_initialized_ = false;
	}
	audio_task_.Stop();
	if (playback_) {
		playback_->Stop();
	}
	audio_queue_ = nullptr;
	return 0;
}

bool AudioDevice::Initialized() const {
	webrtc::MutexLock lock(&mutex_);
	return initialized_;
}

int16_t AudioDevice::PlayoutDevices() {
	const auto devices = capture::EnumerateAudioDevices();
	return static_cast<int16_t>(
	    std::count_if(devices.begin(), devices.end(), [](const auto& device) {
		    return device.kind == capture::AudioDeviceKind::Output;
	    }));
}

int16_t AudioDevice::RecordingDevices() { return 0; }

int32_t AudioDevice::PlayoutDeviceName(uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
                                       char guid[webrtc::kAdmMaxGuidSize]) {
	if (name == nullptr || guid == nullptr) {
		return -1;
	}
	const auto devices = capture::EnumerateAudioDevices();
	std::uint16_t output_index = 0;
	for (const auto& device : devices) {
		if (device.kind != capture::AudioDeviceKind::Output) {
			continue;
		}
		if (output_index++ != index) {
			continue;
		}
		std::strncpy(name, device.label.c_str(), webrtc::kAdmMaxDeviceNameSize - 1);
		name[webrtc::kAdmMaxDeviceNameSize - 1] = '\0';
		std::strncpy(guid, device.id.c_str(), webrtc::kAdmMaxGuidSize - 1);
		guid[webrtc::kAdmMaxGuidSize - 1] = '\0';
		return 0;
	}
	return -1;
}

int32_t AudioDevice::RecordingDeviceName(uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
                                         char guid[webrtc::kAdmMaxGuidSize]) {
	return 0;
}

int32_t AudioDevice::SetPlayoutDevice(uint16_t index) {
	const auto devices = capture::EnumerateAudioDevices();
	std::uint16_t output_index = 0;
	for (const auto& device : devices) {
		if (device.kind == capture::AudioDeviceKind::Output && output_index++ == index) {
			return SetPlayoutDeviceId(device.id) ? 0 : -1;
		}
	}
	return -1;
}

int32_t AudioDevice::SetPlayoutDevice(WindowsDeviceType device) {
	(void)device;
	const auto devices = capture::EnumerateAudioDevices();
	const auto selected = std::find_if(devices.begin(), devices.end(), [](const auto& item) {
		return item.kind == capture::AudioDeviceKind::Output && item.is_default;
	});
	if (selected != devices.end()) {
		return SetPlayoutDeviceId(selected->id) ? 0 : -1;
	}
	const auto first = std::find_if(devices.begin(), devices.end(), [](const auto& item) {
		return item.kind == capture::AudioDeviceKind::Output;
	});
	return first != devices.end() && SetPlayoutDeviceId(first->id) ? 0 : -1;
}

int32_t AudioDevice::SetRecordingDevice(uint16_t index) { return 0; }

int32_t AudioDevice::SetRecordingDevice(WindowsDeviceType device) { return 0; }

int32_t AudioDevice::PlayoutIsAvailable(bool* available) {
	if (available == nullptr) {
		return -1;
	}
	*available = PlayoutDevices() > 0;
	return 0;
}

int32_t AudioDevice::InitPlayout() {
	webrtc::MutexLock lock(&mutex_);
	playout_initialized_ = playback_ != nullptr;
	return playout_initialized_ ? 0 : -1;
}

bool AudioDevice::PlayoutIsInitialized() const {
	webrtc::MutexLock lock(&mutex_);
	return playout_initialized_;
}

int32_t AudioDevice::RecordingIsAvailable(bool* available) { return 0; }

int32_t AudioDevice::InitRecording() { return 0; }

bool AudioDevice::RecordingIsInitialized() const { return false; }

int32_t AudioDevice::StartPlayout() {
	webrtc::MutexLock lock(&mutex_);
	if (!playback_ || (!playback_->IsRunning() && !playback_->Start())) {
		return -1;
	}
	playout_initialized_ = true;
	playing_ = true;
	return 0;
}

int32_t AudioDevice::StopPlayout() {
	webrtc::MutexLock lock(&mutex_);
	playing_ = false;
	if (playback_) {
		playback_->Stop();
	}
	return 0;
}

bool AudioDevice::Playing() const {
	webrtc::MutexLock lock(&mutex_);
	return playing_;
}

int32_t AudioDevice::StartRecording() { return 0; }

int32_t AudioDevice::StopRecording() { return 0; }

bool AudioDevice::Recording() const { return false; }

int32_t AudioDevice::InitSpeaker() { return playback_ ? 0 : -1; }

bool AudioDevice::SpeakerIsInitialized() const { return playback_ != nullptr; }

int32_t AudioDevice::InitMicrophone() { return 0; }

bool AudioDevice::MicrophoneIsInitialized() const { return false; }

int32_t AudioDevice::SpeakerVolumeIsAvailable(bool* available) {
	if (available == nullptr) {
		return -1;
	}
	*available = playback_ != nullptr;
	return 0;
}

int32_t AudioDevice::SetSpeakerVolume(uint32_t volume) {
	return playback_ && volume <= 255 && playback_->SetVolume(static_cast<float>(volume) / 255.0F)
	           ? 0
	           : -1;
}

int32_t AudioDevice::SpeakerVolume(uint32_t* volume) const {
	if (volume == nullptr || !playback_) {
		return -1;
	}
	*volume = static_cast<std::uint32_t>(playback_->Volume() * 255.0F + 0.5F);
	return 0;
}

int32_t AudioDevice::MaxSpeakerVolume(uint32_t* maxVolume) const {
	if (maxVolume == nullptr) {
		return -1;
	}
	*maxVolume = 255;
	return 0;
}

int32_t AudioDevice::MinSpeakerVolume(uint32_t* minVolume) const {
	if (minVolume == nullptr) {
		return -1;
	}
	*minVolume = 0;
	return 0;
}

int32_t AudioDevice::MicrophoneVolumeIsAvailable(bool* available) { return 0; }

int32_t AudioDevice::SetMicrophoneVolume(uint32_t volume) { return 0; }

int32_t AudioDevice::MicrophoneVolume(uint32_t* volume) const { return 0; }

int32_t AudioDevice::MaxMicrophoneVolume(uint32_t* maxVolume) const { return 0; }

int32_t AudioDevice::MinMicrophoneVolume(uint32_t* minVolume) const { return 0; }

int32_t AudioDevice::SpeakerMuteIsAvailable(bool* available) {
	if (available == nullptr) {
		return -1;
	}
	*available = playback_ != nullptr;
	return 0;
}

int32_t AudioDevice::SetSpeakerMute(bool enable) {
	if (!playback_) {
		return -1;
	}
	playback_->SetMuted(enable);
	return 0;
}

int32_t AudioDevice::SpeakerMute(bool* enabled) const {
	if (enabled == nullptr || !playback_) {
		return -1;
	}
	*enabled = playback_->IsMuted();
	return 0;
}

int32_t AudioDevice::MicrophoneMuteIsAvailable(bool* available) { return 0; }

int32_t AudioDevice::SetMicrophoneMute(bool enable) { return 0; }

int32_t AudioDevice::MicrophoneMute(bool* enabled) const { return 0; }

int32_t AudioDevice::StereoPlayoutIsAvailable(bool* available) const {
	if (available == nullptr) {
		return -1;
	}
	*available = true;
	return 0;
}

int32_t AudioDevice::SetStereoPlayout(bool enable) { return enable ? 0 : -1; }

int32_t AudioDevice::StereoPlayout(bool* enabled) const {
	if (enabled == nullptr) {
		return -1;
	}
	*enabled = true;
	return 0;
}

int32_t AudioDevice::StereoRecordingIsAvailable(bool* available) const { return 0; }

int32_t AudioDevice::SetStereoRecording(bool enable) { return 0; }

int32_t AudioDevice::StereoRecording(bool* enabled) const {
	*enabled = true;
	return 0;
}

int32_t AudioDevice::PlayoutDelay(uint16_t* delayMS) const {
	if (delayMS == nullptr || !playback_) {
		return -1;
	}
	*delayMS = static_cast<std::uint16_t>(std::min<std::uint32_t>(
	    playback_->Stats().estimated_delay_ms, std::numeric_limits<std::uint16_t>::max()));
	return 0;
}

bool AudioDevice::SetPlayoutDeviceId(std::string_view device_id) {
	if (device_id.empty() || !playback_) {
		return false;
	}
	return playback_->SwitchDevice(device_id);
}

std::string AudioDevice::PlayoutDeviceId() const {
	return playback_ ? playback_->DeviceId() : std::string{};
}

capture::AudioPlaybackStats AudioDevice::PlaybackStats() const {
	return playback_ ? playback_->Stats() : capture::AudioPlaybackStats{};
}

std::string AudioDevice::PlayoutError() const {
	return playback_ ? playback_->LastError() : "audio playback is unavailable";
}

bool AudioDevice::BuiltInAECIsAvailable() const { return false; }

bool AudioDevice::BuiltInAGCIsAvailable() const { return false; }

bool AudioDevice::BuiltInNSIsAvailable() const { return false; }

int32_t AudioDevice::EnableBuiltInAEC(bool enable) { return 0; }

int32_t AudioDevice::EnableBuiltInAGC(bool enable) { return 0; }

int32_t AudioDevice::EnableBuiltInNS(bool enable) { return 0; }

#if defined(WEBRTC_IOS)
int AudioDevice::GetPlayoutAudioParameters(webrtc::AudioParameters* params) const { return 0; }

int AudioDevice::GetRecordAudioParameters(webrtc::AudioParameters* params) const { return 0; }
#endif // WEBRTC_IOS

} // namespace core
} // namespace livekit
