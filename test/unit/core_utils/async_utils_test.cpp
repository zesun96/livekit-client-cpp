#include "debouncer.h"
#include "timer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace livekit::core {
namespace {
using namespace std::chrono_literals;

TEST(DebouncerTest, SuppressesCallsDuringInterval) {
	auto debouncer = Debouncer::Create(20ms);
	ASSERT_TRUE(debouncer->lock());
	EXPECT_FALSE(debouncer->lock());
	std::this_thread::sleep_for(30ms);
	EXPECT_TRUE(debouncer->lock());
}

TEST(DebouncerTest, RejectsNegativeInterval) {
	EXPECT_THROW((void)Debouncer::Create(-1ms), std::invalid_argument);
}

TEST(TimerTest, FiresOneShotOnce) {
	auto timer = std::make_shared<Timer>();
	std::promise<void> fired;
	auto result = fired.get_future();
	std::atomic<int> calls{0};
	timer->SetTimeout(
	    [&]() {
		    ++calls;
		    fired.set_value();
	    },
	    10);

	ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
	timer->Stop();
	EXPECT_EQ(calls.load(), 1);
}

TEST(TimerTest, StopCancelsPendingCallback) {
	auto timer = std::make_shared<Timer>();
	std::atomic<int> calls{0};
	timer->SetTimeout([&]() { ++calls; }, 100);
	timer->Stop();
	std::this_thread::sleep_for(150ms);
	EXPECT_EQ(calls.load(), 0);
}

TEST(TimerTest, IntervalStopsWithoutFurtherCallbacks) {
	auto timer = std::make_shared<Timer>();
	std::promise<void> fired_twice;
	auto result = fired_twice.get_future();
	std::atomic<int> calls{0};
	timer->SetInterval(
	    [&]() {
		    if (++calls == 2) {
			    fired_twice.set_value();
		    }
	    },
	    10);

	ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
	timer->Stop();
	const int stopped_at = calls.load();
	std::this_thread::sleep_for(30ms);
	EXPECT_EQ(calls.load(), stopped_at);
}

TEST(TimerTest, RejectsNegativeDelay) {
	auto timer = std::make_shared<Timer>();
	EXPECT_THROW(timer->SetTimeout([] {}, -1), std::invalid_argument);
}

} // namespace
} // namespace livekit::core
