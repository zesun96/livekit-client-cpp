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

#ifndef _LKC_CORE_TRACK_TRACK_PUBLICATION_INTERFACE_H_
#define _LKC_CORE_TRACK_TRACK_PUBLICATION_INTERFACE_H_

#include "livekit/core/option/e2ee_option.h"
#include "livekit/core/option/media_option.h"
#include "livekit/core/track/subscription_error.h"

#include <optional>
#include <string>

namespace livekit {
namespace core {

class TrackInterface;

class TrackPublicationInterface {
public:
	virtual ~TrackPublicationInterface() = default;

	virtual std::string Sid() = 0;
	virtual std::string Name() { return {}; }
	virtual TrackKind Kind() { return TrackKind::Unknown; }
	virtual TrackSource Source() { return TrackSource::Unknown; }
	virtual TrackDimensions Dimensions() { return {}; }
	virtual std::string MimeType() { return {}; }
	virtual bool IsMuted() { return false; }
	virtual bool IsSimulcasted() { return false; }
	virtual TrackInterface* Track() { return nullptr; }
	// For remote publications, reports the publisher's permission for this local participant.
	virtual bool IsSubscriptionAllowed() { return true; }
	// The most recent server-reported failure, if a subscription attempt failed.
	virtual std::optional<SubscriptionError> LastSubscriptionError() { return std::nullopt; }
	// Remote publications override these methods. Local publications keep the safe defaults.
	virtual TrackSubscriptionStatus SubscriptionStatus() {
		return TrackSubscriptionStatus::Unsubscribed;
	}
	virtual bool SetSubscribed(bool) { return false; }
	virtual RemoteTrackSettings GetRemoteTrackSettings() { return {}; }
	virtual bool UpdateRemoteTrackSettings(const RemoteTrackSettings&) { return false; }
	// Appended for ABI compatibility. Reports the encryption declared in TrackInfo.
	virtual EncryptionType Encryption() { return EncryptionType::None; }
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_TRACK_PUBLICATION_INTERFACE_H_
