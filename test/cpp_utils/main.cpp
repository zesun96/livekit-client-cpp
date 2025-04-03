#include "livekit/core/livekit_client_test.h"
#include <thread>

void test1() { livekit::core::TestWebrtc(); }
void test2() { livekit::core::Test(); }

int main(int argc, char* argv[]) {

	std::thread t1(test1);
	t1.join();

    //std::thread t2(test2);
	//t2.join();
	while (true) {
	}
	return 0;
}
