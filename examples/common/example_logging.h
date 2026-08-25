#pragma once

#include "livekit/core/logging.h"

#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace livekit::examples {
namespace detail {

inline plog::Severity ToPlogSeverity(core::LogLevel level) {
	switch (level) {
	case core::LogLevel::Trace:
		return plog::verbose;
	case core::LogLevel::Debug:
		return plog::debug;
	case core::LogLevel::Info:
		return plog::info;
	case core::LogLevel::Warning:
		return plog::warning;
	case core::LogLevel::Error:
		return plog::error;
	case core::LogLevel::Off:
		return plog::none;
	}
	return plog::none;
}

inline core::LogLevel ReadLogLevel() {
	const char* value = std::getenv("LIVEKIT_LOG_LEVEL");
	if (value == nullptr) {
		return core::LogLevel::Info;
	}
	std::string normalized(value);
	std::transform(normalized.begin(), normalized.end(), normalized.begin(),
	               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	if (normalized == "trace") {
		return core::LogLevel::Trace;
	}
	if (normalized == "debug") {
		return core::LogLevel::Debug;
	}
	if (normalized == "warning" || normalized == "warn") {
		return core::LogLevel::Warning;
	}
	if (normalized == "error") {
		return core::LogLevel::Error;
	}
	if (normalized == "off" || normalized == "none") {
		return core::LogLevel::Off;
	}
	return core::LogLevel::Info;
}

inline core::LogOptions MakeLogOptions(core::LogLevel level) {
	core::LogOptions options;
	options.livekit_level = level;
	options.websocket_level = level;
	options.webrtc_level = level;
	if (level == core::LogLevel::Info) {
		options.webrtc_level = core::LogLevel::Warning;
	} else if (level == core::LogLevel::Debug) {
		options.webrtc_level = core::LogLevel::Info;
	}
	return options;
}

inline void InitializePlog() {
	static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
	static const bool initialized = [] {
		plog::init(plog::verbose, &appender);
		return true;
	}();
	(void)initialized;
}

} // namespace detail

class PlogLogSink final : public core::LogSinkInterface {
public:
	void OnLog(const core::LogRecord& record) override {
		PLOG(detail::ToPlogSeverity(record.level))
		    << '[' << core::LogSourceName(record.source) << "] " << record.message;
	}
};

inline std::shared_ptr<core::LogSinkInterface> ConfigureExampleLogging() {
	detail::InitializePlog();
	auto sink = std::make_shared<PlogLogSink>();
	core::SetLogOptions(detail::MakeLogOptions(detail::ReadLogLevel()));
	core::SetLogSink(sink);
	return sink;
}

} // namespace livekit::examples
