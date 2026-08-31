/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_DETAIL_TRACING_H_
#define _LKC_CORE_DETAIL_TRACING_H_

#include "livekit/core/tracing.h"

#include <cstdint>
#include <string_view>

namespace livekit::core::detail {

bool IsTraceEnabled(TraceCategory category) noexcept;
std::uint64_t NextTraceCorrelationId() noexcept;
void EmitTrace(TraceCategory category, TracePhase phase, std::string_view name,
               std::uint64_t correlation_id = 0) noexcept;

class TraceSpan {
public:
	TraceSpan(TraceCategory category, const char* name) noexcept;
	~TraceSpan() noexcept;

	TraceSpan(const TraceSpan&) = delete;
	TraceSpan& operator=(const TraceSpan&) = delete;

private:
	TraceCategory category_;
	const char* name_ = nullptr;
	bool active_ = false;
};

} // namespace livekit::core::detail

#define LKC_TRACE_JOIN_INNER(left, right) left##right
#define LKC_TRACE_JOIN(left, right)       LKC_TRACE_JOIN_INNER(left, right)
#define LKC_TRACE_SPAN(category, name)                                                             \
	::livekit::core::detail::TraceSpan LKC_TRACE_JOIN(lkc_trace_span_, __LINE__)(category, name)
#define LKC_TRACE_INSTANT(category, name)                                                          \
	::livekit::core::detail::EmitTrace(category, ::livekit::core::TracePhase::Instant, name)

#endif // _LKC_CORE_DETAIL_TRACING_H_
