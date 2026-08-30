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

#include "local_audio_track.h"

#include "../detail/preconnect_audio_buffer.h"

#include <memory>

namespace livekit {
namespace core {
namespace {

class PreconnectAudioSink final : public AudioSinkWrapper {
public:
	explicit PreconnectAudioSink(std::shared_ptr<detail::PreconnectAudioBuffer> buffer)
	    : buffer_(std::move(buffer)) {}

	void on_data(const void* audio_data, int bits_per_sample, int sample_rate,
	             size_t number_of_channels, size_t number_of_frames) override {
		buffer_->on_data(audio_data, bits_per_sample, sample_rate, number_of_channels,
		                 number_of_frames);
	}

private:
	std::shared_ptr<detail::PreconnectAudioBuffer> buffer_;
};

} // namespace

LocalAudioTrack::LocalAudioTrack(std::string name, std::unique_ptr<AudioTrack> audio_track,
                                 AudioSourceInterface* source)
    : LocalTrack("TR_unknown", name, TrackKind::Audio, std::move(audio_track)), source_(source) {}

LocalAudioTrack::~LocalAudioTrack() { DiscardPreconnectBuffer(); }

bool LocalAudioTrack::StartPreconnectBuffer(std::function<void()> on_audio_available) {
	std::lock_guard<std::mutex> guard(preconnect_mutex_);
	if (preconnect_buffer_ != nullptr) {
		return true;
	}
	auto* audio_track = dynamic_cast<AudioTrack*>(media_track());
	if (audio_track == nullptr) {
		return false;
	}
	auto buffer = std::make_shared<detail::PreconnectAudioBuffer>(
	    detail::PreconnectAudioBuffer::kDefaultMaximumBytes,
	    detail::PreconnectAudioBuffer::kDefaultTimeout, std::move(on_audio_available));
	auto sink =
	    std::make_shared<AudioSink>(std::make_unique<PreconnectAudioSink>(buffer), 48000, 1);
	audio_track->add_sink(sink);
	preconnect_buffer_ = std::move(buffer);
	preconnect_sink_ = std::move(sink);
	return true;
}

std::optional<detail::PreconnectAudioData> LocalAudioTrack::TakePreconnectBuffer() {
	std::shared_ptr<detail::PreconnectAudioBuffer> buffer;
	std::shared_ptr<AudioSink> sink;
	{
		std::lock_guard<std::mutex> guard(preconnect_mutex_);
		buffer = std::move(preconnect_buffer_);
		sink = std::move(preconnect_sink_);
	}
	if (sink != nullptr) {
		if (auto* audio_track = dynamic_cast<AudioTrack*>(media_track())) {
			audio_track->remove_sink(sink);
		}
	}
	return buffer != nullptr ? buffer->Take() : std::nullopt;
}

void LocalAudioTrack::DiscardPreconnectBuffer() {
	std::shared_ptr<detail::PreconnectAudioBuffer> buffer;
	std::shared_ptr<AudioSink> sink;
	{
		std::lock_guard<std::mutex> guard(preconnect_mutex_);
		buffer = std::move(preconnect_buffer_);
		sink = std::move(preconnect_sink_);
	}
	if (sink != nullptr) {
		if (auto* audio_track = dynamic_cast<AudioTrack*>(media_track())) {
			audio_track->remove_sink(sink);
		}
	}
	if (buffer != nullptr) {
		buffer->Discard();
	}
}

bool LocalAudioTrack::HasPreconnectBuffer() const {
	std::lock_guard<std::mutex> guard(preconnect_mutex_);
	return preconnect_buffer_ != nullptr && preconnect_buffer_->IsRecording();
}

std::size_t LocalAudioTrack::PreconnectBufferSize() const {
	std::lock_guard<std::mutex> guard(preconnect_mutex_);
	return preconnect_buffer_ != nullptr ? preconnect_buffer_->Size() : 0;
}

} // namespace core
} // namespace livekit
