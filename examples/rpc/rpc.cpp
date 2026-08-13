#include "example_utils.h"

#include "livekit/core/rpc.h"

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
	room->RegisterRpcMethod("example.echo", [](const livekit::core::RpcInvocationData& invocation) {
		std::cout << "RPC from " << invocation.caller_identity << ": " << invocation.payload
		          << std::endl;
		return livekit::core::RpcResult::Success("echo:" + invocation.payload);
	});
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return room->IsConnected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}

	if (argc >= 4) {
		livekit::core::PerformRpcParams params;
		params.destination_identity = argv[3];
		params.method = "example.echo";
		params.payload = argc >= 5 ? argv[4] : "hello";
		const auto result = room->GetLocalParticipant()->PerformRpc(params);
		if (!result.Ok()) {
			std::cerr << "RPC failed (" << static_cast<uint32_t>(result.error->code)
			          << "): " << result.error->message << std::endl;
			return 1;
		}
		std::cout << "RPC response: " << result.payload << std::endl;
	} else {
		std::cout << "RPC receiver ready as " << room->GetLocalParticipant()->Identity()
		          << "; waiting for 30 seconds" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(30));
	}

	room->Disconnect();
	return 0;
}
