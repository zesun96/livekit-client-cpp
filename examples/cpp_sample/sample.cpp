#include "livekit/core/livekit_client.h"
#include <thread>

void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE3MzQ4ODIxMTksImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3MzQ3OTU3MTksInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6dHJ1ZX19.VvRxg8pruyAhZ9ATHHEI2A-j5VlJZNSJbCEihKJr1Xg";
	auto room_options = livekit::core::RoomOptions();
	auto room = livekit::core::Room();
	room.connect("ws://127.0.0.1:7880/rtc", token, room_options);
	//room.connect("ws://localhost:7880/rtc", token, room_options);
	while (true) {
	}
	return;
}

int main(int argc, char* argv[]) {
	std::thread t1(start);
	t1.join();
	while (true) {
	}
	return 0;
}
