/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "webrtc_audio_processor.h"

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/environment/environment_factory.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace livekit::capture {

class WebRtcAudioProcessor::Impl {
public:
	Impl(bool echo_cancellation, bool auto_gain_control, bool noise_suppression)
	    : processing_(Build(echo_cancellation, auto_gain_control, noise_suppression)),
	      echo_cancellation_(echo_cancellation) {}

	bool Configure(bool echo_cancellation, bool auto_gain_control,
	               bool noise_suppression) noexcept {
		try {
			auto processing = Build(echo_cancellation, auto_gain_control, noise_suppression);
			if (processing == nullptr) {
				return false;
			}
			std::lock_guard<std::mutex> guard(processing_mutex_);
			processing_ = std::move(processing);
			echo_cancellation_ = echo_cancellation;
			return true;
		} catch (...) {
			return false;
		}
	}

private:
	static webrtc::scoped_refptr<webrtc::AudioProcessing>
	Build(bool echo_cancellation, bool auto_gain_control, bool noise_suppression) {
		webrtc::AudioProcessing::Config config;
		config.pipeline.maximum_internal_processing_rate = 48000;
		config.echo_canceller.enabled = echo_cancellation;
		config.noise_suppression.enabled = noise_suppression;
		config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kHigh;
		config.gain_controller1.enabled = auto_gain_control;
		config.gain_controller1.mode =
		    webrtc::AudioProcessing::Config::GainController1::kAdaptiveDigital;
		return webrtc::BuiltinAudioProcessingBuilder(config).Build(webrtc::CreateEnvironment());
	}

public:
	bool ProcessCapture(std::span<std::int16_t> samples, std::uint32_t sample_rate,
	                    std::uint32_t channels, std::uint32_t stream_delay_ms) noexcept {
		if (!IsValidFrame(samples.size(), sample_rate, channels)) {
			return false;
		}
		std::lock_guard<std::mutex> guard(processing_mutex_);
		if (processing_ == nullptr) {
			return false;
		}
		const webrtc::StreamConfig stream(static_cast<int>(sample_rate), channels);
		if (echo_cancellation_ && processing_->set_stream_delay_ms(static_cast<int>(
		                              std::min<std::uint32_t>(stream_delay_ms, 500))) !=
		                              webrtc::AudioProcessing::kNoError) {
			return false;
		}
		return processing_->ProcessStream(samples.data(), stream, stream, samples.data()) ==
		       webrtc::AudioProcessing::kNoError;
	}

	bool ProcessRender(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
	                   std::uint32_t channels) noexcept {
		if (!IsValidFrame(samples.size(), sample_rate, channels)) {
			return false;
		}
		try {
			std::lock_guard<std::mutex> guard(processing_mutex_);
			if (!echo_cancellation_ || processing_ == nullptr) {
				return false;
			}
			thread_local std::vector<std::int16_t> render_buffer;
			render_buffer.assign(samples.begin(), samples.end());
			const webrtc::StreamConfig stream(static_cast<int>(sample_rate), channels);
			return processing_->ProcessReverseStream(render_buffer.data(), stream, stream,
			                                         render_buffer.data()) ==
			       webrtc::AudioProcessing::kNoError;
		} catch (...) {
			// Audio callbacks cannot propagate allocation failures across the device boundary.
			return false;
		}
	}

private:
	static bool IsValidFrame(std::size_t sample_count, std::uint32_t sample_rate,
	                         std::uint32_t channels) noexcept {
		return sample_rate >= 8000 && sample_rate % 100 == 0 && channels > 0 &&
		       sample_count == (sample_rate / 100) * channels;
	}

	bool echo_cancellation_ = false;
	std::mutex processing_mutex_;
	webrtc::scoped_refptr<webrtc::AudioProcessing> processing_;
};

WebRtcAudioProcessor::WebRtcAudioProcessor(bool echo_cancellation, bool auto_gain_control,
                                           bool noise_suppression)
    : impl_(std::make_unique<Impl>(echo_cancellation, auto_gain_control, noise_suppression)) {}

WebRtcAudioProcessor::~WebRtcAudioProcessor() = default;

bool WebRtcAudioProcessor::ProcessCapture(std::span<std::int16_t> samples,
                                          std::uint32_t sample_rate, std::uint32_t channels,
                                          std::uint32_t stream_delay_ms) noexcept {
	return impl_->ProcessCapture(samples, sample_rate, channels, stream_delay_ms);
}

bool WebRtcAudioProcessor::ProcessRender(std::span<const std::int16_t> samples,
                                         std::uint32_t sample_rate,
                                         std::uint32_t channels) noexcept {
	return impl_->ProcessRender(samples, sample_rate, channels);
}

bool WebRtcAudioProcessor::Configure(bool echo_cancellation, bool auto_gain_control,
                                     bool noise_suppression) noexcept {
	return impl_->Configure(echo_cancellation, auto_gain_control, noise_suppression);
}

} // namespace livekit::capture
