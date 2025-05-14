#include "livekit/core/livekit_client.h"
#include <thread>

void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3NDQyOTUwNzMsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3ND"
	                    "QyMDg2NzMsInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.-eqtIhSAt0B-KKxOObJBNGDOC554oFuwXa-_YrNNRBg";
	auto room_options = livekit::core::default_room_options();
	auto room = livekit::core::Room();
	room.Connect("http://localhost:7880/rtc", token, room_options);
	while (true) {
	}
	return;
}

int main(int argc, char* argv[]) {
	livekit::core::Init();
	start();
	livekit::core::Destroy();
	return 0;
}
