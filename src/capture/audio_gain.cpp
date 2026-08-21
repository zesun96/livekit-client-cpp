/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_gain.h"

#include <algorithm>
#include <cmath>

namespace livekit::capture {

bool IsAudioGainValid(float gain) noexcept {
	return std::isfinite(gain) && gain >= kMinAudioGain && gain <= kMaxAudioGain;
}

bool ApplyAudioGain(std::span<const std::int16_t> input, std::span<std::int16_t> output,
                    float gain) noexcept {
	if (input.size() != output.size() || !IsAudioGainValid(gain)) {
		return false;
	}
	if (gain == kMaxAudioGain) {
		if (input.data() != output.data()) {
			std::copy(input.begin(), input.end(), output.begin());
		}
		return true;
	}
	for (std::size_t index = 0; index < input.size(); ++index) {
		output[index] = static_cast<std::int16_t>(std::lround(input[index] * gain));
	}
	return true;
}

} // namespace livekit::capture
