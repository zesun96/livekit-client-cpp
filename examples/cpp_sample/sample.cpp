#include "livekit/core/livekit_client.h"
#include <thread>

void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3MzUyMjM2OTEsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3Mz"
	                    "UxMzcyOTEsInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.fnUBGqUdCzGclw-nKLGseehDzGvwAKb-s4T9gnxHBdU";
	auto room_options = livekit::core::RoomOptions();
	auto room = livekit::core::Room();
	room.connect("ws://localhost:7880/rtc", token, room_options);
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
