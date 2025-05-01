#include "livekit/core/livekit_client_test.h"
#include <thread>

void test1() { livekit::core::TestWebrtc(); }
void test2() { livekit::core::Test(); }

void test3() { livekit::core::TestPeerConntion(); }

int main(int argc, char* argv[]) {
	livekit::core::Init();
	// std::thread t1(test1);
	// t1.join();

	// std::thread t2(test2);
	// t2.join();

	std::thread t3(test3);
	t3.join();
	while (true) {
	}
	livekit::core::Destroy();
	return 0;
}
