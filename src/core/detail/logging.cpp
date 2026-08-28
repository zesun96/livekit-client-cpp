/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "logging.h"

#include <libwebsockets.h>
#include <rtc_base/logging.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string_view>
#include <utility>

namespace livekit::core {
namespace {

struct LoggingState {
	std::mutex mutex;
	std::shared_ptr<LogSinkInterface> sink;
	LogOptions options;
};

LoggingState& State() {
	static LoggingState state;
	return state;
}

LogLevel ThresholdFor(const LogOptions& options, LogSource source) {
	switch (source) {
	case LogSource::LiveKit:
		return options.livekit_level;
	case LogSource::WebRTC:
		return options.webrtc_level;
	case LogSource::WebSocket:
		return options.websocket_level;
	}
	return LogLevel::Off;
}

std::string LowerAscii(std::string_view value) {
	std::string result(value);
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return result;
}

void RedactLineFromMarker(std::string& message, std::string_view marker,
                          std::string_view replacement) {
	for (size_t search_from = 0;;) {
		const auto lower = LowerAscii(message);
		const auto marker_position = lower.find(marker, search_from);
		if (marker_position == std::string::npos) {
			return;
		}
		const auto line_end = message.find_first_of("\r\n", marker_position);
		const auto replaced_size =
		    (line_end == std::string::npos ? message.size() : line_end) - marker_position;
		message.replace(marker_position, replaced_size, replacement);
		search_from = marker_position + replacement.size();
	}
}

void RedactJwtLikeValues(std::string& message) {
	for (size_t search_from = 0;;) {
		const auto start = message.find("eyJ", search_from);
		if (start == std::string::npos) {
			return;
		}
		auto end = start;
		size_t dots = 0;
		while (end < message.size()) {
			const auto character = static_cast<unsigned char>(message[end]);
			if (!(std::isalnum(character) != 0 || character == '_' || character == '-' ||
			      character == '.')) {
				break;
			}
			if (character == '.') {
				++dots;
			}
			++end;
		}
		if (dots >= 2) {
			constexpr std::string_view replacement = "[credential redacted]";
			message.replace(start, end - start, replacement);
			search_from = start + replacement.size();
		} else {
			search_from = end;
		}
	}
}

std::string SanitizeLogMessage(std::string message) {
	const auto lower = LowerAscii(message);
	if (lower.starts_with("v=0\r\n") || lower.starts_with("v=0\n") ||
	    lower.find("\r\na=candidate:") != std::string::npos ||
	    lower.find("\na=candidate:") != std::string::npos) {
		return "[SDP redacted]";
	}

	RedactLineFromMarker(message, "access_token=", "[credential redacted]");
	RedactLineFromMarker(message, "authorization:", "[credential redacted]");
	RedactLineFromMarker(message, "candidate:", "[ICE details redacted]");
	RedactLineFromMarker(message, "cand[", "[ICE details redacted]");
	RedactLineFromMarker(message, "conn[", "[ICE details redacted]");
	RedactLineFromMarker(message, "port[", "[ICE details redacted]");
	RedactLineFromMarker(message, "turn:", "[TURN URL redacted]");
	RedactLineFromMarker(message, "turns:", "[TURN URL redacted]");
	RedactJwtLikeValues(message);
	return message;
}

} // namespace

void SetLogSink(std::shared_ptr<LogSinkInterface> sink) {
	auto& state = State();
	std::lock_guard<std::mutex> guard(state.mutex);
	state.sink = std::move(sink);
}

std::shared_ptr<LogSinkInterface> GetLogSink() {
	auto& state = State();
	std::lock_guard<std::mutex> guard(state.mutex);
	return state.sink;
}

void SetLogOptions(const LogOptions& options) {
	{
		auto& state = State();
		std::lock_guard<std::mutex> guard(state.mutex);
		state.options = options;
	}
	detail::RefreshBackendLogging();
}

LogOptions GetLogOptions() {
	auto& state = State();
	std::lock_guard<std::mutex> guard(state.mutex);
	return state.options;
}

const char* LogLevelName(LogLevel level) noexcept {
	switch (level) {
	case LogLevel::Trace:
		return "trace";
	case LogLevel::Debug:
		return "debug";
	case LogLevel::Info:
		return "info";
	case LogLevel::Warning:
		return "warning";
	case LogLevel::Error:
		return "error";
	case LogLevel::Off:
		return "off";
	}
	return "unknown";
}

const char* LogSourceName(LogSource source) noexcept {
	switch (source) {
	case LogSource::LiveKit:
		return "livekit";
	case LogSource::WebRTC:
		return "webrtc";
	case LogSource::WebSocket:
		return "websocket";
	}
	return "unknown";
}

namespace detail {
namespace {

std::string TrimLine(std::string message) {
	while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
		message.pop_back();
	}
	return message;
}

webrtc::LoggingSeverity ToWebRtcSeverity(LogLevel level) {
	switch (level) {
	case LogLevel::Trace:
		return webrtc::LS_VERBOSE;
	case LogLevel::Debug:
	case LogLevel::Info:
		return webrtc::LS_INFO;
	case LogLevel::Warning:
		return webrtc::LS_WARNING;
	case LogLevel::Error:
		return webrtc::LS_ERROR;
	case LogLevel::Off:
		return webrtc::LS_NONE;
	}
	return webrtc::LS_NONE;
}

LogLevel FromWebRtcSeverity(webrtc::LoggingSeverity severity) {
	switch (severity) {
	case webrtc::LS_VERBOSE:
		return LogLevel::Trace;
	case webrtc::LS_INFO:
		return LogLevel::Info;
	case webrtc::LS_WARNING:
		return LogLevel::Warning;
	case webrtc::LS_ERROR:
		return LogLevel::Error;
	case webrtc::LS_NONE:
		return LogLevel::Off;
	}
	return LogLevel::Debug;
}

LogLevel FromWebSocketLevel(int level) {
	if ((level & LLL_ERR) != 0) {
		return LogLevel::Error;
	}
	if ((level & LLL_WARN) != 0) {
		return LogLevel::Warning;
	}
	if ((level & (LLL_NOTICE | LLL_INFO | LLL_USER)) != 0) {
		return LogLevel::Info;
	}
	return LogLevel::Debug;
}

int ToWebSocketMask(LogLevel level) {
	switch (level) {
	case LogLevel::Trace:
	case LogLevel::Debug:
		return LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG | LLL_PARSER | LLL_HEADER |
		       LLL_EXT | LLL_CLIENT | LLL_LATENCY | LLL_USER | LLL_THREAD;
	case LogLevel::Info:
		return LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_USER;
	case LogLevel::Warning:
		return LLL_ERR | LLL_WARN;
	case LogLevel::Error:
		return LLL_ERR;
	case LogLevel::Off:
		return 0;
	}
	return 0;
}

class WebRtcLogSink final : public webrtc::LogSink {
public:
	void OnLogMessage(const std::string& message) override {
		EmitLog(LogLevel::Info, LogSource::WebRTC, TrimLine(message));
	}

	void OnLogMessage(const std::string& message, webrtc::LoggingSeverity severity) override {
		EmitLog(FromWebRtcSeverity(severity), LogSource::WebRTC, TrimLine(message));
	}

	void OnLogMessage(const webrtc::LogLineRef& line) override {
		EmitLog(FromWebRtcSeverity(line.severity()), LogSource::WebRTC,
		        TrimLine(std::string(line.message())), std::string(line.filename()).c_str(),
		        line.line());
	}
};

struct BackendState {
	std::mutex mutex;
	bool active = false;
	WebRtcLogSink webrtc_sink;
};

BackendState& Backends() {
	static BackendState state;
	return state;
}

void WebSocketLogCallback(int level, const char* line) {
	EmitLog(FromWebSocketLevel(level), LogSource::WebSocket,
	        TrimLine(line == nullptr ? std::string{} : std::string(line)));
}

} // namespace

bool IsLogEnabled(LogLevel level, LogSource source) noexcept {
	try {
		if (level == LogLevel::Off) {
			return false;
		}
		auto& state = State();
		std::lock_guard<std::mutex> guard(state.mutex);
		return state.sink != nullptr && level >= ThresholdFor(state.options, source);
	} catch (...) {
		return false;
	}
}

void EmitLog(LogLevel level, LogSource source, std::string message, const char* file,
             int line) noexcept {
	try {
		std::shared_ptr<LogSinkInterface> sink;
		{
			auto& state = State();
			std::lock_guard<std::mutex> guard(state.mutex);
			if (level == LogLevel::Off || level < ThresholdFor(state.options, source)) {
				return;
			}
			sink = state.sink;
		}
		if (!sink) {
			return;
		}
		sink->OnLog({level, source, SanitizeLogMessage(std::move(message)),
		             file == nullptr ? "" : file, line});
	} catch (...) {
		// Logging must never change SDK control flow.
	}
}

void StartBackendLogging() {
	auto& backends = Backends();
	std::lock_guard<std::mutex> guard(backends.mutex);
	const auto options = GetLogOptions();
	if (backends.active) {
		webrtc::LogMessage::RemoveLogToStream(&backends.webrtc_sink);
	} else {
		backends.active = true;
	}
	webrtc::LogMessage::AddLogToStream(&backends.webrtc_sink,
	                                   ToWebRtcSeverity(options.webrtc_level));
	webrtc::LogMessage::LogToDebug(webrtc::LS_NONE);
	lws_set_log_level(ToWebSocketMask(options.websocket_level), WebSocketLogCallback);
}

void RefreshBackendLogging() {
	auto& backends = Backends();
	std::lock_guard<std::mutex> guard(backends.mutex);
	if (!backends.active) {
		return;
	}
	const auto options = GetLogOptions();
	webrtc::LogMessage::RemoveLogToStream(&backends.webrtc_sink);
	webrtc::LogMessage::AddLogToStream(&backends.webrtc_sink,
	                                   ToWebRtcSeverity(options.webrtc_level));
	lws_set_log_level(ToWebSocketMask(options.websocket_level), WebSocketLogCallback);
}

void StopBackendLogging() {
	auto& backends = Backends();
	std::lock_guard<std::mutex> guard(backends.mutex);
	if (!backends.active) {
		return;
	}
	webrtc::LogMessage::RemoveLogToStream(&backends.webrtc_sink);
	backends.active = false;
	lws_set_log_level(0, WebSocketLogCallback);
}

LogMessage::LogMessage(LogLevel level, LogSource source, const char* file, int line)
    : level_(level), source_(source), file_(file), line_(line) {}

LogMessage::~LogMessage() noexcept {
	try {
		EmitLog(level_, source_, stream_.str(), file_, line_);
	} catch (...) {
		// Logging must never change SDK control flow.
	}
}

} // namespace detail
} // namespace livekit::core
