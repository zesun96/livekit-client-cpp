#include "logging.h"

#include "livekit/core/livekit_client.h"

#include "rtc_base/logging.h"

#include <gtest/gtest.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace livekit::core {
namespace {

class CapturingLogSink final : public LogSinkInterface {
public:
	void OnLog(const LogRecord& record) override {
		std::lock_guard<std::mutex> guard(mutex_);
		records_.push_back(record);
	}

	bool Contains(LogSource source, LogLevel level, const std::string& text) const {
		std::lock_guard<std::mutex> guard(mutex_);
		for (const auto& record : records_) {
			if (record.source == source && record.level == level &&
			    record.message.find(text) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

private:
	mutable std::mutex mutex_;
	std::vector<LogRecord> records_;
};

class ThrowingLogSink final : public LogSinkInterface {
public:
	void OnLog(const LogRecord&) override { throw std::runtime_error("sink failure"); }
};

class LoggingStateGuard {
public:
	LoggingStateGuard() : sink_(GetLogSink()), options_(GetLogOptions()) {}
	~LoggingStateGuard() {
		SetLogOptions(options_);
		SetLogSink(std::move(sink_));
	}

private:
	std::shared_ptr<LogSinkInterface> sink_;
	LogOptions options_;
};

TEST(LoggingTest, FiltersOwnedSdkRecordsBySourceThreshold) {
	LoggingStateGuard restore;
	auto sink = std::make_shared<CapturingLogSink>();
	SetLogSink(sink);
	LogOptions options;
	options.livekit_level = LogLevel::Warning;
	SetLogOptions(options);

	detail::EmitLog(LogLevel::Info, LogSource::LiveKit, "filtered");
	detail::EmitLog(LogLevel::Warning, LogSource::LiveKit, "retained", "logging_test.cpp", 42);

	EXPECT_FALSE(sink->Contains(LogSource::LiveKit, LogLevel::Info, "filtered"));
	EXPECT_TRUE(sink->Contains(LogSource::LiveKit, LogLevel::Warning, "retained"));
}

TEST(LoggingTest, BridgesWebRtcLogsAndContainsSinkExceptions) {
	LoggingStateGuard restore;
	auto sink = std::make_shared<CapturingLogSink>();
	SetLogSink(sink);
	LogOptions options;
	options.webrtc_level = LogLevel::Warning;
	SetLogOptions(options);
	ASSERT_TRUE(Init());

	RTC_LOG(LS_WARNING) << "logging bridge sentinel";
	EXPECT_TRUE(sink->Contains(LogSource::WebRTC, LogLevel::Warning, "logging bridge sentinel"));
	options.webrtc_level = LogLevel::Error;
	SetLogOptions(options);
	RTC_LOG(LS_WARNING) << "filtered WebRTC sentinel";
	EXPECT_FALSE(sink->Contains(LogSource::WebRTC, LogLevel::Warning, "filtered WebRTC sentinel"));

	SetLogSink(std::make_shared<ThrowingLogSink>());
	EXPECT_NO_THROW(detail::EmitLog(LogLevel::Error, LogSource::LiveKit, "ignored exception"));
	EXPECT_TRUE(Destroy());
}

TEST(LoggingTest, ProvidesStableNames) {
	EXPECT_STREQ(LogLevelName(LogLevel::Debug), "debug");
	EXPECT_STREQ(LogSourceName(LogSource::WebSocket), "websocket");
}

} // namespace
} // namespace livekit::core
