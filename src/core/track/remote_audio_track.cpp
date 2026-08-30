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

#include <algorithm>

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
    : RemoteTrack(std::move(sid), std::move(name), TrackKind::Audio, std::move(audio_track)),
      callback_(std::move(callback)) {
	sink_ = std::make_shared<AudioSink>(
	    std::make_unique<FrameSink>([this](const AudioFrame& frame) { OnFrame(frame); }), 48000, 1);
	static_cast<AudioTrack*>(media_track())->add_sink(sink_);
}

RemoteAudioTrack::~RemoteAudioTrack() {
	static_cast<AudioTrack*>(media_track())->remove_sink(sink_);
	CloseStreams();
}

std::shared_ptr<AudioStream> RemoteAudioTrack::CreateAudioStream(MediaStreamOptions options) {
	if (options.capacity == 0) {
		return nullptr;
	}
	auto stream = std::shared_ptr<AudioStream>(new AudioStream(options.capacity));
	std::lock_guard<std::mutex> guard(streams_mutex_);
	streams_.erase(std::remove_if(streams_.begin(), streams_.end(),
	                              [](const auto& weak) { return weak.expired(); }),
	               streams_.end());
	streams_.push_back(stream);
	return stream;
}

void RemoteAudioTrack::OnFrame(const AudioFrame& frame) {
	std::vector<std::shared_ptr<AudioStream>> streams;
	{
		std::lock_guard<std::mutex> guard(streams_mutex_);
		for (auto it = streams_.begin(); it != streams_.end();) {
			if (auto stream = it->lock()) {
				streams.push_back(std::move(stream));
				++it;
			} else {
				it = streams_.erase(it);
			}
		}
	}
	for (const auto& stream : streams) {
		stream->Push(frame);
	}
	if (callback_) {
		callback_(frame);
	}
}

void RemoteAudioTrack::CloseStreams() {
	std::vector<std::shared_ptr<AudioStream>> streams;
	{
		std::lock_guard<std::mutex> guard(streams_mutex_);
		for (const auto& weak : streams_) {
			if (auto stream = weak.lock()) {
				streams.push_back(std::move(stream));
			}
		}
		streams_.clear();
	}
	for (const auto& stream : streams) {
		stream->Close();
	}
}

} // namespace core
} // namespace livekit
