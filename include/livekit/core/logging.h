/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_LOGGING_H_
#define _LKC_CORE_LOGGING_H_

#include <memory>
#include <string>

namespace livekit::core {

enum class LogLevel {
	Trace = 0,
	Debug,
	Info,
	Warning,
	Error,
	Off,
};

enum class LogSource {
	LiveKit,
	WebRTC,
	WebSocket,
};

struct LogRecord {
	LogLevel level = LogLevel::Info;
	LogSource source = LogSource::LiveKit;
	std::string message;
	std::string file;
	int line = 0;
};

/**
 * Receives synchronous log callbacks from SDK, WebRTC, and WebSocket threads.
 * Implementations must be thread-safe and should return quickly. Exceptions are ignored by the SDK.
 * Do not call SetLogOptions(), Init(), or Destroy() from inside OnLog().
 */
class LogSinkInterface {
public:
	virtual ~LogSinkInterface() = default;
	virtual void OnLog(const LogRecord& record) = 0;
};

struct LogOptions {
	LogLevel livekit_level = LogLevel::Info;
	LogLevel webrtc_level = LogLevel::Warning;
	LogLevel websocket_level = LogLevel::Warning;
};

/** Replaces the process-wide sink. Passing nullptr disables application log delivery. */
void SetLogSink(std::shared_ptr<LogSinkInterface> sink);
std::shared_ptr<LogSinkInterface> GetLogSink();

/** Updates source-specific thresholds. Do not call this function from a log callback. */
void SetLogOptions(const LogOptions& options);
LogOptions GetLogOptions();

const char* LogLevelName(LogLevel level) noexcept;
const char* LogSourceName(LogSource source) noexcept;

} // namespace livekit::core

#endif // _LKC_CORE_LOGGING_H_
