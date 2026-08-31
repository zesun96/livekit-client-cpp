#include "tracing.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace livekit::core {
namespace {

class CapturingTraceSink final : public TraceSinkInterface {
public:
	void OnTrace(const TraceRecord& record) override {
		std::lock_guard<std::mutex> guard(mutex_);
		records_.push_back(record);
	}

	std::vector<TraceRecord> Records() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return records_;
	}

private:
	mutable std::mutex mutex_;
	std::vector<TraceRecord> records_;
};

class ThrowingTraceSink final : public TraceSinkInterface {
public:
	void OnTrace(const TraceRecord&) override { throw std::runtime_error("sink failure"); }
};

class BlockingTraceSink final : public TraceSinkInterface {
public:
	void OnTrace(const TraceRecord&) override {
		std::unique_lock<std::mutex> lock(mutex_);
		entered_ = true;
		changed_.notify_all();
		changed_.wait(lock, [this] { return released_; });
	}

	bool WaitUntilEntered() {
		std::unique_lock<std::mutex> lock(mutex_);
		return changed_.wait_for(lock, std::chrono::seconds(2), [this] { return entered_; });
	}

	void Release() {
		std::lock_guard<std::mutex> guard(mutex_);
		released_ = true;
		changed_.notify_all();
	}

private:
	std::mutex mutex_;
	std::condition_variable changed_;
	bool entered_ = false;
	bool released_ = false;
};

class TracingStateGuard {
public:
	TracingStateGuard() : sink_(GetTraceSink()), options_(GetTraceOptions()) {}
	~TracingStateGuard() {
		SetTraceOptions(options_);
		SetTraceSink(std::move(sink_));
	}

private:
	std::shared_ptr<TraceSinkInterface> sink_;
	TraceOptions options_;
};

TEST(TracingTest, FiltersCategoriesAndOwnsRecords) {
	TracingStateGuard restore;
	auto sink = std::make_shared<CapturingTraceSink>();
	SetTraceSink(sink);
	SetTraceOptions({true, static_cast<std::uint64_t>(TraceCategory::Lifecycle)});

	detail::EmitTrace(TraceCategory::Lifecycle, TracePhase::Instant, "runtime.ready");
	detail::EmitTrace(TraceCategory::Data, TracePhase::Instant, "filtered.data");
	SetTraceOptions({false, kAllTraceCategories});
	detail::EmitTrace(TraceCategory::Lifecycle, TracePhase::Instant, "filtered.disabled");

	const auto records = sink->Records();
	ASSERT_EQ(records.size(), 1u);
	EXPECT_EQ(records.front().phase, TracePhase::Instant);
	EXPECT_EQ(records.front().category, TraceCategory::Lifecycle);
	EXPECT_EQ(records.front().name, "runtime.ready");
	EXPECT_GT(records.front().thread_id, 0u);
	EXPECT_EQ(records.front().correlation_id, 0u);
}

TEST(TracingTest, EmitsPairedDurationsAndAsyncCorrelation) {
	TracingStateGuard restore;
	auto sink = std::make_shared<CapturingTraceSink>();
	SetTraceSink(sink);
	SetTraceOptions({true, kAllTraceCategories});
	{ detail::TraceSpan span(TraceCategory::Track, "track.publish"); }
	const auto correlation_id = detail::NextTraceCorrelationId();
	detail::EmitTrace(TraceCategory::Transport, TracePhase::AsyncBegin, "connection.recovery",
	                  correlation_id);
	detail::EmitTrace(TraceCategory::Transport, TracePhase::AsyncEnd, "connection.recovery",
	                  correlation_id);

	const auto records = sink->Records();
	ASSERT_EQ(records.size(), 4u);
	EXPECT_EQ(records[0].phase, TracePhase::DurationBegin);
	EXPECT_EQ(records[1].phase, TracePhase::DurationEnd);
	EXPECT_LE(records[0].timestamp_us, records[1].timestamp_us);
	EXPECT_EQ(records[2].phase, TracePhase::AsyncBegin);
	EXPECT_EQ(records[3].phase, TracePhase::AsyncEnd);
	EXPECT_NE(correlation_id, 0u);
	EXPECT_EQ(records[2].correlation_id, correlation_id);
	EXPECT_EQ(records[3].correlation_id, correlation_id);
}

TEST(TracingTest, ContainsSinkExceptions) {
	TracingStateGuard restore;
	SetTraceSink(std::make_shared<ThrowingTraceSink>());
	SetTraceOptions({true, kAllTraceCategories});
	EXPECT_NO_THROW(
	    detail::EmitTrace(TraceCategory::Lifecycle, TracePhase::Instant, "ignored.exception"));
}

TEST(TracingTest, ReplacingSinkWaitsForInProgressDelivery) {
	TracingStateGuard restore;
	auto sink = std::make_shared<BlockingTraceSink>();
	SetTraceSink(sink);
	SetTraceOptions({true, kAllTraceCategories});
	std::thread producer([] {
		detail::EmitTrace(TraceCategory::Lifecycle, TracePhase::Instant, "blocking.delivery");
	});
	const bool entered = sink->WaitUntilEntered();
	auto replacement = std::async(std::launch::async, [] { SetTraceSink(nullptr); });
	if (entered) {
		EXPECT_EQ(replacement.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
	}
	sink->Release();
	producer.join();
	EXPECT_TRUE(entered);
	EXPECT_EQ(replacement.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

TEST(TracingTest, WritesPerfettoCompatibleJson) {
	TracingStateGuard restore;
	const auto path =
	    std::filesystem::temp_directory_path() /
	    ("livekit-trace-" + std::to_string(detail::NextTraceCorrelationId()) + ".json");
	auto sink = CreateJsonTraceSink(path.string());
	ASSERT_NE(sink, nullptr);
	SetTraceSink(sink);
	SetTraceOptions({true, kAllTraceCategories});
	detail::EmitTrace(TraceCategory::Lifecycle, TracePhase::Instant, "runtime.\"ready");
	const auto correlation_id = detail::NextTraceCorrelationId();
	detail::EmitTrace(TraceCategory::Transport, TracePhase::AsyncBegin, "connection.recovery",
	                  correlation_id);
	detail::EmitTrace(TraceCategory::Transport, TracePhase::AsyncEnd, "connection.recovery",
	                  correlation_id);
	SetTraceSink(nullptr);
	sink.reset();

	std::ifstream input(path, std::ios::binary);
	ASSERT_TRUE(input);
	const std::string json((std::istreambuf_iterator<char>(input)),
	                       std::istreambuf_iterator<char>());
	EXPECT_TRUE(json.starts_with("{\"traceEvents\":["));
	EXPECT_TRUE(json.ends_with("],\"displayTimeUnit\":\"ms\"}\n"));
	EXPECT_NE(json.find("runtime.\\\"ready"), std::string::npos);
	EXPECT_NE(json.find("\"cat\":\"transport\""), std::string::npos);
	EXPECT_NE(json.find("\"ph\":\"b\""), std::string::npos);
	EXPECT_NE(json.find("\"ph\":\"e\""), std::string::npos);
	EXPECT_NE(json.find("\"id\":" + std::to_string(correlation_id)), std::string::npos);
	EXPECT_EQ(json.find("access_token"), std::string::npos);
	input.close();
	std::error_code error;
	std::filesystem::remove(path, error);
}

TEST(TracingTest, ProvidesStableNames) {
	EXPECT_STREQ(TraceCategoryName(TraceCategory::Signaling), "signaling");
	EXPECT_STREQ(TracePhaseName(TracePhase::DurationBegin), "B");
}

} // namespace
} // namespace livekit::core
