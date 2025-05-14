#include "livekit/core/livekit_client_test.h"
#include <iostream>
#include <thread>

static void test1() { livekit::core::TestWebrtc(); }
static void test2() { livekit::core::Test(); }

static void test3() { livekit::core::TestPeerConntion(); }

static void test4() { livekit::core::TestIceGathering(); }

int main(int argc, char* argv[]) {
	auto ret = livekit::core::Init();
	if (!ret) {
		std::cout << "init failed" << std::endl;
		return -1;
	}
	// std::thread t1(test1);
	// t1.join();

	// std::thread t2(test2);
	// t2.join();

	std::thread t3(test3);
	t3.join();

	//std::thread t4(test4);
	//t4.join();
	while (true) {
	}
	livekit::core::Destroy();
	return 0;
}
