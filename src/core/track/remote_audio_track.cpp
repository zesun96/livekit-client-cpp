/**
 *
 * Copyright (c) 2024 sunze
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

#include "remote_audio_track.h"

namespace livekit {
namespace core {
namespace {

class FrameSink final : public AudioSinkWrapper {
public:
	explicit FrameSink(RemoteAudioTrack::FrameCallback callback) : callback_(std::move(callback)) {}

	void on_data(const void* audio_data, int bits_per_sample, int sample_rate,
	             size_t number_of_channels, size_t number_of_frames) override {
		if (bits_per_sample != 16 || audio_data == nullptr) {
			return;
		}
		AudioFrame frame;
		frame.sample_rate = static_cast<uint32_t>(sample_rate);
		frame.num_channels = static_cast<uint32_t>(number_of_channels);
		frame.samples_per_channel = static_cast<uint32_t>(number_of_frames);
		const auto* samples = static_cast<const int16_t*>(audio_data);
		frame.data.assign(samples, samples + number_of_channels * number_of_frames);
		callback_(frame);
	}

private:
	RemoteAudioTrack::FrameCallback callback_;
};

} // namespace

RemoteAudioTrack::RemoteAudioTrack(std::string sid, std::string name,
                                   std::unique_ptr<AudioTrack> audio_track, FrameCallback callback)
    : RemoteTrack(std::move(sid), std::move(name), TrackKind::Audio, std::move(audio_track)) {
	sink_ = std::make_shared<AudioSink>(std::make_unique<FrameSink>(std::move(callback)), 48000, 1);
	static_cast<AudioTrack*>(media_track())->add_sink(sink_);
}

} // namespace core
} // namespace livekit
