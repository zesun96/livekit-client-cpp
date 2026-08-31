#include <livekit/core/livekit_client.h>
#include <vld.h>

#include <iostream>

namespace {

int ExerciseSdkLifecycle() {
	using namespace livekit::core;

	std::cerr << "Initializing LiveKit SDK...\n";
	if (!Init()) {
		std::cerr << "LiveKit SDK initialization failed.\n";
		return 1;
	}
#if defined(_DEBUG)
	// WebRTC intentionally creates process-lifetime locks and RNG state during its first
	// initialization. Keep the gate focused on allocations owned by the SDK lifecycle.
	VLDMarkAllLeaksAsReported();
#endif

	int result = 0;
	{
		std::cerr << "Creating and destroying an idle room...\n";
		auto room = CreateRoomUnique();
		if (!room || room->State() != RoomInterface::RoomState::Disconnected) {
			std::cerr << "LiveKit SDK lifecycle probe returned unexpected state.\n";
			result = 1;
		}
	}

	std::cerr << "Shutting down LiveKit SDK...\n";
	if (!Destroy()) {
		std::cerr << "LiveKit SDK shutdown failed.\n";
		result = 1;
	}
	return result;
}

} // namespace

int main() {
#if !defined(_DEBUG)
	std::cout << "Visual Leak Detector is active only for Debug builds.\n";
	return 77;
#else
	VLDSetReportOptions(VLD_OPT_REPORT_TO_STDOUT, nullptr);
	VLDSetOptions(VLDGetOptions() | VLD_OPT_AGGREGATE_DUPLICATES | VLD_OPT_SKIP_CRTSTARTUP_LEAKS, 0,
	              64);
	VLDMarkAllLeaksAsReported();

	const int sdk_result = ExerciseSdkLifecycle();
	std::cerr << "Counting unfreed SDK lifecycle blocks...\n";
	const VLD_UINT leak_count = VLDGetLeaksCount();
	if (leak_count != 0) {
		VLDReportLeaks();
		std::cerr << "Visual Leak Detector found " << leak_count
		          << " unfreed SDK lifecycle block(s).\n";
	}
	VLDMarkAllLeaksAsReported();

	if (sdk_result != 0) {
		return sdk_result;
	}
	return leak_count == 0 ? 0 : 86;
#endif
}
