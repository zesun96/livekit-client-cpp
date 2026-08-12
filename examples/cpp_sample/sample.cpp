#include "example_utils.h"

#include <iostream>

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

	auto* participant = room->GetLocalParticipant();
	std::cout << "Connected as " << participant->Identity() << " (" << participant->Sid() << ')'
	          << std::endl;
	room->Disconnect();
	return 0;
}
