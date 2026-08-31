/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_TRACING_H_
#define _LKC_CORE_TRACING_H_

#include <cstdint>
#include <memory>
#include <string>

namespace livekit::core {

enum class TraceCategory : std::uint64_t {
	Lifecycle = 1ULL << 0,
	Signaling = 1ULL << 1,
	Transport = 1ULL << 2,
	Track = 1ULL << 3,
	Data = 1ULL << 4,
	Rpc = 1ULL << 5,
	E2ee = 1ULL << 6,
};

inline constexpr std::uint64_t kAllTraceCategories =
    static_cast<std::uint64_t>(TraceCategory::Lifecycle) |
    static_cast<std::uint64_t>(TraceCategory::Signaling) |
    static_cast<std::uint64_t>(TraceCategory::Transport) |
    static_cast<std::uint64_t>(TraceCategory::Track) |
    static_cast<std::uint64_t>(TraceCategory::Data) |
    static_cast<std::uint64_t>(TraceCategory::Rpc) |
    static_cast<std::uint64_t>(TraceCategory::E2ee);

enum class TracePhase {
	Instant,
	DurationBegin,
	DurationEnd,
	AsyncBegin,
	AsyncEnd,
};

struct TraceRecord {
	TracePhase phase = TracePhase::Instant;
	TraceCategory category = TraceCategory::Lifecycle;
	std::string name;
	std::uint64_t timestamp_us = 0;
	std::uint64_t thread_id = 0;
	std::uint64_t correlation_id = 0;
};

/**
 * Receives synchronous trace callbacks from SDK threads. Implementations must be thread-safe and
 * should return quickly. Do not call a tracing configuration function from OnTrace(). Exceptions
 * are ignored by the SDK. Record strings are owned values.
 */
class TraceSinkInterface {
public:
	virtual ~TraceSinkInterface() = default;
	virtual void OnTrace(const TraceRecord& record) = 0;
};

struct TraceOptions {
	bool enabled = false;
	std::uint64_t category_mask = kAllTraceCategories;
};

/**
 * Replaces the process-wide sink. Passing nullptr disables trace delivery. The call waits for
 * OnTrace() calls on the previous sink to finish before returning.
 */
void SetTraceSink(std::shared_ptr<TraceSinkInterface> sink);
std::shared_ptr<TraceSinkInterface> GetTraceSink();

void SetTraceOptions(const TraceOptions& options);
TraceOptions GetTraceOptions();

/**
 * Creates a thread-safe streaming Chrome Trace Event JSON sink. The returned file can be opened by
 * Perfetto UI or chrome://tracing and is finalized when the last sink reference is destroyed.
 * Returns nullptr when the file cannot be opened.
 */
std::shared_ptr<TraceSinkInterface> CreateJsonTraceSink(const std::string& path);

const char* TraceCategoryName(TraceCategory category) noexcept;
const char* TracePhaseName(TracePhase phase) noexcept;

} // namespace livekit::core

#endif // _LKC_CORE_TRACING_H_
