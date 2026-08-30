#pragma once

#ifndef LKC_CORE_DETAIL_PRECONNECT_AUDIO_BUFFER_H
#define LKC_CORE_DETAIL_PRECONNECT_AUDIO_BUFFER_H

#include "../track/audio_track.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace livekit::core::detail {

struct PreconnectAudioData {
	std::vector<uint8_t> bytes;
	uint32_t sample_rate = 0;
	uint32_t channels = 0;
	uint64_t dropped_bytes = 0;
};

// Receives normalized 16-bit PCM from an AudioTrack and retains only the newest bounded window.
class PreconnectAudioBuffer final : public AudioSinkWrapper {
public:
	static constexpr std::size_t kDefaultMaximumBytes = 10 * 1024 * 1024;
	static constexpr auto kDefaultTimeout = std::chrono::seconds(10);

	explicit PreconnectAudioBuffer(std::size_t maximum_bytes = kDefaultMaximumBytes,
	                               std::chrono::steady_clock::duration timeout = kDefaultTimeout,
	                               std::function<void()> on_audio_available = {});

	void on_data(const void* audio_data, int bits_per_sample, int sample_rate,
	             size_t number_of_channels, size_t number_of_frames) override;

	std::optional<PreconnectAudioData> Take();
	void Discard();
	bool IsRecording() const;
	std::size_t Size() const;

private:
	bool ExpireIfNeeded(std::chrono::steady_clock::time_point now);
	void Append(const uint8_t* data, std::size_t size);
	void Clear();

	mutable std::mutex mutex_;
	std::vector<uint8_t> storage_;
	std::size_t head_ = 0;
	std::size_t size_ = 0;
	uint64_t dropped_bytes_ = 0;
	uint32_t sample_rate_ = 0;
	uint32_t channels_ = 0;
	std::chrono::steady_clock::time_point started_at_;
	std::chrono::steady_clock::duration timeout_;
	std::function<void()> on_audio_available_;
	bool recording_ = true;
};

} // namespace livekit::core::detail

#endif // LKC_CORE_DETAIL_PRECONNECT_AUDIO_BUFFER_H
