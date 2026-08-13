/**
 *
 * Copyright (c) 2024 sunze
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

#ifndef _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_
#define _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_

#include "livekit/core/data_packet.h"
#include "livekit/core/option/option.h"
#include "livekit/core/participant/participant_interface.h"
#include "livekit/core/rpc.h"
#include "livekit/core/subscription_permission.h"

#include "../track/audio_source_interface.h"
#include "../track/local_track_interface.h"
#include "../track/track_interface.h"
#include "../track/video_source_interface.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace livekit {

namespace core {

class LocalParticipantInterface : public virtual ParticipantInterface {
public:
	virtual ~LocalParticipantInterface() = default;

	virtual LocalTrackInterface* CreateLocalAudioTreack(std::string label,
	                                                    AudioSourceInterface* source) = 0;
	LocalTrackInterface* CreateLocalAudioTrack(std::string label, AudioSourceInterface* source) {
		return CreateLocalAudioTreack(std::move(label), source);
	}
	std::unique_ptr<LocalTrackInterface> CreateLocalAudioTrackUnique(std::string label,
	                                                                 AudioSourceInterface* source) {
		return std::unique_ptr<LocalTrackInterface>(
		    CreateLocalAudioTreack(std::move(label), source));
	}

	virtual LocalTrackInterface* CreateLocalVideoTrack(std::string label,
	                                                   VideoSourceInterface* source) = 0;
	std::unique_ptr<LocalTrackInterface> CreateLocalVideoTrackUnique(std::string label,
	                                                                 VideoSourceInterface* source) {
		return std::unique_ptr<LocalTrackInterface>(
		    CreateLocalVideoTrack(std::move(label), source));
	}

	virtual bool PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) = 0;
	// Removes a published track from the publisher PeerConnection. The track object remains owned
	// by the caller and may be published again. When stop_on_unpublish is true its media source is
	// disabled after the sender has been removed.
	virtual bool UnpublishTrack(LocalTrackInterface* track, bool stop_on_unpublish = true) = 0;
	virtual std::size_t UnpublishTracks(const std::vector<LocalTrackInterface*>& tracks,
	                                    bool stop_on_unpublish = true) = 0;
	virtual bool RepublishAllTracks() = 0;
	virtual bool SetMetadata(const std::string&) { return false; }
	virtual bool SetName(const std::string&) { return false; }
	virtual bool SetAttributes(const std::map<std::string, std::string>&) { return false; }
	virtual bool PublishData(const std::vector<uint8_t>& data, DataPublishOptions options = {}) = 0;
	virtual bool SendText(const std::string& text, TextSendOptions options = {}) = 0;
	virtual bool SendBytes(const std::vector<uint8_t>& data, ByteSendOptions options = {}) = 0;
	virtual bool SendFile(const std::string& path, FileSendOptions options = {}) = 0;
	virtual RpcResult PerformRpc(const PerformRpcParams& params) = 0;
	// Controls who may subscribe to locally published tracks. When all_participants_allowed is
	// false, omitted participants cannot subscribe to any track.
	virtual bool SetTrackSubscriptionPermissions(
	    bool all_participants_allowed,
	    const std::vector<ParticipantTrackPermission>& participant_permissions = {}) = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_
