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
#include "option/media_option.h"
#include "track/audio_frame.h"
#include "track/subscription_error.h"
#include "track/video_frame.h"

#include <map>
#include <string>
#include <vector>

namespace livekit {
namespace core {

class RemoteParticipantInterface;
class RemoteTrackInterface;
class ParticipantInterface;
class TrackPublicationInterface;

// Values intentionally match livekit.protocol.DisconnectReason so applications can preserve the
// full server-provided reason without depending on generated protobuf headers.
enum class DisconnectReason : int {
	Unknown = 0,
	ClientInitiated = 1,
	DuplicateIdentity = 2,
	ServerShutdown = 3,
	ParticipantRemoved = 4,
	RoomDeleted = 5,
	StateMismatch = 6,
	JoinFailure = 7,
	Migration = 8,
	SignalClose = 9,
	RoomClosed = 10,
	UserUnavailable = 11,
	UserRejected = 12,
	SipTrunkFailure = 13,
	ConnectionTimeout = 14,
	MediaFailure = 15,
	AgentError = 16,
};

class RoomEventInterface {
public:
	virtual ~RoomEventInterface() {}

	virtual void OnConnected() = 0;
	virtual void OnReconnecting() {}
	virtual void OnReconnected() {}
	virtual void OnDisconnected() {}
	// The default forwards to the legacy no-argument callback, preserving existing listeners.
	virtual void OnDisconnected(DisconnectReason) { OnDisconnected(); }
	virtual void OnParticipantConnected(RemoteParticipantInterface*) {}
	virtual void OnParticipantDisconnected(RemoteParticipantInterface*) {}
	virtual void OnTrackPublished(TrackPublicationInterface*, RemoteParticipantInterface*) {}
	virtual void OnTrackUnpublished(TrackPublicationInterface*, RemoteParticipantInterface*) {}
	virtual void OnLocalTrackPublished(TrackPublicationInterface*, ParticipantInterface*) {}
	virtual void OnLocalTrackUnpublished(TrackPublicationInterface*, ParticipantInterface*) {}
	virtual void OnTrackMuted(TrackPublicationInterface*, ParticipantInterface*) {}
	virtual void OnTrackUnmuted(TrackPublicationInterface*, ParticipantInterface*) {}
	virtual void OnTrackSubscribed(RemoteTrackInterface*, RemoteParticipantInterface*) {}
	virtual void OnTrackSubscriptionPermissionChanged(TrackPublicationInterface*,
	                                                  RemoteParticipantInterface*, bool) {}
	virtual void OnActiveSpeakersChanged(const std::vector<ParticipantInterface*>&) {}
	virtual void OnParticipantMetadataChanged(const std::string&, ParticipantInterface*) {}
	virtual void OnParticipantNameChanged(const std::string&, ParticipantInterface*) {}
	virtual void OnParticipantAttributesChanged(const std::map<std::string, std::string>&,
	                                            ParticipantInterface*) {}
	virtual void OnConnectionQualityChanged(ConnectionQuality, ParticipantInterface*) {}
	virtual void OnRoomMetadataChanged(const std::string&) {}
	virtual void OnAudioFrame(RemoteTrackInterface*, RemoteParticipantInterface*,
	                          const AudioFrame&) {}
	virtual void OnVideoFrame(RemoteTrackInterface*, RemoteParticipantInterface*,
	                          const VideoFrame&) {}
	virtual void OnDataReceived(const DataReceivedEvent&) {}
	virtual void OnTextReceived(const TextReceivedEvent&) {}
	virtual void OnByteReceived(const ByteReceivedEvent&) {}
	virtual void OnFileReceived(const FileReceivedEvent&) {}
	// Kept at the end so extending the listener does not reorder existing callback slots.
	virtual void OnTrackSubscriptionFailed(const std::string&, RemoteParticipantInterface*,
	                                       SubscriptionError) {}
	virtual void OnTrackUnsubscribed(RemoteTrackInterface*, TrackPublicationInterface*,
	                                 RemoteParticipantInterface*) {}
	virtual void OnTrackStreamStateChanged(TrackPublicationInterface*, RemoteParticipantInterface*,
	                                       TrackStreamState) {}
	virtual void OnTrackSubscriptionStatusChanged(TrackPublicationInterface*,
	                                              RemoteParticipantInterface*,
	                                              TrackSubscriptionStatus) {}
	virtual void OnDataChannelBufferStatusChanged(const DataChannelBufferStatus&) {}
	virtual void OnSipDtmfReceived(const SipDtmfEvent&) {}
	virtual void OnChatMessageReceived(const ChatMessage&) {}
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_ROOM_EVENT_INTERFACE_H_
