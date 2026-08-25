/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_DETAIL_LOGGING_H_
#define _LKC_CORE_DETAIL_LOGGING_H_

#include "livekit/core/logging.h"

#include <sstream>
#include <string>

namespace livekit::core::detail {

bool IsLogEnabled(LogLevel level, LogSource source) noexcept;
void EmitLog(LogLevel level, LogSource source, std::string message, const char* file = nullptr,
             int line = 0) noexcept;

void StartBackendLogging();
void RefreshBackendLogging();
void StopBackendLogging();

class LogMessage {
public:
	LogMessage(LogLevel level, LogSource source, const char* file, int line);
	~LogMessage() noexcept;

	std::ostringstream& stream() { return stream_; }

private:
	LogLevel level_;
	LogSource source_;
	const char* file_;
	int line_;
	std::ostringstream stream_;
};

class LogVoidify {
public:
	void operator&(std::ostream&) const noexcept {}
};

} // namespace livekit::core::detail

#define LKC_LOG_AT(level)                                                                          \
	!::livekit::core::detail::IsLogEnabled(level, ::livekit::core::LogSource::LiveKit)             \
	    ? (void)0                                                                                  \
	    : ::livekit::core::detail::LogVoidify() &                                                  \
	          ::livekit::core::detail::LogMessage(level, ::livekit::core::LogSource::LiveKit,      \
	                                              __FILE__, __LINE__)                              \
	              .stream()
#define LKC_LOG_TRACE   LKC_LOG_AT(::livekit::core::LogLevel::Trace)
#define LKC_LOG_DEBUG   LKC_LOG_AT(::livekit::core::LogLevel::Debug)
#define LKC_LOG_INFO    LKC_LOG_AT(::livekit::core::LogLevel::Info)
#define LKC_LOG_WARNING LKC_LOG_AT(::livekit::core::LogLevel::Warning)
#define LKC_LOG_ERROR   LKC_LOG_AT(::livekit::core::LogLevel::Error)

#endif // _LKC_CORE_DETAIL_LOGGING_H_
