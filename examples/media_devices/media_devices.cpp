#include "livekit/core/livekit_client.h"

#include <iostream>

namespace {

const char* KindName(livekit::core::MediaDeviceKind kind) {
	switch (kind) {
	case livekit::core::MediaDeviceKind::AudioInput:
		return "audio input";
	case livekit::core::MediaDeviceKind::AudioOutput:
		return "audio output";
	case livekit::core::MediaDeviceKind::VideoInput:
		return "video input";
	}
	return "unknown";
}

} // namespace

int main() {
	const auto devices = livekit::core::EnumerateMediaDevices();
	std::cout << "Found " << devices.size() << " media device(s)" << std::endl;
	for (const auto& device : devices) {
		std::cout << KindName(device.kind) << (device.is_default ? " [default] " : " ")
		          << device.label << "\n  " << device.id << std::endl;
	}
	return 0;
}
