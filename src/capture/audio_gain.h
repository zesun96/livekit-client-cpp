/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <span>

namespace livekit::capture {

constexpr float kMinAudioGain = 0.0F;
constexpr float kMaxAudioGain = 1.0F;

bool IsAudioGainValid(float gain) noexcept;
bool ApplyAudioGain(std::span<const std::int16_t> input, std::span<std::int16_t> output,
                    float gain) noexcept;

} // namespace livekit::capture
