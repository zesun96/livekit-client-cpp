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

#include "livekit/core/track/rtc_stats.h"

#include "livekit/core/track/track_interface.h"

#include <nlohmann/json.hpp>

#include <limits>
#include <unordered_map>
#include <utility>

namespace livekit {
namespace core {
namespace {

using Json = nlohmann::json;

const Json* FindObject(const std::unordered_map<std::string, const Json*>& objects,
                       const Json& object, const char* field) {
	auto value = object.find(field);
	if (value == object.end() || !value->is_string()) {
		return nullptr;
	}
	auto found = objects.find(value->get_ref<const std::string&>());
	return found != objects.end() ? found->second : nullptr;
}

std::string StringValue(const Json& object, const char* field) {
	auto value = object.find(field);
	return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

std::optional<double> DoubleValue(const Json& object, const char* field) {
	auto value = object.find(field);
	return value != object.end() && value->is_number() ? std::optional(value->get<double>())
	                                                   : std::nullopt;
}

uint64_t UnsignedValue(const Json& object, const char* field) {
	auto value = object.find(field);
	if (value == object.end() || !value->is_number()) {
		return 0;
	}
	if (value->is_number_unsigned()) {
		return value->get<uint64_t>();
	}
	if (value->is_number_integer()) {
		const auto number = value->get<int64_t>();
		return number > 0 ? static_cast<uint64_t>(number) : 0;
	}
	const auto number = value->get<double>();
	return number > 0.0 && number < static_cast<double>(std::numeric_limits<uint64_t>::max())
	           ? static_cast<uint64_t>(number)
	           : 0;
}

int64_t SignedValue(const Json& object, const char* field) {
	auto value = object.find(field);
	if (value == object.end() || !value->is_number()) {
		return 0;
	}
	if (value->is_number_unsigned()) {
		const auto number = value->get<uint64_t>();
		return number <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
		           ? static_cast<int64_t>(number)
		           : 0;
	}
	if (value->is_number_integer()) {
		return value->get<int64_t>();
	}
	const auto number = value->get<double>();
	return number >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
	               number <= static_cast<double>(std::numeric_limits<int64_t>::max())
	           ? static_cast<int64_t>(number)
	           : 0;
}

uint32_t Uint32Value(const Json& object, const char* field) {
	const auto number = UnsignedValue(object, field);
	return number <= std::numeric_limits<uint32_t>::max() ? static_cast<uint32_t>(number) : 0;
}

std::optional<double> FirstDouble(const Json& primary, const Json* secondary, const char* first,
                                  const char* second = nullptr) {
	if (auto value = DoubleValue(primary, first)) {
		return value;
	}
	if (second != nullptr) {
		if (auto value = DoubleValue(primary, second)) {
			return value;
		}
	}
	if (secondary != nullptr) {
		if (auto value = DoubleValue(*secondary, first)) {
			return value;
		}
		if (second != nullptr) {
			return DoubleValue(*secondary, second);
		}
	}
	return std::nullopt;
}

} // namespace

RTCStatsSnapshot ParseRTCStatsReport(std::string_view report_json) noexcept {
	RTCStatsSnapshot snapshot;
	if (report_json.empty()) {
		return snapshot;
	}

	try {
		auto report = Json::parse(report_json);
		if (!report.is_array()) {
			return snapshot;
		}

		std::unordered_map<std::string, const Json*> objects;
		for (const auto& object : report) {
			if (!object.is_object()) {
				continue;
			}
			auto id = object.find("id");
			if (id != object.end() && id->is_string()) {
				objects.emplace(id->get_ref<const std::string&>(), &object);
			}
		}

		for (const auto& object : report) {
			if (!object.is_object()) {
				continue;
			}
			const auto type = StringValue(object, "type");
			if (type != "outbound-rtp" && type != "inbound-rtp") {
				continue;
			}

			RTCTrackStats stats;
			stats.id = StringValue(object, "id");
			stats.direction =
			    type == "outbound-rtp" ? RTCStatsDirection::Send : RTCStatsDirection::Receive;
			stats.kind = StringValue(object, "kind");
			if (stats.kind.empty()) {
				stats.kind = StringValue(object, "mediaType");
			}
			stats.rid = StringValue(object, "rid");
			stats.timestamp_ms = DoubleValue(object, "timestamp").value_or(0.0);
			stats.bytes = UnsignedValue(
			    object, stats.direction == RTCStatsDirection::Send ? "bytesSent" : "bytesReceived");
			stats.packets = UnsignedValue(object, stats.direction == RTCStatsDirection::Send
			                                          ? "packetsSent"
			                                          : "packetsReceived");
			stats.packets_lost = SignedValue(object, "packetsLost");

			const Json* remote = FindObject(objects, object, "remoteId");
			stats.round_trip_time_seconds =
			    FirstDouble(object, remote, "roundTripTime", "currentRoundTripTime");
			stats.jitter_seconds = FirstDouble(object, remote, "jitter");
			stats.audio_level = DoubleValue(object, "audioLevel");
			stats.concealed_samples = UnsignedValue(object, "concealedSamples");
			stats.frame_width = Uint32Value(object, "frameWidth");
			stats.frame_height = Uint32Value(object, "frameHeight");
			stats.frames_per_second = DoubleValue(object, "framesPerSecond").value_or(0.0);
			stats.frames =
			    UnsignedValue(object, stats.direction == RTCStatsDirection::Send ? "framesSent"
			                                                                     : "framesDecoded");
			stats.frames_dropped = UnsignedValue(object, "framesDropped");
			stats.fir_count = UnsignedValue(object, "firCount");
			stats.pli_count = UnsignedValue(object, "pliCount");
			stats.nack_count = UnsignedValue(object, "nackCount");
			stats.qp_sum = UnsignedValue(object, "qpSum");
			stats.quality_limitation_reason = StringValue(object, "qualityLimitationReason");
			stats.codec_implementation = StringValue(
			    object, stats.direction == RTCStatsDirection::Send ? "encoderImplementation"
			                                                       : "decoderImplementation");
			if (const auto* codec = FindObject(objects, object, "codecId")) {
				stats.codec_mime_type = StringValue(*codec, "mimeType");
			}
			snapshot.streams.push_back(std::move(stats));
		}
	} catch (...) {
		return {};
	}
	return snapshot;
}

RTCStatsSnapshot TrackInterface::GetRTCStatsSnapshot() {
	return ParseRTCStatsReport(GetRTCStats());
}

RTCStatsSnapshot RTCStatsMonitor::Sample(TrackInterface& track) {
	return Sample(track.GetRTCStats());
}

RTCStatsSnapshot RTCStatsMonitor::Sample(std::string_view report_json) {
	auto snapshot = ParseRTCStatsReport(report_json);
	if (snapshot.Empty()) {
		return snapshot;
	}
	std::unordered_map<std::string, PreviousSample> current_samples;
	for (auto& stream : snapshot.streams) {
		if (!stream.id.empty() && stream.timestamp_ms > 0.0) {
			auto previous = previous_samples_.find(stream.id);
			if (previous != previous_samples_.end() && stream.bytes >= previous->second.bytes &&
			    stream.timestamp_ms > previous->second.timestamp_ms) {
				const auto elapsed_ms = stream.timestamp_ms - previous->second.timestamp_ms;
				stream.bitrate_bps = static_cast<double>(stream.bytes - previous->second.bytes) *
				                     8.0 * 1000.0 / elapsed_ms;
			}
			current_samples.emplace(stream.id, PreviousSample{.timestamp_ms = stream.timestamp_ms,
			                                                  .bytes = stream.bytes});
		}
	}
	previous_samples_ = std::move(current_samples);
	return snapshot;
}

void RTCStatsMonitor::Reset() noexcept { previous_samples_.clear(); }

} // namespace core
} // namespace livekit
