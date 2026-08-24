#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
	const auto arguments = livekit::examples::ReadConnectionArguments(argc, argv);
	if (!livekit::examples::ValidateConnectionArguments(arguments, argv[0])) {
		return 2;
	}

	livekit::examples::ClientRuntime runtime;
	if (!runtime.initialized()) {
		std::cerr << "Failed to initialize LiveKit" << std::endl;
		return 1;
	}
	auto room = livekit::core::CreateRoomUnique();
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return room->IsConnected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}

	livekit::core::DataTrackSchema schema;
	schema.id = {"example.telemetry.v1",
	             {livekit::core::DataTrackSchemaEncodingKind::JsonSchema, {}}};
	const std::string schema_json =
	    R"({"type":"object","properties":{"temperature":{"type":"number"}},"required":["temperature"]})";
	schema.definition.assign(schema_json.begin(), schema_json.end());
	if (const auto error = room->StoreDataTrackSchema(schema)) {
		std::cerr << "Failed to store DataTrack schema: " << error.message << std::endl;
		return 1;
	}

	livekit::core::DataTrackPublishOptions options;
	options.name = "example-telemetry";
	options.frame_encoding = {livekit::core::DataTrackFrameEncodingKind::Json, {}};
	options.schema = schema.id;
	auto published = room->GetLocalParticipant()->PublishDataTrack(options);
	if (!published) {
		std::cerr << "Failed to publish DataTrack: " << published.error.message << std::endl;
		return 1;
	}

	const std::string json = R"({"temperature":21.5})";
	livekit::core::DataTrackFrame frame;
	frame.payload.assign(json.begin(), json.end());
	frame.user_timestamp = 1;
	if (const auto error = published.track->TryPush(frame)) {
		std::cerr << "Failed to send DataTrack frame: " << error.message << std::endl;
		return 1;
	}
	std::cout << "Published one JSON DataTrack frame using schema " << schema.id.name << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(1));
	published.track->Unpublish();
	room->Disconnect();
	return 0;
}
