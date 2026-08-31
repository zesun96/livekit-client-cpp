/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "tracing.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

namespace livekit::core {
namespace {

struct TracingState {
	std::mutex configuration_mutex;
	std::mutex mutex;
	std::condition_variable deliveries_finished;
	std::shared_ptr<TraceSinkInterface> sink;
	std::size_t deliveries_in_progress = 0;
	std::atomic<bool> enabled{false};
	std::atomic<std::uint64_t> category_mask{kAllTraceCategories};
};

TracingState& State() {
	static TracingState state;
	return state;
}

std::uint64_t TimestampMicroseconds() noexcept {
	static const auto epoch = std::chrono::steady_clock::now();
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
	                                      std::chrono::steady_clock::now() - epoch)
	                                      .count());
}

std::uint64_t CurrentThreadId() noexcept {
	const auto value =
	    static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	return value != 0 ? value : 1;
}

std::string EscapeJson(std::string_view value) {
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
		case '"':
			escaped << "\\\"";
			break;
		case '\\':
			escaped << "\\\\";
			break;
		case '\b':
			escaped << "\\b";
			break;
		case '\f':
			escaped << "\\f";
			break;
		case '\n':
			escaped << "\\n";
			break;
		case '\r':
			escaped << "\\r";
			break;
		case '\t':
			escaped << "\\t";
			break;
		default:
			if (character < 0x20) {
				escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
				        << static_cast<unsigned int>(character) << std::dec;
			} else {
				escaped << static_cast<char>(character);
			}
			break;
		}
	}
	return escaped.str();
}

class JsonTraceSink final : public TraceSinkInterface {
public:
	explicit JsonTraceSink(const std::string& path)
	    : output_(path, std::ios::binary | std::ios::trunc) {
		if (output_) {
			output_ << "{\"traceEvents\":[";
		}
	}

	~JsonTraceSink() override {
		std::lock_guard<std::mutex> guard(mutex_);
		if (output_) {
			output_ << "],\"displayTimeUnit\":\"ms\"}\n";
			output_.flush();
		}
	}

	bool IsOpen() const noexcept { return output_.is_open() && output_.good(); }

	void OnTrace(const TraceRecord& record) override {
		std::lock_guard<std::mutex> guard(mutex_);
		if (!output_) {
			return;
		}
		if (!first_) {
			output_ << ',';
		}
		first_ = false;
		output_ << "{\"name\":\"" << EscapeJson(record.name) << "\",\"cat\":\""
		        << TraceCategoryName(record.category) << "\",\"ph\":\""
		        << TracePhaseName(record.phase) << "\",\"ts\":" << record.timestamp_us
		        << ",\"pid\":0,\"tid\":" << record.thread_id;
		if (record.correlation_id != 0) {
			output_ << ",\"id\":" << record.correlation_id;
		}
		if (record.phase == TracePhase::Instant) {
			output_ << ",\"s\":\"t\"";
		}
		output_ << '}';
		output_.flush();
	}

private:
	mutable std::mutex mutex_;
	std::ofstream output_;
	bool first_ = true;
};

} // namespace

void SetTraceSink(std::shared_ptr<TraceSinkInterface> sink) {
	auto& state = State();
	std::lock_guard<std::mutex> configuration_guard(state.configuration_mutex);
	std::shared_ptr<TraceSinkInterface> previous;
	{
		std::unique_lock<std::mutex> guard(state.mutex);
		previous.swap(state.sink);
		state.deliveries_finished.wait(guard,
		                               [&state] { return state.deliveries_in_progress == 0; });
		state.sink = std::move(sink);
	}
	previous.reset();
}

std::shared_ptr<TraceSinkInterface> GetTraceSink() {
	auto& state = State();
	std::lock_guard<std::mutex> guard(state.mutex);
	return state.sink;
}

void SetTraceOptions(const TraceOptions& options) {
	auto& state = State();
	state.category_mask.store(options.category_mask, std::memory_order_relaxed);
	state.enabled.store(options.enabled, std::memory_order_release);
}

TraceOptions GetTraceOptions() {
	auto& state = State();
	return {state.enabled.load(std::memory_order_acquire),
	        state.category_mask.load(std::memory_order_relaxed)};
}

std::shared_ptr<TraceSinkInterface> CreateJsonTraceSink(const std::string& path) {
	if (path.empty()) {
		return nullptr;
	}
	auto sink = std::make_shared<JsonTraceSink>(path);
	return sink->IsOpen() ? std::move(sink) : nullptr;
}

const char* TraceCategoryName(TraceCategory category) noexcept {
	switch (category) {
	case TraceCategory::Lifecycle:
		return "lifecycle";
	case TraceCategory::Signaling:
		return "signaling";
	case TraceCategory::Transport:
		return "transport";
	case TraceCategory::Track:
		return "track";
	case TraceCategory::Data:
		return "data";
	case TraceCategory::Rpc:
		return "rpc";
	case TraceCategory::E2ee:
		return "e2ee";
	}
	return "unknown";
}

const char* TracePhaseName(TracePhase phase) noexcept {
	switch (phase) {
	case TracePhase::Instant:
		return "i";
	case TracePhase::DurationBegin:
		return "B";
	case TracePhase::DurationEnd:
		return "E";
	case TracePhase::AsyncBegin:
		return "b";
	case TracePhase::AsyncEnd:
		return "e";
	}
	return "i";
}

namespace detail {

bool IsTraceEnabled(TraceCategory category) noexcept {
	auto& state = State();
	return state.enabled.load(std::memory_order_acquire) &&
	       (state.category_mask.load(std::memory_order_relaxed) &
	        static_cast<std::uint64_t>(category)) != 0;
}

std::uint64_t NextTraceCorrelationId() noexcept {
	static std::atomic<std::uint64_t> next_id{1};
	return next_id.fetch_add(1, std::memory_order_relaxed);
}

void EmitTrace(TraceCategory category, TracePhase phase, std::string_view name,
               std::uint64_t correlation_id) noexcept {
	std::shared_ptr<TraceSinkInterface> sink;
	try {
		if (!IsTraceEnabled(category)) {
			return;
		}
		{
			auto& state = State();
			std::lock_guard<std::mutex> guard(state.mutex);
			sink = state.sink;
			if (sink) {
				++state.deliveries_in_progress;
			}
		}
		if (sink) {
			sink->OnTrace({phase, category, std::string(name), TimestampMicroseconds(),
			               CurrentThreadId(), correlation_id});
		}
	} catch (...) {
		// Tracing must never change SDK control flow.
	}
	if (sink) {
		auto& state = State();
		{
			std::lock_guard<std::mutex> guard(state.mutex);
			--state.deliveries_in_progress;
		}
		state.deliveries_finished.notify_all();
	}
}

TraceSpan::TraceSpan(TraceCategory category, const char* name) noexcept
    : category_(category), name_(name), active_(name != nullptr && IsTraceEnabled(category)) {
	if (active_) {
		EmitTrace(category_, TracePhase::DurationBegin, name_);
	}
}

TraceSpan::~TraceSpan() noexcept {
	if (active_) {
		EmitTrace(category_, TracePhase::DurationEnd, name_);
	}
}

} // namespace detail
} // namespace livekit::core
