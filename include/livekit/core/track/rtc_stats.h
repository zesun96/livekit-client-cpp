/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef _LKC_CORE_TRACK_RTC_STATS_H_
#define _LKC_CORE_TRACK_RTC_STATS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace livekit {
namespace core {

class TrackInterface;

enum class RTCStatsDirection {
	Unknown,
	Send,
	Receive,
};

// A normalized RTP stream sample. Counter fields are cumulative and bitrate is populated by
// RTCStatsMonitor after two valid samples for the same stream.
struct RTCTrackStats {
	std::string id;
	RTCStatsDirection direction = RTCStatsDirection::Unknown;
	std::string kind;
	std::string rid;
	double timestamp_ms = 0.0;
	uint64_t bytes = 0;
	uint64_t packets = 0;
	int64_t packets_lost = 0;
	std::optional<double> bitrate_bps;
	std::optional<double> round_trip_time_seconds;
	std::optional<double> jitter_seconds;
	std::optional<double> audio_level;
	uint64_t concealed_samples = 0;
	uint32_t frame_width = 0;
	uint32_t frame_height = 0;
	double frames_per_second = 0.0;
	uint64_t frames = 0;
	uint64_t frames_dropped = 0;
	uint64_t fir_count = 0;
	uint64_t pli_count = 0;
	uint64_t nack_count = 0;
	uint64_t qp_sum = 0;
	std::string codec_mime_type;
	std::string codec_implementation;
	std::string quality_limitation_reason;
};

struct RTCStatsSnapshot {
	std::vector<RTCTrackStats> streams;

	bool Empty() const noexcept { return streams.empty(); }
};

// Parses libwebrtc RTCStatsReport::ToJson output. Invalid or empty reports produce an empty
// snapshot and never throw.
RTCStatsSnapshot ParseRTCStatsReport(std::string_view report_json) noexcept;

// Computes rates between caller-scheduled samples without owning the track or starting threads.
class RTCStatsMonitor {
public:
	RTCStatsSnapshot Sample(TrackInterface& track);
	RTCStatsSnapshot Sample(std::string_view report_json);
	void Reset() noexcept;

private:
	struct PreviousSample {
		double timestamp_ms = 0.0;
		uint64_t bytes = 0;
	};

	std::unordered_map<std::string, PreviousSample> previous_samples_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_RTC_STATS_H_
