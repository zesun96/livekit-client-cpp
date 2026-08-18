/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace livekit::capture {

class WebRtcAudioProcessor {
public:
	WebRtcAudioProcessor(bool echo_cancellation, bool auto_gain_control, bool noise_suppression);
	~WebRtcAudioProcessor();

	WebRtcAudioProcessor(const WebRtcAudioProcessor&) = delete;
	WebRtcAudioProcessor& operator=(const WebRtcAudioProcessor&) = delete;

	bool ProcessCapture(std::span<std::int16_t> samples, std::uint32_t sample_rate,
	                    std::uint32_t channels) noexcept;
	void ProcessRender(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
	                   std::uint32_t channels) noexcept;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace livekit::capture
