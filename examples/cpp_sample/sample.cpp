#include "livekit/core/livekit_client.h"
#include <thread>

void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3MzQ3OTU4MDEsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3Mz"
	                    "Q3MDk0MDEsInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.TBBouvrhnu7MZVQ60Sfdpr6P-j_1i3J5VrX37dduQIw";
	auto room_options = livekit::core::RoomOptions();
	auto room = livekit::core::Room();
	room.connect("ws://127.0.0.1:7880/rtc", token, room_options);
}

int main(int argc, char* argv[]) {
	std::thread t1(start);
	t1.join();
	while (true) {
	}
	return 0;
}
