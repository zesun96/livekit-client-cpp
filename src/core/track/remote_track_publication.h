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

#ifndef _LKC_CORE_TRACK_REMOTE_TRACK_PUBLICATION_H_
#define _LKC_CORE_TRACK_REMOTE_TRACK_PUBLICATION_H_

#include "livekit/core/track/remote_track_publication_interface.h"
#include "track_publication.h"

#include <functional>
#include <mutex>

namespace livekit {
namespace core {

class RemoteTrackPublication : public TrackPublication, public RemoteTrackPublicationInterface {
public:
	using SubscriptionHandler = std::function<bool(const std::string&, bool)>;
	using SettingsHandler = std::function<bool(const std::string&, const RemoteTrackSettings&)>;
	using StatusHandler =
	    std::function<void(const std::string&, TrackSubscriptionStatus, TrackSubscriptionStatus)>;

	RemoteTrackPublication(livekit::TrackInfo info, bool auto_subscribe,
	                       SubscriptionHandler subscription_handler = {},
	                       SettingsHandler settings_handler = {},
	                       StatusHandler status_handler = {});
	virtual ~RemoteTrackPublication() override = default;

	TrackSubscriptionStatus SubscriptionStatus() override;
	bool SetSubscribed(bool subscribed) override;
	RemoteTrackSettings GetRemoteTrackSettings() override;
	bool UpdateRemoteTrackSettings(const RemoteTrackSettings& settings) override;

	bool SetTrackAttached(bool attached);
	bool ResendPreferences();

private:
	static bool IsValidSettings(TrackKind kind, const RemoteTrackSettings& settings);

	mutable std::mutex remote_mutex_;
	bool subscription_desired_ = true;
	bool subscription_overridden_ = false;
	bool track_attached_ = false;
	bool settings_overridden_ = false;
	RemoteTrackSettings settings_;
	SubscriptionHandler subscription_handler_;
	SettingsHandler settings_handler_;
	StatusHandler status_handler_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_REMOTE_TRACK_PUBLICATION_H_
