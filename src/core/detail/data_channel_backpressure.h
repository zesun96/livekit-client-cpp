#pragma once

#ifndef _LKC_CORE_DETAIL_DATA_CHANNEL_BACKPRESSURE_H_
#define _LKC_CORE_DETAIL_DATA_CHANNEL_BACKPRESSURE_H_

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace livekit {
namespace core {

struct DataChannelBufferTransition {
	bool changed = false;
	bool backpressured = false;
};

class DataChannelBackpressure {
public:
	DataChannelBackpressure(uint64_t high_water_mark, uint64_t low_water_mark);

	DataChannelBufferTransition Update(bool reliable, uint64_t buffered_amount);
	bool WaitUntilWritable(bool reliable, const std::function<uint64_t()>& buffered_amount,
	                       const std::function<bool()>& is_open, std::chrono::milliseconds timeout);
	void Notify();
	void Reset();
	uint64_t HighWaterMark() const;
	uint64_t LowWaterMark() const;

private:
	std::size_t Index(bool reliable) const;

	const uint64_t high_water_mark_;
	const uint64_t low_water_mark_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::array<bool, 2> backpressured_{};
	uint64_t generation_ = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_DATA_CHANNEL_BACKPRESSURE_H_
