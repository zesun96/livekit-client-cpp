#include "frame_queue.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace livekit::capture {
namespace {

using namespace std::chrono_literals;

TEST(FrameQueueTest, CopiesCallbackOwnedFrameData) {
	std::mutex mutex;
	std::condition_variable condition;
	std::vector<std::uint8_t> received;
	LatestVideoFrameQueue queue([&](const OwnedBgraFrame& frame) {
		{
			std::lock_guard<std::mutex> guard(mutex);
			received = frame.data;
		}
		condition.notify_all();
	});
	ASSERT_TRUE(queue.Start());
	std::array<std::uint8_t, 8> pixels{1, 2, 3, 4, 5, 6, 7, 8};
	ASSERT_TRUE(queue.Push(pixels.data(), 2, 1, 8, 10));
	pixels.fill(0);
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 1s, [&] { return !received.empty(); }));
	}
	queue.Stop();
	EXPECT_EQ(received, (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(FrameQueueTest, KeepsOnlyNewestPendingFrame) {
	std::mutex mutex;
	std::condition_variable condition;
	std::condition_variable release_first;
	std::vector<std::int64_t> timestamps;
	bool first_entered = false;
	bool may_finish_first = false;
	LatestVideoFrameQueue queue([&](const OwnedBgraFrame& frame) {
		std::unique_lock<std::mutex> lock(mutex);
		timestamps.push_back(frame.timestamp_us);
		if (timestamps.size() == 1) {
			first_entered = true;
			condition.notify_all();
			release_first.wait(lock, [&] { return may_finish_first; });
		} else {
			condition.notify_all();
		}
	});
	ASSERT_TRUE(queue.Start());
	const std::array<std::uint8_t, 4> pixel{1, 2, 3, 4};
	ASSERT_TRUE(queue.Push(pixel.data(), 1, 1, 4, 1));
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 1s, [&] { return first_entered; }));
	}
	ASSERT_TRUE(queue.Push(pixel.data(), 1, 1, 4, 2));
	ASSERT_TRUE(queue.Push(pixel.data(), 1, 1, 4, 3));
	{
		std::lock_guard<std::mutex> guard(mutex);
		may_finish_first = true;
	}
	release_first.notify_all();
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 1s, [&] { return timestamps.size() == 2; }));
	}
	queue.Stop();
	EXPECT_EQ(timestamps, (std::vector<std::int64_t>{1, 3}));
}

TEST(FrameQueueTest, StopsDeterministicallyAndRejectsInvalidFrames) {
	LatestVideoFrameQueue queue([](const OwnedBgraFrame&) {});
	const std::array<std::uint8_t, 4> pixel{};
	EXPECT_FALSE(queue.Push(pixel.data(), 1, 1, 4, 1));
	ASSERT_TRUE(queue.Start());
	EXPECT_TRUE(queue.IsRunning());
	EXPECT_FALSE(queue.Push(nullptr, 1, 1, 4, 1));
	EXPECT_FALSE(queue.Push(pixel.data(), 2, 1, 4, 1));
	queue.Stop();
	queue.Stop();
	EXPECT_FALSE(queue.IsRunning());
	EXPECT_FALSE(queue.Push(pixel.data(), 1, 1, 4, 1));
	EXPECT_TRUE(queue.Start());
	queue.Stop();
}

} // namespace
} // namespace livekit::capture
