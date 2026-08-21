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

#include "livekit/core/media_device.h"

#include "../capture/audio_capture_adapter.h"
#include "../capture/camera_capture_adapter.h"
#include <utility>
#include <vector>

namespace livekit::core {
namespace {

void EnumerateAudioDevices(std::vector<MediaDeviceInfo>& devices) {
	for (auto& device : capture::EnumerateAudioDevices()) {
		devices.push_back({std::move(device.id), std::move(device.label),
		                   device.kind == capture::AudioDeviceKind::Input
		                       ? MediaDeviceKind::AudioInput
		                       : MediaDeviceKind::AudioOutput,
		                   device.is_default});
	}
}

void EnumerateVideoInputs(std::vector<MediaDeviceInfo>& devices) {
	for (auto& device : capture::EnumerateCameraDevices()) {
		devices.push_back(
		    {std::move(device.id), std::move(device.label), MediaDeviceKind::VideoInput, false});
	}
}

} // namespace

std::vector<MediaDeviceInfo> EnumerateMediaDevices() {
	std::vector<MediaDeviceInfo> devices;
	EnumerateAudioDevices(devices);
	EnumerateVideoInputs(devices);
	return devices;
}

} // namespace livekit::core
