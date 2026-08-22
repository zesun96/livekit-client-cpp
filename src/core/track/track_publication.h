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

#ifndef _LKC_CORE_TRACK_TRACK_PUBLICATION_H_
#define _LKC_CORE_TRACK_TRACK_PUBLICATION_H_

#include "livekit/core/track/track_publication_interface.h"
#include "track.h"

#include "livekit_models.pb.h"

#include <mutex>

namespace livekit {
namespace core {

class TrackPublication : public TrackPublicationInterface {
public:
	TrackPublication(livekit::TrackInfo info, ::livekit::core::Track* track);
	virtual ~TrackPublication() = default;

	virtual std::string Sid() override;
	std::string Name() override;
	TrackKind Kind() override;
	TrackSource Source() override;
	TrackDimensions Dimensions() override;
	std::string MimeType() override;
	bool IsMuted() override;
	bool IsSimulcasted() override;
	TrackInterface* Track() override;
	bool IsSubscriptionAllowed() override;
	std::optional<SubscriptionError> LastSubscriptionError() override;
	EncryptionType Encryption() override;

	void UpdateInfo(livekit::TrackInfo info);
	livekit::TrackInfo Info() const;
	void SetTrack(::livekit::core::Track* track);
	void SetMuted(bool muted);
	bool SetSubscriptionAllowed(bool allowed);
	void SetSubscriptionError(std::optional<SubscriptionError> error);

private:
	mutable std::mutex mutex_;
	livekit::TrackInfo info_;
	TrackKind kind_;
	TrackSource source_;
	TrackDimensions dimensions_;
	std::string track_sid_;
	std::string track_name_;
	std::string mime_type_;
	bool simulcasted_;
	bool muted_;
	bool subscription_allowed_ = true;
	std::optional<SubscriptionError> subscription_error_;

	::livekit::core::Track* track_;
};
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_TRACK_PUBLICATION_H_
