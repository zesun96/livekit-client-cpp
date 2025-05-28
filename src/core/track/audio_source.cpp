/**
 *
 * Copyright (c) 2025 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "audio_source.h"

namespace livekit {
namespace core {

void AudioSource::CaptureFrame(void* data, uint32_t sample_rate, uint32_t num_channels,
                               uint32_t samples_per_channel) {
	// process audio data
}

AudioSourceInterface* CreateAudioSource(AudioSourceOptions options, uint32_t sample_rate,
                                        uint32_t num_channels, uint32_t queue_size_samples) {
	return new AudioSource();
}

} // namespace core
} // namespace livekit