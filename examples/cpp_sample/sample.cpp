#include "livekit/core/livekit_client.h"
#include <thread>

void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3NDM3ODU4MTYsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjIiLCJuYmYiOjE3ND"
	                    "M2OTk0MTYsInN1YiI6InVzZXIyIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.6ygSXvL4HzXWeSdj-6v2meAoO_y2Eau5QZtDlxcmWXI";
	auto room_options = livekit::core::RoomOptions();
	auto room = livekit::core::Room();
	room.connect("http://localhost:7880/rtc", token, room_options);
	while (true) {
	}
	return;
}

int main(int argc, char* argv[]) {
	start();
	return 0;
}
