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

#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/track_factory.h"
#include <chrono>
#include <iostream>
#include <thread>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

class RoomEvent : public livekit::core::RoomEventInterface {
public:
	RoomEvent() = default;
	virtual ~RoomEvent() = default;
	virtual void OnConnected() override {
		std::cout << "Room connected" << std::endl;
		return;
	}
};

int main(int argc, char* argv[]) {

	livekit::core::Init();

	drwav wav;
	if (!drwav_init_file(&wav, "change-sophie.wav", NULL)) {
		std::cout << "Failed to open wav file" << std::endl;
		return -1;
	}

	std::cout << "wav file info: " << "[sampleRate=" << wav.sampleRate << "]"
	          << "[channels=" << wav.channels << "]" << std::endl;

	float* pSampleData = (float*)malloc(wav.totalPCMFrameCount * wav.channels * sizeof(float));
	drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, pSampleData);

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
		drwav_uninit(&wav);
		free(pSampleData);
		return -1;
	}

	auto local_participant = room->GetLocalParticipant();
	if (!local_participant) {
		std::cout << "Failed to get local participant" << std::endl;
		room->RemoveEventListener();
		drwav_uninit(&wav);
		free(pSampleData);
		return -1;
	}

	// publish audio
	livekit::core::TrackPublishOptions publish_options;
	publish_options.source = livekit::core::TrackSource::Microphone;

	livekit::core::AudioSourceOptions audio_source_options;

	auto audio_source =
	    livekit::core::CreateAudioSource(audio_source_options, wav.sampleRate, wav.channels, 0);

	auto audio_track = livekit::core::CreateLocalAudioTreack("file", audio_source);

	local_participant->PublishTrack(audio_track, publish_options);

	while (true) {
	}

	room->RemoveEventListener();

	livekit::core::Destroy();

	drwav_uninit(&wav);
	free(pSampleData);
	return 0;
}
