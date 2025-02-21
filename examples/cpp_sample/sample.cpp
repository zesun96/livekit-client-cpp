#include "livekit/core/livekit_client.h"
#include <thread>

void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3NDAyNDcyNzIsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3ND"
	                    "AxNjA4NzIsInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.aiVbcF9n0m8IEBox9eYJfunqGrZbPyuO-6C6JSH9D2s";
	auto room_options = livekit::core::RoomOptions();
	auto room = livekit::core::Room();
	room.connect("http://localhost:7880/rtc", token, room_options);
	while (true) {
	}
	return;
}

int main(int argc, char* argv[]) {
	std::thread t1(start);
	// t1.join();
	while (true) {
	}
	return 0;
}
