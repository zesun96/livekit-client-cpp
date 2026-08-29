/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace livekit::capture {

struct WebRtcAudioProcessingStats {
	bool echo_return_loss_available = false;
	double echo_return_loss_db = 0.0;
	bool echo_return_loss_enhancement_available = false;
	double echo_return_loss_enhancement_db = 0.0;
	bool residual_echo_likelihood_available = false;
	double residual_echo_likelihood = 0.0;
	bool residual_echo_likelihood_recent_max_available = false;
	double residual_echo_likelihood_recent_max = 0.0;
	bool delay_median_available = false;
	std::int32_t delay_median_ms = 0;
	bool delay_standard_deviation_available = false;
	std::int32_t delay_standard_deviation_ms = 0;
	bool delay_available = false;
	std::int32_t delay_ms = 0;
};

class WebRtcAudioProcessor {
public:
	WebRtcAudioProcessor(bool echo_cancellation, bool auto_gain_control, bool noise_suppression);
	~WebRtcAudioProcessor();

	WebRtcAudioProcessor(const WebRtcAudioProcessor&) = delete;
	WebRtcAudioProcessor& operator=(const WebRtcAudioProcessor&) = delete;

	bool ProcessCapture(std::span<std::int16_t> samples, std::uint32_t sample_rate,
	                    std::uint32_t channels, std::uint32_t stream_delay_ms = 0) noexcept;
	bool ProcessRender(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
	                   std::uint32_t channels) noexcept;
	bool Configure(bool echo_cancellation, bool auto_gain_control, bool noise_suppression) noexcept;
	WebRtcAudioProcessingStats GetStats() const noexcept;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace livekit::capture
