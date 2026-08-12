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

#pragma once

#ifndef _LKC_CORE_ROOM_EVENT_INTERFACE_H_
#define _LKC_CORE_ROOM_EVENT_INTERFACE_H_

#include "data_packet.h"
#include "track/audio_frame.h"
#include "track/video_frame.h"

namespace livekit {
namespace core {

class RemoteParticipantInterface;
class RemoteTrackInterface;

class RoomEventInterface {
public:
	virtual ~RoomEventInterface() {}

	virtual void OnConnected() = 0;
	virtual void OnDisconnected() {}
	virtual void OnTrackSubscribed(RemoteTrackInterface*, RemoteParticipantInterface*) {}
	virtual void OnAudioFrame(RemoteTrackInterface*, RemoteParticipantInterface*,
	                          const AudioFrame&) {}
	virtual void OnVideoFrame(RemoteTrackInterface*, RemoteParticipantInterface*,
	                          const VideoFrame&) {}
	virtual void OnDataReceived(const DataReceivedEvent&) {}
	virtual void OnFileReceived(const FileReceivedEvent&) {}
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_ROOM_EVENT_INTERFACE_H_
