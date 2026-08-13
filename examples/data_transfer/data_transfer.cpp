#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
	const auto arguments = livekit::examples::ReadConnectionArguments(argc, argv);
	if (!livekit::examples::ValidateConnectionArguments(arguments, argv[0])) {
		return 2;
	}
	std::filesystem::path path;
	if (argc >= 4) {
		path = argv[3];
	} else if (const char* value = std::getenv("LIVEKIT_FILE")) {
		path = value;
	} else {
		std::cerr << "Usage: " << argv[0] << " <url> <token> <file>\n"
		          << "When using LIVEKIT_URL and LIVEKIT_TOKEN, also set LIVEKIT_FILE."
		          << std::endl;
		return 2;
	}
	if (!std::filesystem::is_regular_file(path)) {
		std::cerr << "Not a readable file: " << path << std::endl;
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

	livekit::core::TextSendOptions message_options;
	message_options.topic = "text-transfer";
	const std::string message = "sending:" + path.filename().string();
	const std::vector<uint8_t> bytes{'l', 'i', 'v', 'e', 'k', 'i', 't'};
	livekit::core::ByteSendOptions byte_options;
	byte_options.topic = "byte-transfer";
	livekit::core::StreamTextOptions stream_options;
	stream_options.topic = "incremental-text-transfer";
	const std::string first_chunk = "incremental ";
	const std::string second_chunk = "message";
	stream_options.total_size = first_chunk.size() + second_chunk.size();
	auto writer = room->GetLocalParticipant()->StreamText(stream_options);
	if (!room->GetLocalParticipant()->SendText(message, message_options) ||
	    !room->GetLocalParticipant()->SendBytes(bytes, byte_options) ||
	    !room->GetLocalParticipant()->SendFile(path.string()) || !writer ||
	    !writer->Write(first_chunk) || !writer->Write(second_chunk) || !writer->Close()) {
		std::cerr << "Failed to send data streams" << std::endl;
		return 1;
	}

	std::cout << "Sent " << path.filename() << " (" << std::filesystem::file_size(path) << " bytes)"
	          << std::endl;
	room->Disconnect();
	return 0;
}
