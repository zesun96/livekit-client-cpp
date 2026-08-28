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

TEST(LoggingTest, RedactsCredentialsAndConnectionDescriptions) {
	LoggingStateGuard restore;
	auto sink = std::make_shared<CapturingLogSink>();
	SetLogSink(sink);
	LogOptions options;
	options.livekit_level = LogLevel::Trace;
	options.webrtc_level = LogLevel::Trace;
	options.websocket_level = LogLevel::Trace;
	SetLogOptions(options);

	detail::EmitLog(LogLevel::Info, LogSource::LiveKit,
	                "url=wss://host/rtc?access_token=secret&auto_subscribe=1");
	detail::EmitLog(LogLevel::Info, LogSource::LiveKit, "Authorization: Bearer secret");
	detail::EmitLog(LogLevel::Info, LogSource::WebRTC,
	                "candidate:1 1 UDP 1 192.0.2.1 5000 typ host");
	detail::EmitLog(LogLevel::Warning, LogSource::WebRTC,
	                "Candidate has an unknown component: "
	                "Cand[:123:1:udp:192.0.2.2:5001:host:secret-ufrag:secret-password]");
	detail::EmitLog(LogLevel::Warning, LogSource::WebRTC,
	                "Conn[id:host:udp:192.0.2.3:5002->secret-connection]");
	detail::EmitLog(LogLevel::Error, LogSource::WebRTC,
	                "Port[id:host:udp:192.0.2.4:5003]");
	detail::EmitLog(LogLevel::Info, LogSource::WebRTC,
	                "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=candidate:secret\r\n");
	detail::EmitLog(LogLevel::Info, LogSource::WebSocket, "token eyJabc.def.ghi");

	EXPECT_FALSE(sink->Contains(LogSource::LiveKit, LogLevel::Info, "secret"));
	EXPECT_FALSE(sink->Contains(LogSource::WebRTC, LogLevel::Info, "192.0.2.1"));
	EXPECT_FALSE(sink->Contains(LogSource::WebRTC, LogLevel::Warning, "secret-ufrag"));
	EXPECT_FALSE(sink->Contains(LogSource::WebRTC, LogLevel::Warning, "secret-connection"));
	EXPECT_FALSE(sink->Contains(LogSource::WebRTC, LogLevel::Error, "192.0.2.4"));
	EXPECT_FALSE(sink->Contains(LogSource::WebRTC, LogLevel::Info, "m=audio"));
	EXPECT_FALSE(sink->Contains(LogSource::WebSocket, LogLevel::Info, "eyJabc.def.ghi"));
	EXPECT_TRUE(sink->Contains(LogSource::WebRTC, LogLevel::Info, "ICE details redacted"));
	EXPECT_TRUE(sink->Contains(LogSource::WebRTC, LogLevel::Info, "SDP redacted"));
}

} // namespace
} // namespace livekit::core
