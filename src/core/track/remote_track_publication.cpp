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

#include "remote_track_publication.h"

namespace livekit {
namespace core {

RemoteTrackPublication::RemoteTrackPublication(livekit::TrackInfo info, bool auto_subscribe,
                                               SubscriptionHandler subscription_handler,
                                               SettingsHandler settings_handler,
                                               StatusHandler status_handler)
    : TrackPublication(std::move(info), nullptr), subscription_desired_(auto_subscribe),
      subscription_handler_(std::move(subscription_handler)),
      settings_handler_(std::move(settings_handler)), status_handler_(std::move(status_handler)) {}

TrackSubscriptionStatus RemoteTrackPublication::SubscriptionStatus() {
	std::lock_guard<std::mutex> guard(remote_mutex_);
	if (!subscription_desired_) {
		return TrackSubscriptionStatus::Unsubscribed;
	}
	return track_attached_ ? TrackSubscriptionStatus::Subscribed : TrackSubscriptionStatus::Desired;
}

bool RemoteTrackPublication::SetSubscribed(bool subscribed) {
	SubscriptionHandler handler;
	TrackSubscriptionStatus previous;
	{
		std::lock_guard<std::mutex> guard(remote_mutex_);
		if (!subscription_handler_) {
			return false;
		}
		previous = !subscription_desired_ ? TrackSubscriptionStatus::Unsubscribed
		                                  : (track_attached_ ? TrackSubscriptionStatus::Subscribed
		                                                     : TrackSubscriptionStatus::Desired);
		handler = subscription_handler_;
	}
	if (!handler(Sid(), subscribed)) {
		return false;
	}
	TrackSubscriptionStatus current;
	StatusHandler status_handler;
	{
		std::lock_guard<std::mutex> guard(remote_mutex_);
		subscription_desired_ = subscribed;
		subscription_overridden_ = true;
		current = !subscription_desired_ ? TrackSubscriptionStatus::Unsubscribed
		                                 : (track_attached_ ? TrackSubscriptionStatus::Subscribed
		                                                    : TrackSubscriptionStatus::Desired);
		status_handler = status_handler_;
	}
	if (current != previous && status_handler) {
		status_handler(Sid(), current, previous);
	}
	return true;
}

RemoteTrackSettings RemoteTrackPublication::GetRemoteTrackSettings() {
	std::lock_guard<std::mutex> guard(remote_mutex_);
	return settings_;
}

bool RemoteTrackPublication::UpdateRemoteTrackSettings(const RemoteTrackSettings& settings) {
	SettingsHandler handler;
	{
		std::lock_guard<std::mutex> guard(remote_mutex_);
		if (!subscription_desired_ || !settings_handler_ || !IsValidSettings(Kind(), settings)) {
			return false;
		}
		if (settings_.enabled == settings.enabled &&
		    settings_.video_quality == settings.video_quality &&
		    settings_.video_dimensions.has_value() == settings.video_dimensions.has_value() &&
		    (!settings.video_dimensions.has_value() ||
		     (settings_.video_dimensions->width == settings.video_dimensions->width &&
		      settings_.video_dimensions->height == settings.video_dimensions->height)) &&
		    settings_.video_fps == settings.video_fps && settings_.priority == settings.priority) {
			return false;
		}
		handler = settings_handler_;
	}
	if (!handler(Sid(), settings)) {
		return false;
	}
	{
		std::lock_guard<std::mutex> guard(remote_mutex_);
		settings_ = settings;
		settings_overridden_ = true;
	}
	return true;
}

bool RemoteTrackPublication::SetTrackAttached(bool attached) {
	std::lock_guard<std::mutex> guard(remote_mutex_);
	if (track_attached_ == attached) {
		return false;
	}
	track_attached_ = attached;
	return true;
}

bool RemoteTrackPublication::ResendPreferences() {
	SubscriptionHandler subscription_handler;
	SettingsHandler settings_handler;
	bool subscription_overridden = false;
	bool settings_overridden = false;
	bool subscribed = true;
	RemoteTrackSettings settings;
	{
		std::lock_guard<std::mutex> guard(remote_mutex_);
		subscription_handler = subscription_handler_;
		settings_handler = settings_handler_;
		subscription_overridden = subscription_overridden_;
		settings_overridden = settings_overridden_;
		subscribed = subscription_desired_;
		settings = settings_;
	}
	bool success = true;
	if (subscription_overridden) {
		success = subscription_handler && subscription_handler(Sid(), subscribed) && success;
	}
	if (settings_overridden && subscribed) {
		success = settings_handler && settings_handler(Sid(), settings) && success;
	}
	return success;
}

bool RemoteTrackPublication::IsValidSettings(TrackKind kind, const RemoteTrackSettings& settings) {
	if (settings.video_quality.has_value() && settings.video_dimensions.has_value()) {
		return false;
	}
	if (settings.video_dimensions.has_value() &&
	    (settings.video_dimensions->width == 0 || settings.video_dimensions->height == 0)) {
		return false;
	}
	if (kind != TrackKind::Video &&
	    (settings.video_quality.has_value() || settings.video_dimensions.has_value() ||
	     settings.video_fps != 0)) {
		return false;
	}
	return true;
}

} // namespace core
} // namespace livekit
