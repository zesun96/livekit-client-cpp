#include "livekit/core/livekit_client.h"
#include <thread>

static void start() {
	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3NTAxNzE3OTMsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3NT"
	                    "AwODUzOTMsInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.cRQx6rxKooOt9dbqz1WgEfen0A4VDlsfMNTRDJE3mZM";
	auto room_options = livekit::core::default_room_connect_options();
	auto room = livekit::core::CreateRoom();
	room->Connect("http://localhost:7880/rtc", token, room_options);
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
