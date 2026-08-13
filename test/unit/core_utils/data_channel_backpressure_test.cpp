#include "data_channel_backpressure.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace livekit::core {
namespace {

TEST(DataChannelBackpressureTest, ReportsHighAndLowWaterTransitionsOnce) {
	DataChannelBackpressure backpressure(100, 25);
	EXPECT_FALSE(backpressure.Update(true, 99).changed);
	auto high = backpressure.Update(true, 100);
	EXPECT_TRUE(high.changed);
	EXPECT_TRUE(high.backpressured);
	EXPECT_FALSE(backpressure.Update(true, 50).changed);
	auto low = backpressure.Update(true, 25);
	EXPECT_TRUE(low.changed);
	EXPECT_FALSE(low.backpressured);
	EXPECT_FALSE(backpressure.Update(true, 0).changed);

	// Reliable and lossy channels retain independent transition state.
	EXPECT_TRUE(backpressure.Update(false, 100).changed);
	EXPECT_TRUE(backpressure.Update(true, 100).backpressured);
}

TEST(DataChannelBackpressureTest, WaitsForDrainAndHonorsTimeout) {
	DataChannelBackpressure backpressure(100, 25);
	std::atomic<uint64_t> amount{100};
	std::atomic<bool> open{true};
	std::thread drain([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		amount = 20;
		backpressure.Notify();
	});
	EXPECT_TRUE(backpressure.WaitUntilWritable(
	    true, [&] { return amount.load(); }, [&] { return open.load(); },
	    std::chrono::milliseconds(100)));
	drain.join();

	amount = 100;
	EXPECT_FALSE(backpressure.WaitUntilWritable(
	    true, [&] { return amount.load(); }, [&] { return open.load(); },
	    std::chrono::milliseconds(1)));
}

TEST(DataChannelBackpressureTest, ResetCancelsWaitingSenderImmediately) {
	DataChannelBackpressure backpressure(100, 25);
	std::atomic<uint64_t> amount{100};
	std::atomic<bool> result{true};
	std::atomic<int> amount_checks{0};
	std::promise<void> waiting;
	auto waiting_future = waiting.get_future();
	std::thread sender([&] {
		result = backpressure.WaitUntilWritable(
		    true,
		    [&] {
			    if (++amount_checks == 2) {
				    waiting.set_value();
			    }
			    return amount.load();
		    },
		    [] { return true; }, std::chrono::seconds(5));
	});
	waiting_future.wait();
	backpressure.Reset();
	sender.join();
	EXPECT_FALSE(result.load());
}

} // namespace
} // namespace livekit::core
