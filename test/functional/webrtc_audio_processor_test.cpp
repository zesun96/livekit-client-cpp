#include "webrtc_audio_processor.h"
#include "../support/audio_fixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace livekit::capture {
namespace {

double Rms(std::span<const std::int16_t> samples) {
	double sum_squares = 0.0;
	for (const auto sample : samples) {
		sum_squares += static_cast<double>(sample) * sample;
	}
	return std::sqrt(sum_squares / samples.size());
}

std::vector<std::int16_t> LoadSpeechFixture() {
	const auto path = std::filesystem::path(LIVEKIT_TEST_RESOURCE_DIR) / "audio" /
	                  "change-sophie.wav";
	return test_support::LoadPcm16Mono48Khz(path);
}

double BestDelayedProjectionGain(std::span<const std::int16_t> output,
	                              std::span<const std::int16_t> signal,
	                              std::size_t frame_samples,
	                              std::size_t maximum_delay_frames) {
	double best_gain = 0.0;
	for (std::size_t delay_frames = 0; delay_frames <= maximum_delay_frames; ++delay_frames) {
		const auto delay = delay_frames * frame_samples;
		if (delay >= output.size()) {
			break;
		}
		double projection = 0.0;
		double signal_energy = 0.0;
		for (std::size_t index = delay; index < output.size(); ++index) {
			projection += static_cast<double>(output[index]) * signal[index - delay];
			signal_energy += static_cast<double>(signal[index - delay]) * signal[index - delay];
		}
		if (signal_energy > 0.0) {
			const double gain = projection / signal_energy;
			if (std::abs(gain) > std::abs(best_gain)) {
				best_gain = gain;
			}
		}
	}
	return best_gain;
}

TEST(WebRtcAudioProcessorTest, AcceptsTenMillisecondCaptureAndRenderFrames) {
	WebRtcAudioProcessor processor(true, true, true);
	std::array<std::int16_t, 480> capture{};
	const std::array<std::int16_t, 960> render{};

	EXPECT_TRUE(processor.ProcessRender(render, 48000, 2));
	EXPECT_TRUE(processor.ProcessCapture(capture, 48000, 1));
	EXPECT_FALSE(processor.ProcessCapture(std::span<std::int16_t>(capture).first(479), 48000, 1));
	EXPECT_FALSE(
	    processor.ProcessRender(std::span<const std::int16_t>(render).first(959), 48000, 2));
}

TEST(WebRtcAudioProcessorTest, PreservesPcmWhenAllProcessingIsDisabled) {
	WebRtcAudioProcessor processor(false, false, false);
	std::array<std::int16_t, 480> capture{};
	capture[0] = -1234;
	capture[479] = 2345;
	const auto expected = capture;
	const std::array<std::int16_t, 480> render{};

	ASSERT_TRUE(processor.ProcessCapture(capture, 48000, 1));
	EXPECT_EQ(capture, expected);
	EXPECT_FALSE(processor.ProcessRender(render, 48000, 1));
}

TEST(WebRtcAudioProcessorTest, SupportsRuntimeReconfiguration) {
	WebRtcAudioProcessor processor(true, true, true);
	std::array<std::int16_t, 480> capture{};
	const std::array<std::int16_t, 960> render{};

	ASSERT_TRUE(processor.ProcessRender(render, 48000, 2));
	ASSERT_TRUE(processor.Configure(false, false, false));
	EXPECT_FALSE(processor.ProcessRender(render, 48000, 2));
	EXPECT_TRUE(processor.ProcessCapture(capture, 48000, 1, 20));
	ASSERT_TRUE(processor.Configure(true, false, true));
	EXPECT_TRUE(processor.ProcessRender(render, 48000, 2));
	EXPECT_TRUE(processor.ProcessCapture(capture, 48000, 1, 20));
}

TEST(WebRtcAudioProcessorTest, ExposesAecStatisticsWithoutInventingUnavailableValues) {
	WebRtcAudioProcessor processor(true, false, false);
	std::array<std::int16_t, 480> capture{};
	std::array<std::int16_t, 480> render{};
	constexpr double pi = 3.14159265358979323846;
	for (std::size_t frame = 0; frame < 500; ++frame) {
		for (std::size_t index = 0; index < render.size(); ++index) {
			const auto sample = frame * render.size() + index;
			render[index] = static_cast<std::int16_t>(
			    4000.0 * std::sin(2.0 * pi * 700.0 * static_cast<double>(sample) / 48000.0));
			capture[index] = render[index] / 2;
		}
		ASSERT_TRUE(processor.ProcessRender(render, 48000, 1));
		ASSERT_TRUE(processor.ProcessCapture(capture, 48000, 1, 20));
	}

	const auto stats = processor.GetStats();
	if (stats.echo_return_loss_available) {
		EXPECT_TRUE(std::isfinite(stats.echo_return_loss_db));
	}
	if (stats.echo_return_loss_enhancement_available) {
		EXPECT_TRUE(std::isfinite(stats.echo_return_loss_enhancement_db));
	}
	if (stats.residual_echo_likelihood_available) {
		EXPECT_GE(stats.residual_echo_likelihood, 0.0);
		EXPECT_LE(stats.residual_echo_likelihood, 1.0);
	}
	if (stats.residual_echo_likelihood_recent_max_available) {
		EXPECT_GE(stats.residual_echo_likelihood_recent_max, 0.0);
		EXPECT_LE(stats.residual_echo_likelihood_recent_max, 1.0);
	}
}

TEST(WebRtcAudioProcessorTest, SuppressesStationaryNoiseAfterAdaptation) {
	WebRtcAudioProcessor processor(false, false, true);
	std::array<std::int16_t, 480> capture{};
	uint32_t noise_state = 0x9e3779b9u;
	double input_energy = 0.0;
	double output_energy = 0.0;
	std::size_t measured_samples = 0;
	for (std::size_t frame = 0; frame < 600; ++frame) {
		for (auto& sample : capture) {
			noise_state ^= noise_state << 13;
			noise_state ^= noise_state >> 17;
			noise_state ^= noise_state << 5;
			const auto centered = static_cast<int32_t>(noise_state & 0xffffu) - 32768;
			sample = static_cast<std::int16_t>(centered * 1800 / 32768);
		}
		const auto input_rms = Rms(capture);
		ASSERT_TRUE(processor.ProcessCapture(capture, 48000, 1));
		if (frame >= 400) {
			input_energy += input_rms * input_rms * capture.size();
			const auto output_rms = Rms(capture);
			output_energy += output_rms * output_rms * capture.size();
			measured_samples += capture.size();
		}
	}
	const double input_rms = std::sqrt(input_energy / measured_samples);
	const double output_rms = std::sqrt(output_energy / measured_samples);
	EXPECT_LT(output_rms, input_rms * 0.75)
	    << "input_rms=" << input_rms << ", output_rms=" << output_rms;
}

TEST(WebRtcAudioProcessorTest, PreservesNearEndSignalDuringDoubleTalk) {
	WebRtcAudioProcessor processor(true, false, false);
	const auto speech = LoadSpeechFixture();
	ASSERT_GE(speech.size(), 500u * 480u);
	std::array<std::int16_t, 480> render{};
	std::array<std::int16_t, 480> capture{};
	std::array<std::int16_t, 480> near_end{};
	uint32_t noise_state = 0x243f6a88u;
	std::size_t speech_offset = 0;
	std::vector<std::int16_t> measured_output;
	std::vector<std::int16_t> measured_near_end;
	std::vector<std::int16_t> measured_render;
	measured_output.reserve(300 * capture.size());
	measured_near_end.reserve(300 * capture.size());
	measured_render.reserve(300 * capture.size());
	for (std::size_t frame = 0; frame < 1000; ++frame) {
		for (std::size_t index = 0; index < render.size(); ++index) {
			noise_state ^= noise_state << 13;
			noise_state ^= noise_state >> 17;
			noise_state ^= noise_state << 5;
			const auto centered = static_cast<int32_t>(noise_state & 0xffffu) - 32768;
			render[index] = static_cast<std::int16_t>(centered * 6000 / 32768);
			near_end[index] = frame >= 500 ? speech[speech_offset++] : 0;
			capture[index] = static_cast<std::int16_t>(render[index] / 2 + near_end[index]);
		}
		ASSERT_TRUE(processor.ProcessRender(render, 48000, 1));
		ASSERT_TRUE(processor.ProcessCapture(capture, 48000, 1, 0));
		if (frame >= 700) {
			measured_output.insert(measured_output.end(), capture.begin(), capture.end());
			measured_near_end.insert(measured_near_end.end(), near_end.begin(), near_end.end());
			measured_render.insert(measured_render.end(), render.begin(), render.end());
		}
	}
	const double near_end_gain = BestDelayedProjectionGain(
	    measured_output, measured_near_end, capture.size(), 20);
	const double residual_echo_gain = std::abs(BestDelayedProjectionGain(
	    measured_output, measured_render, capture.size(), 20));
	EXPECT_GT(std::abs(near_end_gain), 0.60) << "near_end_gain=" << near_end_gain;
	EXPECT_LT(residual_echo_gain, 0.25) << "residual_echo_gain=" << residual_echo_gain;
}

} // namespace
} // namespace livekit::capture
