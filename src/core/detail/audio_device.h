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

#pragma once

#ifndef _LKC_CORE_DETAIL_AUDIO_DEVICE_H_
#define _LKC_CORE_DETAIL_AUDIO_DEVICE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_utils/repeating_task.h"

namespace livekit {
namespace capture {
class AudioPlaybackAdapter;
struct AudioPlaybackStats;
} // namespace capture
namespace core {

class AudioDevice : public webrtc::AudioDeviceModule {
public:
	AudioDevice(webrtc::TaskQueueFactory* task_queue_factory);
	~AudioDevice() override;

	int32_t ActiveAudioLayer(AudioLayer* audioLayer) const override;
	int32_t RegisterAudioCallback(webrtc::AudioTransport* transport) override;
	void DeliverRenderData(const std::int16_t* samples, std::uint32_t sample_rate,
	                       std::uint32_t channels, std::uint32_t frames_per_channel) noexcept;
	bool ReadRenderData(uint64_t& sequence, std::vector<std::int16_t>& samples,
	                    std::uint32_t& sample_rate, std::uint32_t& channels) const;
	bool SetPlayoutDeviceId(std::string_view device_id);
	std::string PlayoutDeviceId() const;
	capture::AudioPlaybackStats PlaybackStats() const;
	std::string PlayoutError() const;

	int32_t Init() override;
	int32_t Terminate() override;
	bool Initialized() const override;

	int16_t PlayoutDevices() override;
	int16_t RecordingDevices() override;
	int32_t PlayoutDeviceName(uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
	                          char guid[webrtc::kAdmMaxGuidSize]) override;

	int32_t RecordingDeviceName(uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
	                            char guid[webrtc::kAdmMaxGuidSize]) override;

	int32_t SetPlayoutDevice(uint16_t index) override;
	int32_t SetPlayoutDevice(WindowsDeviceType device) override;
	int32_t SetRecordingDevice(uint16_t index) override;
	int32_t SetRecordingDevice(WindowsDeviceType device) override;

	int32_t PlayoutIsAvailable(bool* available) override;
	int32_t InitPlayout() override;
	bool PlayoutIsInitialized() const override;
	int32_t RecordingIsAvailable(bool* available) override;
	int32_t InitRecording() override;
	bool RecordingIsInitialized() const override;

	int32_t StartPlayout() override;
	int32_t StopPlayout() override;
	bool Playing() const override;
	int32_t StartRecording() override;
	int32_t StopRecording() override;
	bool Recording() const override;

	int32_t InitSpeaker() override;
	bool SpeakerIsInitialized() const override;
	int32_t InitMicrophone() override;
	bool MicrophoneIsInitialized() const override;

	int32_t SpeakerVolumeIsAvailable(bool* available) override;
	int32_t SetSpeakerVolume(uint32_t volume) override;
	int32_t SpeakerVolume(uint32_t* volume) const override;
	int32_t MaxSpeakerVolume(uint32_t* maxVolume) const override;
	int32_t MinSpeakerVolume(uint32_t* minVolume) const override;

	int32_t MicrophoneVolumeIsAvailable(bool* available) override;
	int32_t SetMicrophoneVolume(uint32_t volume) override;
	int32_t MicrophoneVolume(uint32_t* volume) const override;
	int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override;
	int32_t MinMicrophoneVolume(uint32_t* minVolume) const override;

	int32_t SpeakerMuteIsAvailable(bool* available) override;
	int32_t SetSpeakerMute(bool enable) override;
	int32_t SpeakerMute(bool* enabled) const override;

	int32_t MicrophoneMuteIsAvailable(bool* available) override;
	int32_t SetMicrophoneMute(bool enable) override;
	int32_t MicrophoneMute(bool* enabled) const override;

	int32_t StereoPlayoutIsAvailable(bool* available) const override;
	int32_t SetStereoPlayout(bool enable) override;
	int32_t StereoPlayout(bool* enabled) const override;
	int32_t StereoRecordingIsAvailable(bool* available) const override;
	int32_t SetStereoRecording(bool enable) override;
	int32_t StereoRecording(bool* enabled) const override;

	int32_t PlayoutDelay(uint16_t* delayMS) const override;

	bool BuiltInAECIsAvailable() const override;
	bool BuiltInAGCIsAvailable() const override;
	bool BuiltInNSIsAvailable() const override;

	int32_t EnableBuiltInAEC(bool enable) override;
	int32_t EnableBuiltInAGC(bool enable) override;
	int32_t EnableBuiltInNS(bool enable) override;

#if defined(WEBRTC_IOS)
	int GetPlayoutAudioParameters(webrtc::AudioParameters* params) const override;
	int GetRecordAudioParameters(webrtc::AudioParameters* params) const override;
#endif // WEBRTC_IOS

private:
	mutable webrtc::Mutex mutex_;
	mutable webrtc::Mutex render_data_mutex_;
	std::vector<int16_t> data_;
	std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> audio_queue_;
	webrtc::RepeatingTaskHandle audio_task_;
	webrtc::AudioTransport* audio_transport_ RTC_GUARDED_BY(mutex_) = nullptr;
	std::vector<std::int16_t> render_data_ RTC_GUARDED_BY(render_data_mutex_);
	std::uint32_t render_sample_rate_ RTC_GUARDED_BY(render_data_mutex_) = 0;
	std::uint32_t render_channels_ RTC_GUARDED_BY(render_data_mutex_) = 0;
	uint64_t render_sequence_ RTC_GUARDED_BY(render_data_mutex_) = 0;
	webrtc::TaskQueueFactory* task_queue_factory_;
	bool playing_{false};
	bool initialized_{false};
	bool playout_initialized_{false};
	std::unique_ptr<capture::AudioPlaybackAdapter> playback_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_AUDIO_DEVICE_H_
