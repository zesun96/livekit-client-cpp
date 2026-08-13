#include "data_channel_backpressure.h"

#include <stdexcept>

namespace livekit {
namespace core {

DataChannelBackpressure::DataChannelBackpressure(uint64_t high_water_mark, uint64_t low_water_mark)
    : high_water_mark_(high_water_mark), low_water_mark_(low_water_mark) {
	if (high_water_mark == 0 || low_water_mark >= high_water_mark) {
		throw std::invalid_argument("data channel water marks must satisfy 0 <= low < high");
	}
}

DataChannelBufferTransition DataChannelBackpressure::Update(bool reliable,
                                                            uint64_t buffered_amount) {
	std::lock_guard<std::mutex> guard(mutex_);
	auto& backpressured = backpressured_[Index(reliable)];
	if (!backpressured && buffered_amount >= high_water_mark_) {
		backpressured = true;
		return {true, true};
	}
	if (backpressured && buffered_amount <= low_water_mark_) {
		backpressured = false;
		return {true, false};
	}
	return {false, backpressured};
}

bool DataChannelBackpressure::WaitUntilWritable(bool reliable,
                                                const std::function<uint64_t()>& buffered_amount,
                                                const std::function<bool()>& is_open,
                                                std::chrono::milliseconds timeout) {
	if (!buffered_amount || !is_open) {
		return false;
	}
	std::unique_lock<std::mutex> lock(mutex_);
	const auto generation = generation_;
	if (!is_open()) {
		return false;
	}
	if (buffered_amount() < high_water_mark_) {
		return true;
	}
	return cv_.wait_for(lock, timeout,
	                    [&] {
		                    return generation_ != generation || !is_open() ||
		                           buffered_amount() <= low_water_mark_;
	                    }) &&
	       generation_ == generation && is_open() && buffered_amount() <= low_water_mark_;
}

void DataChannelBackpressure::Notify() { cv_.notify_all(); }

void DataChannelBackpressure::Reset() {
	{
		std::lock_guard<std::mutex> guard(mutex_);
		backpressured_.fill(false);
		++generation_;
	}
	cv_.notify_all();
}

uint64_t DataChannelBackpressure::HighWaterMark() const { return high_water_mark_; }

uint64_t DataChannelBackpressure::LowWaterMark() const { return low_water_mark_; }

std::size_t DataChannelBackpressure::Index(bool reliable) const { return reliable ? 1u : 0u; }

} // namespace core
} // namespace livekit
