#include "preconnect_audio_buffer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace livekit::core::detail {

PreconnectAudioBuffer::PreconnectAudioBuffer(std::size_t maximum_bytes,
                                             std::chrono::steady_clock::duration timeout,
                                             std::function<void()> on_audio_available)
    : storage_(maximum_bytes), started_at_(std::chrono::steady_clock::now()), timeout_(timeout),
      on_audio_available_(std::move(on_audio_available)),
      recording_(maximum_bytes != 0 && timeout > std::chrono::steady_clock::duration::zero()) {}

void PreconnectAudioBuffer::on_data(const void* audio_data, int bits_per_sample, int sample_rate,
                                    size_t number_of_channels, size_t number_of_frames) {
	if (audio_data == nullptr || bits_per_sample != 16 || sample_rate <= 0 ||
	    number_of_channels == 0 || number_of_frames == 0 ||
	    number_of_channels > std::numeric_limits<uint32_t>::max() ||
	    number_of_frames > std::numeric_limits<std::size_t>::max() / number_of_channels) {
		return;
	}
	const auto samples = number_of_frames * number_of_channels;
	if (samples > std::numeric_limits<std::size_t>::max() / sizeof(int16_t)) {
		return;
	}

	bool notify = false;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!recording_ || ExpireIfNeeded(std::chrono::steady_clock::now())) {
			return;
		}
		if (sample_rate_ == 0) {
			sample_rate_ = static_cast<uint32_t>(sample_rate);
			channels_ = static_cast<uint32_t>(number_of_channels);
		} else if (sample_rate_ != static_cast<uint32_t>(sample_rate) ||
		           channels_ != static_cast<uint32_t>(number_of_channels)) {
			return;
		}
		notify = size_ == 0;
		Append(static_cast<const uint8_t*>(audio_data), samples * sizeof(int16_t));
	}
	if (notify && on_audio_available_) {
		on_audio_available_();
	}
}

std::optional<PreconnectAudioData> PreconnectAudioBuffer::Take() {
	std::lock_guard<std::mutex> guard(mutex_);
	if (!recording_ || ExpireIfNeeded(std::chrono::steady_clock::now()) || size_ == 0 ||
	    sample_rate_ == 0 || channels_ == 0) {
		recording_ = false;
		Clear();
		return std::nullopt;
	}

	PreconnectAudioData result;
	result.bytes.resize(size_);
	const auto first = std::min(size_, storage_.size() - head_);
	std::memcpy(result.bytes.data(), storage_.data() + head_, first);
	if (first < size_) {
		std::memcpy(result.bytes.data() + first, storage_.data(), size_ - first);
	}
	result.sample_rate = sample_rate_;
	result.channels = channels_;
	result.dropped_bytes = dropped_bytes_;
	recording_ = false;
	Clear();
	return result;
}

void PreconnectAudioBuffer::Discard() {
	std::lock_guard<std::mutex> guard(mutex_);
	recording_ = false;
	Clear();
}

bool PreconnectAudioBuffer::IsRecording() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return recording_ && std::chrono::steady_clock::now() - started_at_ < timeout_;
}

std::size_t PreconnectAudioBuffer::Size() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return size_;
}

bool PreconnectAudioBuffer::ExpireIfNeeded(std::chrono::steady_clock::time_point now) {
	if (now - started_at_ < timeout_) {
		return false;
	}
	recording_ = false;
	Clear();
	return true;
}

void PreconnectAudioBuffer::Append(const uint8_t* data, std::size_t size) {
	const auto capacity = storage_.size();
	if (size >= capacity) {
		dropped_bytes_ += size_ + size - capacity;
		std::memcpy(storage_.data(), data + size - capacity, capacity);
		head_ = 0;
		size_ = capacity;
		return;
	}
	if (size_ + size > capacity) {
		const auto overflow = size_ + size - capacity;
		head_ = (head_ + overflow) % capacity;
		size_ -= overflow;
		dropped_bytes_ += overflow;
	}
	const auto tail = (head_ + size_) % capacity;
	const auto first = std::min(size, capacity - tail);
	std::memcpy(storage_.data() + tail, data, first);
	if (first < size) {
		std::memcpy(storage_.data(), data + first, size - first);
	}
	size_ += size;
}

void PreconnectAudioBuffer::Clear() {
	head_ = 0;
	size_ = 0;
	sample_rate_ = 0;
	channels_ = 0;
}

} // namespace livekit::core::detail
