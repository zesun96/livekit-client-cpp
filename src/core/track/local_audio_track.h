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

#pragma once

#ifndef _LKC_CORE_TRACK_LOCAL_AUDIO_TRACK_H_
#define _LKC_CORE_TRACK_LOCAL_AUDIO_TRACK_H_

#include "../detail/preconnect_audio_buffer.h"
#include "audio_source.h"
#include "audio_track.h"
#include "local_track.h"

#include <functional>
#include <mutex>
#include <optional>

namespace livekit {
namespace core {

class LocalAudioTrack : public LocalTrack {
public:
	LocalAudioTrack(std::string name, std::unique_ptr<AudioTrack> audio_track,
	                AudioSourceInterface* source);
	~LocalAudioTrack() override;

	bool StartPreconnectBuffer(std::function<void()> on_audio_available = {});
	std::optional<detail::PreconnectAudioData> TakePreconnectBuffer();
	void DiscardPreconnectBuffer();
	bool HasPreconnectBuffer() const;
	std::size_t PreconnectBufferSize() const;

private:
	AudioSourceInterface* source_;
	mutable std::mutex preconnect_mutex_;
	std::shared_ptr<detail::PreconnectAudioBuffer> preconnect_buffer_;
	std::shared_ptr<AudioSink> preconnect_sink_;
};

} // namespace core
} // namespace livekit

#endif //
