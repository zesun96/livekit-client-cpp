#include <livekit/core/livekit_client.h>

#include <iostream>

int main() {
	livekit::core::DataTrackPublishOptions data_track_options;
	data_track_options.name = "consumer-smoke";
	data_track_options.frame_encoding =
	    livekit::core::DataTrackFrameEncoding{livekit::core::DataTrackFrameEncodingKind::Json, {}};
	if (data_track_options.name.empty()) {
		return 1;
	}
	const auto version = livekit::core::Version();
	if (version.empty()) {
		std::cerr << "LiveKit SDK returned an empty version" << std::endl;
		return 1;
	}
	std::cout << "LiveKit SDK " << version << std::endl;
	return 0;
}
