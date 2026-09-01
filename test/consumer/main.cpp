#include <livekit/core/livekit_client.h>

#include <iostream>

int main() {
	livekit::core::TrackRecorderOptions recorder_options;
	recorder_options.output_path = "consumer-recording";
	livekit::core::TrackRecorderStats recorder_stats;
	livekit::core::EncodedVideoFrame encoded_frame;
	if (recorder_options.output_path.empty() ||
	    recorder_stats.state != livekit::core::TrackRecorderState::Stopped ||
	    encoded_frame.codec != livekit::core::EncodedVideoCodec::Unknown) {
		return 1;
	}
	const auto queued_duration = &livekit::core::AudioSourceInterface::QueuedDuration;
	const auto clear_queue = &livekit::core::AudioSourceInterface::ClearQueue;
	const auto wait_for_playout = &livekit::core::AudioSourceInterface::WaitForPlayout;
	(void)queued_duration;
	(void)clear_queue;
	(void)wait_for_playout;
	livekit::core::DataTrackSchema schema;
	schema.id = {"consumer.schema.v1",
	             {livekit::core::DataTrackSchemaEncodingKind::JsonSchema, {}}};
	livekit::core::DataTrackPublishOptions data_track_options;
	data_track_options.name = "consumer-smoke";
	data_track_options.frame_encoding =
	    livekit::core::DataTrackFrameEncoding{livekit::core::DataTrackFrameEncodingKind::Json, {}};
	data_track_options.schema = schema.id;
	const auto store_schema = &livekit::core::RoomInterface::StoreDataTrackSchema;
	const auto get_schema = &livekit::core::RoomInterface::GetDataTrackSchema;
	(void)store_schema;
	(void)get_schema;
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
