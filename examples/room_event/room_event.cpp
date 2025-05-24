/**
 *
 * Copyright (c) 2025 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "livekit/core/livekit_client.h"

#include "livekit/core/track/audio_media_track_interface.h"
#include <chrono>
#include <iostream>
#include <thread>

class RoomEvent : public livekit::core::RoomEventInterface {
public:
	RoomEvent() = default;
	virtual ~RoomEvent() = default;
	virtual void OnConnected() override {
		std::cout << "Room connected" << std::endl;
		return;
	}
};

class AudioTrack : public livekit::core::AudioMediaTrackInterface {
public:
	AudioTrack() {}
	virtual ~AudioTrack() = default;

	virtual MediaTrackKind kind() override { return MediaTrackKind::kAudio; }
};

int main(int argc, char* argv[]) {

	livekit::core::Init();

	std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	                    "eyJleHAiOjE3NDgxODk0NzIsImlzcyI6ImtleTEiLCJuYW1lIjoidXNlcjEiLCJuYmYiOjE3ND"
	                    "gxMDMwNzIsInN1YiI6InVzZXIxIiwidmlkZW8iOnsicm9vbSI6InRlc3QiLCJyb29tSm9pbiI6"
	                    "dHJ1ZX19.yoXRnCof7a4DctSflPA6LsYK7gC589JUWkrI8OWn5WM";
	auto room_options = livekit::core::default_room_connect_options();
	auto room = livekit::core::CreateRoom();

	auto event = std::make_shared<RoomEvent>();
	room->AddEventListener(event.get());

	room->Connect("http://localhost:7880/rtc", token, room_options);

	auto i = 0;
	while (!room->IsConnected() && i < 5) {
		std::this_thread::sleep_for(std::chrono::seconds(10));
		i++;
	}

	if (!room->IsConnected()) {
		std::cout << "Failed to connect to room" << std::endl;
		room->RemoveEventListener();
		return -1;
	}

	auto local_participant = room->GetLocalParticipant();
	if (!local_participant) {
		std::cout << "Failed to get local participant" << std::endl;
		room->RemoveEventListener();
		return -1;
	}

	while (true) {
	}

	room->RemoveEventListener();

	livekit::core::Destroy();
	return 0;
}
