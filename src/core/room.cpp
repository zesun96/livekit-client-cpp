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

#include "room.h"
#include "../capture/audio_capture_adapter.h"
#include "detail/converted_proto.h"
#include "detail/rtc_engine.h"
#include "e2ee/e2ee_manager_internal.h"
#include "track/audio_track.h"
#include "track/remote_audio_track.h"
#include "track/remote_track_publication.h"
#include "track/remote_video_track.h"
#include "track/track_publication.h"
#include "track/video_track.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace {
constexpr uint64_t kMaximumBufferedDataStreamSize = 64ULL * 1024 * 1024;

bool IsSupportedCompression(livekit::DataStream_CompressionType compression) {
	return compression == livekit::DataStream_CompressionType_NONE ||
	       compression == livekit::DataStream_CompressionType_DEFLATE_RAW;
}

bool DecodeInlineContent(const livekit::DataStream_Header& header, std::vector<uint8_t>& output) {
	if (header.compression() == livekit::DataStream_CompressionType_NONE) {
		output.assign(header.inline_content().begin(), header.inline_content().end());
		return output.size() <= kMaximumBufferedDataStreamSize;
	}
	livekit::core::detail::InflateRawStream inflater(kMaximumBufferedDataStreamSize);
	return inflater.IsValid() &&
	       inflater.Write(reinterpret_cast<const uint8_t*>(header.inline_content().data()),
	                      header.inline_content().size(), output) &&
	       inflater.Finished();
}

int64_t CurrentTimestampMilliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

static livekit::core::EngineOptions make_engine_config(livekit::core::RoomOptions room_options) {
	livekit::core::EngineOptions engine_options;

	engine_options.join_retries = room_options.join_retries;
	engine_options.reconnect_timeout = room_options.reconnect_timeout;
	engine_options.reconnect_policy = room_options.reconnect_policy != nullptr
	                                      ? std::move(room_options.reconnect_policy)
	                                      : livekit::core::CreateDefaultReconnectPolicy();
	engine_options.rtc_config.ice_servers = room_options.rtc_config.ice_servers;
	engine_options.rtc_config.continual_gathering_policy =
	    room_options.rtc_config.continual_gathering_policy;
	engine_options.rtc_config.ice_transport_type = room_options.rtc_config.ice_transport_type;
	engine_options.signal_options.reconnect = false;
	engine_options.signal_options.adaptive_stream = room_options.adaptive_stream;
	engine_options.signal_options.auto_subscribe = room_options.auto_subscribe;
	engine_options.signal_options.sdk_options.sdk = room_options.sdk_options.sdk;
	engine_options.signal_options.sdk_options.sdk_version = room_options.sdk_options.sdk_version;
	return engine_options;
}

static livekit::core::ConnectionQuality
from_connection_quality(livekit::ConnectionQuality quality) {
	switch (quality) {
	case livekit::ConnectionQuality::POOR:
		return livekit::core::ConnectionQuality::Poor;
	case livekit::ConnectionQuality::GOOD:
		return livekit::core::ConnectionQuality::Good;
	case livekit::ConnectionQuality::EXCELLENT:
		return livekit::core::ConnectionQuality::Excellent;
	case livekit::ConnectionQuality::LOST:
		return livekit::core::ConnectionQuality::Lost;
	default:
		return livekit::core::ConnectionQuality::Unknown;
	}
}

static livekit::core::VideoQuality from_video_quality(livekit::VideoQuality quality) {
	switch (quality) {
	case livekit::VideoQuality::LOW:
		return livekit::core::VideoQuality::Low;
	case livekit::VideoQuality::MEDIUM:
		return livekit::core::VideoQuality::Medium;
	case livekit::VideoQuality::HIGH:
		return livekit::core::VideoQuality::High;
	case livekit::VideoQuality::OFF:
	default:
		return livekit::core::VideoQuality::Off;
	}
}

static livekit::core::DisconnectReason from_disconnect_reason(livekit::DisconnectReason reason) {
	const auto value = static_cast<int>(reason);
	if (value < static_cast<int>(livekit::DisconnectReason::UNKNOWN_REASON) ||
	    value > static_cast<int>(livekit::DisconnectReason::AGENT_ERROR)) {
		return livekit::core::DisconnectReason::Unknown;
	}
	return static_cast<livekit::core::DisconnectReason>(value);
}
} // namespace

namespace livekit {
namespace core {
Room::Room(RoomOptions options) : options_(options) {
	rtc_engine_ = std::make_unique<RtcEngine>();
	rtc_engine_->SetRoomObserver(this);
	local_participant_ = std::make_unique<LocalParticipant>("", "", EncryptionType::None,
	                                                        rtc_engine_.get(), options_);
	ConfigureE2ee(options_.e2ee);
}

Room::~Room() {
	if (rtc_engine_) {
		rtc_engine_->SetE2EEManager(nullptr, {});
	}
	if (e2ee_manager_) {
		E2EEManagerNativeAccess::SetEnabledCallback(*e2ee_manager_, {});
		e2ee_manager_->SetStateCallback({});
		E2EEManagerNativeAccess::DetachAll(*e2ee_manager_);
	}
	std::map<std::string, std::shared_ptr<RemoteTrack>> detached_tracks;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		detached_tracks.swap(remote_tracks_);
		pending_media_tracks_.clear();
	}
	detached_tracks.clear();
	if (rtc_engine_) {
		rtc_engine_->SetRoomObserver(nullptr);
		rtc_engine_->Disconnect();
	}
}

bool Room::RegisterRpcMethod(std::string method, RpcHandler handler) {
	return rtc_engine_ != nullptr &&
	       rtc_engine_->RegisterRpcMethod(std::move(method), std::move(handler));
}

bool Room::UnregisterRpcMethod(const std::string& method) {
	return rtc_engine_ != nullptr && rtc_engine_->UnregisterRpcMethod(method);
}

bool Room::RegisterTextStreamHandler(std::string topic, TextStreamHandler handler) {
	if (!handler) {
		return false;
	}
	std::lock_guard<std::mutex> guard(stream_handlers_mutex_);
	return text_stream_handlers_.emplace(std::move(topic), std::move(handler)).second;
}

bool Room::UnregisterTextStreamHandler(const std::string& topic) {
	std::lock_guard<std::mutex> guard(stream_handlers_mutex_);
	return text_stream_handlers_.erase(topic) != 0;
}

bool Room::RegisterByteStreamHandler(std::string topic, ByteStreamHandler handler) {
	if (!handler) {
		return false;
	}
	std::lock_guard<std::mutex> guard(stream_handlers_mutex_);
	return byte_stream_handlers_.emplace(std::move(topic), std::move(handler)).second;
}

bool Room::UnregisterByteStreamHandler(const std::string& topic) {
	std::lock_guard<std::mutex> guard(stream_handlers_mutex_);
	return byte_stream_handlers_.erase(topic) != 0;
}

DataTrackError Room::StoreDataTrackSchema(DataTrackSchema schema) {
	if (!IsConnected() || rtc_engine_ == nullptr) {
		return {DataTrackErrorCode::Disconnected, "room is disconnected"};
	}
	auto result = rtc_engine_->StoreDataTrackSchema(schema);
	if (result) {
		return result;
	}
	auto identity = local_participant_->Identity();
	auto key = std::make_tuple(identity, schema.id.name, schema.id.encoding.kind,
	                           schema.id.encoding.custom);
	std::lock_guard<std::mutex> guard(data_track_schema_cache_mutex_);
	data_track_schema_cache_[std::move(key)] = std::move(schema);
	return {};
}

DataTrackSchemaResult Room::GetDataTrackSchema(std::string participant_identity,
                                               DataTrackSchemaId schema_id) {
	auto key = std::make_tuple(participant_identity, schema_id.name, schema_id.encoding.kind,
	                           schema_id.encoding.custom);
	{
		std::lock_guard<std::mutex> guard(data_track_schema_cache_mutex_);
		auto found = data_track_schema_cache_.find(key);
		if (found != data_track_schema_cache_.end()) {
			return {found->second, {}};
		}
	}
	if (!IsConnected() || rtc_engine_ == nullptr) {
		return {{}, {DataTrackErrorCode::Disconnected, "room is disconnected"}};
	}
	auto result = rtc_engine_->GetDataTrackSchema(participant_identity, schema_id);
	if (result) {
		std::lock_guard<std::mutex> guard(data_track_schema_cache_mutex_);
		data_track_schema_cache_[std::move(key)] = *result.schema;
	}
	return result;
}

bool Room::Connect(std::string url, std::string token, RoomConnectOptions opts) {
	return ConnectInternal(std::move(url), std::move(token), std::move(opts), nullptr, {});
}

bool Room::Connect(std::shared_ptr<TokenSourceInterface> source,
                   TokenSourceFetchOptions source_options, RoomConnectOptions opts) {
	if (!source) {
		return false;
	}
	auto result = source->Fetch(source_options, false);
	if (!result) {
		return false;
	}
	return ConnectInternal(std::move(result.response.server_url),
	                       std::move(result.response.participant_token), std::move(opts),
	                       std::move(source), std::move(source_options));
}

bool Room::ConnectInternal(std::string url, std::string token, RoomConnectOptions opts,
                           std::shared_ptr<TokenSourceInterface> source,
                           TokenSourceFetchOptions source_options) {
	auto expected = state_.load();
	if (expected != RoomState::Disconnected && expected != RoomState::Failed) {
		return false;
	}
	if (!state_.compare_exchange_strong(expected, RoomState::Connecting)) {
		return false;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnConnectionStateChanged(RoomState::Connecting);
	}
	disconnected_event_emitted_ = false;
	full_reconnect_prepared_ = false;
	disconnect_reason_ = DisconnectReason::Unknown;
	{
		std::lock_guard<std::mutex> guard(data_track_schema_cache_mutex_);
		data_track_schema_cache_.clear();
	}
	options_ = opts;
	local_participant_->UpdateRoomOptions(opts);

	try {
		ConfigureE2ee(opts.e2ee);
		EngineOptions engine_options = make_engine_config(opts);
		engine_options.token_source = std::move(source);
		engine_options.token_source_options = std::move(source_options);
		livekit::JoinResponse join_response = rtc_engine_->Connect(url, token, engine_options);
		if (!join_response.has_room()) {
			rtc_engine_->Disconnect();
			SetState(RoomState::Failed);
			disconnect_reason_ = DisconnectReason::JoinFailure;
			return false;
		}
		ApplyJoinResponse(join_response, false);
	} catch (...) {
		SetState(RoomState::Failed);
		disconnect_reason_ = DisconnectReason::JoinFailure;
		throw;
	}

	return true;
}

void Room::AddEventListener(RoomEventInterface* listener) {
	event_listener_.store(listener);
	local_participant_->SetEventListener(listener);
}

void Room::RemoveEventListener() {
	local_participant_->SetEventListener(nullptr);
	event_listener_.store(nullptr);
}

bool Room::IsConnected() { return state_.load() == RoomState::Connected; }

bool Room::SetAudioOutputDevice(std::string device_id) {
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	return audio_device && audio_device->SetPlayoutDeviceId(device_id);
}

std::string Room::AudioOutputDevice() const {
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	return audio_device ? audio_device->PlayoutDeviceId() : std::string{};
}

bool Room::SetSpeakerVolume(float volume) {
	if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
		return false;
	}
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	return audio_device &&
	       audio_device->SetSpeakerVolume(static_cast<std::uint32_t>(volume * 255.0F + 0.5F)) == 0;
}

float Room::SpeakerVolume() const {
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	std::uint32_t volume = 255;
	return audio_device && audio_device->SpeakerVolume(&volume) == 0
	           ? static_cast<float>(volume) / 255.0F
	           : 1.0F;
}

bool Room::SetSpeakerMuted(bool muted) {
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	return audio_device && audio_device->SetSpeakerMute(muted) == 0;
}

bool Room::SpeakerMuted() const {
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	bool muted = false;
	return audio_device && audio_device->SpeakerMute(&muted) == 0 && muted;
}

AudioPlaybackStats Room::GetAudioPlaybackStats() const {
	AudioPlaybackStats result;
	auto audio_device = rtc_engine_ ? rtc_engine_->GetAudioDevice() : nullptr;
	if (!audio_device) {
		return result;
	}
	const auto stats = audio_device->PlaybackStats();
	result.queued_frames = stats.queued_frames;
	result.played_frames = stats.played_frames;
	result.dropped_frames = stats.dropped_frames;
	result.underrun_frames = stats.underrun_frames;
	result.buffered_duration_ms = stats.buffered_duration_ms;
	result.device_latency_ms = stats.device_latency_ms;
	result.estimated_delay_ms = stats.estimated_delay_ms;
	return result;
}

RoomInterface::RoomState Room::State() const { return state_.load(); }

DisconnectReason Room::LastDisconnectReason() const { return disconnect_reason_.load(); }

std::string Room::Sid() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.sid();
}

std::string Room::Name() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.name();
}

std::string Room::Metadata() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.metadata();
}

bool Room::IsRecording() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.active_recording();
}

bool Room::Disconnect() {
	auto state = state_.load();
	if (state == RoomState::Disconnecting || state == RoomState::Disconnected) {
		return false;
	}
	SetState(RoomState::Disconnecting);
	if (e2ee_manager_) {
		E2EEManagerNativeAccess::DetachAll(*e2ee_manager_);
	}

	std::map<std::string, std::shared_ptr<RemoteTrack>> detached_tracks;
	std::vector<std::shared_ptr<RemoteParticipant>> detached_participants;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		detached_tracks.swap(remote_tracks_);
		pending_media_tracks_.clear();
		for (const auto& [sid, participant] : remote_participants_) {
			detached_participants.push_back(participant);
		}
		remote_participants_.clear();
	}
	for (const auto& participant : detached_participants) {
		for (const auto& [sid, interface] : participant->DataTracksSnapshot()) {
			if (auto* track = dynamic_cast<RemoteDataTrack*>(interface.get())) {
				track->MarkUnpublished();
			}
		}
	}
	detached_tracks.clear();
	rtc_engine_->Disconnect();
	{
		std::lock_guard<std::mutex> guard(data_track_schema_cache_mutex_);
		data_track_schema_cache_.clear();
	}
	FailIncomingDataStreams("room disconnected");
	SetState(RoomState::Disconnected);
	NotifyDisconnectedOnce(DisconnectReason::ClientInitiated);
	return true;
}

LocalParticipantInterface* Room::GetLocalParticipant() { return this->local_participant_.get(); }

E2EEManager* Room::GetE2EEManager() { return e2ee_manager_.get(); }

void Room::ConfigureE2ee(const std::optional<E2eeOptions>& options) {
	std::unique_ptr<E2EEManager> replacement;
	EncryptionType encryption_type = EncryptionType::None;
	if (options && options->encryption_type != EncryptionType::None) {
		replacement = std::make_unique<E2EEManager>(*options);
		replacement->SetStateCallback([this](const EncryptionStateEvent& event) {
			if (auto* listener = event_listener_.load()) {
				listener->OnEncryptionStateChanged(event);
			}
		});
		E2EEManagerNativeAccess::SetEnabledCallback(*replacement, [this](bool enabled) {
			local_participant_->SetE2EEManager(e2ee_manager_.get(), enabled ? EncryptionType::Gcm
			                                                                : EncryptionType::None);
			return !IsConnected() || local_participant_->RepublishAllTracks();
		});
		encryption_type = options->enabled ? options->encryption_type : EncryptionType::None;
	}
	if (e2ee_manager_) {
		E2EEManagerNativeAccess::SetEnabledCallback(*e2ee_manager_, {});
		e2ee_manager_->SetStateCallback({});
		E2EEManagerNativeAccess::DetachAll(*e2ee_manager_);
	}
	e2ee_manager_ = std::move(replacement);
	local_participant_->SetE2EEManager(e2ee_manager_.get(), encryption_type);
	rtc_engine_->SetE2EEManager(e2ee_manager_.get(), local_participant_->Identity());
}

std::vector<RemoteParticipantInterface*> Room::GetRemoteParticipants() {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	std::vector<RemoteParticipantInterface*> participants;
	for (auto& participant : this->remote_participants_) {
		participants.push_back(participant.second.get());
	}
	return participants;
}

std::vector<RemoteParticipantSnapshot> Room::GetRemoteParticipantSnapshots() const {
	std::vector<std::shared_ptr<RemoteParticipant>> participants;
	std::map<std::string, std::shared_ptr<RemoteTrack>> tracks;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participants.reserve(remote_participants_.size());
		for (const auto& [sid, participant] : remote_participants_) {
			participants.push_back(participant);
		}
		tracks = remote_tracks_;
	}

	std::vector<RemoteParticipantSnapshot> result;
	result.reserve(participants.size());
	for (const auto& participant : participants) {
		RemoteParticipantSnapshot participant_snapshot;
		participant_snapshot.sid = participant->Sid();
		participant_snapshot.identity = participant->Identity();
		participant_snapshot.name = participant->Name();
		participant_snapshot.metadata = participant->Metadata();
		participant_snapshot.attributes = participant->Attributes();
		participant_snapshot.audio_level = participant->AudioLevel();
		participant_snapshot.connection_quality = participant->GetConnectionQuality();
		participant_snapshot.speaking = participant->IsSpeaking();
		participant_snapshot.permissions = participant->Permissions();

		auto publications = participant->TrackPublicationsSnapshot();
		participant_snapshot.publications.reserve(publications.size());
		for (const auto& [sid, publication] : publications) {
			RemoteTrackPublicationSnapshot publication_snapshot;
			publication_snapshot.sid = publication->Sid();
			publication_snapshot.name = publication->Name();
			publication_snapshot.mime_type = publication->MimeType();
			publication_snapshot.kind = publication->Kind();
			publication_snapshot.source = publication->Source();
			publication_snapshot.dimensions = publication->Dimensions();
			publication_snapshot.muted = publication->IsMuted();
			publication_snapshot.simulcasted = publication->IsSimulcasted();
			publication_snapshot.subscription_allowed = publication->IsSubscriptionAllowed();
			publication_snapshot.subscription_status = publication->SubscriptionStatus();
			publication_snapshot.subscription_error = publication->LastSubscriptionError();
			const auto subscribed_track = tracks.find(publication_snapshot.sid);
			if (subscribed_track != tracks.end()) {
				auto* track = subscribed_track->second.get();
				RemoteTrackSnapshot track_snapshot;
				track_snapshot.sid = track->Sid();
				track_snapshot.name = track->Name();
				track_snapshot.kind = track->Kind();
				track_snapshot.source = track->Source();
				track_snapshot.stream_state = track->StreamState();
				track_snapshot.dimensions = track->Dimensions();
				track_snapshot.enabled = track->IsEnabled();
				if (track_snapshot.name.empty()) {
					track_snapshot.name = publication_snapshot.name;
				}
				if (track_snapshot.kind == TrackKind::Unknown) {
					track_snapshot.kind = publication_snapshot.kind;
				}
				if (track_snapshot.source == TrackSource::Unknown) {
					track_snapshot.source = publication_snapshot.source;
				}
				if (track_snapshot.dimensions.width == 0 || track_snapshot.dimensions.height == 0) {
					track_snapshot.dimensions = publication_snapshot.dimensions;
				}
				publication_snapshot.subscribed_track = std::move(track_snapshot);
			}
			participant_snapshot.publications.push_back(std::move(publication_snapshot));
		}
		result.push_back(std::move(participant_snapshot));
	}
	return result;
}

std::vector<RemoteDataTrackSnapshot> Room::GetRemoteDataTrackSnapshots() const {
	std::vector<std::shared_ptr<RemoteParticipant>> participants;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participants.reserve(remote_participants_.size());
		for (const auto& [sid, participant] : remote_participants_) {
			participants.push_back(participant);
		}
	}

	std::vector<RemoteDataTrackSnapshot> result;
	for (const auto& participant : participants) {
		for (const auto& [sid, track] : participant->DataTracksSnapshot()) {
			if (auto remote = std::dynamic_pointer_cast<RemoteDataTrackInterface>(track)) {
				result.push_back(
				    {remote->Info(), remote->PublisherIdentity(), remote->IsPublished()});
			}
		}
	}
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
		return std::tie(left.publisher_identity, left.info.sid) <
		       std::tie(right.publisher_identity, right.info.sid);
	});
	return result;
}

std::shared_ptr<RemoteDataTrackInterface> Room::GetRemoteDataTrack(std::string participant_identity,
                                                                   std::string track_sid) {
	std::shared_ptr<RemoteParticipant> participant;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		const auto found = std::find_if(remote_participants_.begin(), remote_participants_.end(),
		                                [&participant_identity](const auto& entry) {
			                                return entry.second->Identity() == participant_identity;
		                                });
		if (found == remote_participants_.end()) {
			return nullptr;
		}
		participant = found->second;
	}
	const auto tracks = participant->DataTracksSnapshot();
	const auto found = tracks.find(track_sid);
	return found != tracks.end()
	           ? std::dynamic_pointer_cast<RemoteDataTrackInterface>(found->second)
	           : nullptr;
}

RemoteParticipantInterface* Room::GetRemoteParticipantBySid(std::string sid) {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	auto participant = remote_participants_.find(sid);
	if (participant != remote_participants_.end()) {
		return participant->second.get();
	}
	return nullptr;
}

RemoteParticipantInterface* Room::GetRemoteParticipantByName(std::string name) {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	for (auto& participant : this->remote_participants_) {
		if (participant.second->Name() == name) {
			return participant.second.get();
		}
	}
	return nullptr;
}

std::vector<ParticipantInterface*> Room::Participants() {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	std::vector<ParticipantInterface*> participants;
	participants.push_back(this->local_participant_.get());
	for (auto& participant : this->remote_participants_) {
		participants.push_back(participant.second.get());
	}
	return participants;
}

ParticipantInterface* Room::GetParticipantBySid(std::string sid) {
	if (this->local_participant_->Sid() == sid) {
		return this->local_participant_.get();
	}
	return GetRemoteParticipantBySid(std::move(sid));
}

ParticipantInterface* Room::GetParticipantByName(std::string name) {
	if (this->local_participant_->Name() == name) {
		return this->local_participant_.get();
	}
	return GetRemoteParticipantByName(std::move(name));
}

bool Room::SetLocalTrackMutedInternal(std::string track_sid, bool muted) {
	if (track_sid.empty()) {
		return false;
	}
	auto publications = local_participant_->TrackPublicationsSnapshot();
	auto found = publications.find(track_sid);
	if (found == publications.end() || !rtc_engine_->SetTrackMuted(track_sid, muted)) {
		return false;
	}
	if (auto* local_track = dynamic_cast<LocalTrack*>(found->second->Track())) {
		if (local_track->media_track() != nullptr) {
			local_track->media_track()->set_enabled(!muted);
		}
	}
	if (auto* publication = dynamic_cast<TrackPublication*>(found->second.get())) {
		publication->SetMuted(muted);
	}
	if (auto* listener = event_listener_.load()) {
		if (muted) {
			listener->OnTrackMuted(found->second.get(), local_participant_.get());
		} else {
			listener->OnTrackUnmuted(found->second.get(), local_participant_.get());
		}
	}
	return true;
}

bool Room::SetRemoteTrackSubscribedInternal(std::string participant_sid, std::string track_sid,
                                            bool subscribed) {
	if (participant_sid.empty() || track_sid.empty()) {
		return false;
	}
	std::shared_ptr<TrackPublicationInterface> publication;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto participant = remote_participants_.find(participant_sid);
		if (participant == remote_participants_.end()) {
			return false;
		}
		auto publications = participant->second->TrackPublicationsSnapshot();
		auto found = publications.find(track_sid);
		if (found == publications.end()) {
			return false;
		}
		publication = found->second;
	}
	auto* remote = dynamic_cast<RemoteTrackPublication*>(publication.get());
	if (remote == nullptr || !remote->SetSubscribed(subscribed)) {
		return false;
	}
	return true;
}

bool Room::UpdateRemoteTrackSettingsInternal(std::string participant_sid, std::string track_sid,
                                             const RemoteTrackSettings& settings) {
	if (participant_sid.empty() || track_sid.empty()) {
		return false;
	}
	std::shared_ptr<TrackPublicationInterface> publication;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto participant = remote_participants_.find(participant_sid);
		if (participant == remote_participants_.end()) {
			return false;
		}
		auto publications = participant->second->TrackPublicationsSnapshot();
		auto found = publications.find(track_sid);
		if (found == publications.end()) {
			return false;
		}
		publication = found->second;
	}
	auto* remote = dynamic_cast<RemoteTrackPublication*>(publication.get());
	return remote != nullptr && remote->UpdateRemoteTrackSettings(settings);
}

bool Room::SendRemoteTrackSubscribed(const std::string& participant_sid,
                                     const std::string& track_sid, bool subscribed) {
	return rtc_engine_ != nullptr &&
	       rtc_engine_->SetTrackSubscribed(participant_sid, track_sid, subscribed);
}

bool Room::SendRemoteTrackSettings(const std::string& participant_sid, const std::string& track_sid,
                                   const RemoteTrackSettings& settings) {
	if (rtc_engine_ == nullptr) {
		return false;
	}
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto participant = remote_participants_.find(participant_sid);
		if (participant == remote_participants_.end() ||
		    !participant->second->HasTrackSid(track_sid)) {
			return false;
		}
	}
	return rtc_engine_->UpdateTrackSettings(track_sid, settings);
}

void Room::RemoteSubscriptionStatusChanged(const std::string& participant_sid,
                                           const std::string& track_sid,
                                           TrackSubscriptionStatus status) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<TrackPublicationInterface> publication;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto found_participant = remote_participants_.find(participant_sid);
		if (found_participant == remote_participants_.end()) {
			return;
		}
		participant = found_participant->second;
		auto publications = participant->TrackPublicationsSnapshot();
		auto found_publication = publications.find(track_sid);
		if (found_publication == publications.end()) {
			return;
		}
		publication = found_publication->second;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnTrackSubscriptionStatusChanged(publication.get(), participant.get(), status);
	}
}

RemoteParticipant::PublicationHandlers
Room::CreateRemotePublicationHandlers(const std::string& participant_sid) {
	RemoteParticipant::PublicationHandlers handlers;
	handlers.subscription = [this, participant_sid](const std::string& track_sid, bool subscribed) {
		return SendRemoteTrackSubscribed(participant_sid, track_sid, subscribed);
	};
	handlers.settings = [this, participant_sid](const std::string& track_sid,
	                                            const RemoteTrackSettings& settings) {
		return SendRemoteTrackSettings(participant_sid, track_sid, settings);
	};
	handlers.status = [this, participant_sid](const std::string& track_sid,
	                                          TrackSubscriptionStatus status,
	                                          TrackSubscriptionStatus) {
		RemoteSubscriptionStatusChanged(participant_sid, track_sid, status);
	};
	return handlers;
}

void Room::ResendRemoteTrackPreferences() {
	std::vector<std::shared_ptr<TrackPublicationInterface>> publications;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& [sid, participant] : remote_participants_) {
			auto snapshot = participant->TrackPublicationsSnapshot();
			for (auto& [track_sid, publication] : snapshot) {
				publications.push_back(std::move(publication));
			}
		}
	}
	for (const auto& publication : publications) {
		if (auto* remote = dynamic_cast<RemoteTrackPublication*>(publication.get())) {
			remote->ResendPreferences();
		}
	}
}

void Room::ResendRemoteDataTrackSubscriptions() {
	std::vector<std::shared_ptr<DataTrackInterface>> tracks;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& [sid, participant] : remote_participants_) {
			auto snapshot = participant->DataTracksSnapshot();
			for (auto& [track_sid, track] : snapshot) {
				tracks.push_back(std::move(track));
			}
		}
	}
	for (const auto& track : tracks) {
		if (auto* remote = dynamic_cast<RemoteDataTrack*>(track.get())) {
			remote->ResendSubscription();
		}
	}
}

bool Room::SimulateSignalDisconnectForTesting() {
	return rtc_engine_ != nullptr && rtc_engine_->SimulateSignalDisconnectForTesting();
}

bool Room::SimulateFullReconnectForTesting() {
	return rtc_engine_ != nullptr && rtc_engine_->SimulateFullReconnectForTesting();
}

bool Room::SimulateMediaFailureForTesting() {
	return rtc_engine_ != nullptr && rtc_engine_->SimulateMediaFailureForTesting();
}

std::string Room::AccessTokenForReconnectForTesting() const {
	return rtc_engine_ != nullptr ? rtc_engine_->AccessTokenForReconnect() : std::string{};
}

std::vector<RemoteParticipantSnapshot> RoomInterface::GetRemoteParticipantSnapshots() const {
	auto* room = dynamic_cast<const Room*>(this);
	return room != nullptr ? room->GetRemoteParticipantSnapshots()
	                       : std::vector<RemoteParticipantSnapshot>{};
}

std::vector<RemoteDataTrackSnapshot> RoomInterface::GetRemoteDataTrackSnapshots() const {
	auto* room = dynamic_cast<const Room*>(this);
	return room != nullptr ? room->GetRemoteDataTrackSnapshots()
	                       : std::vector<RemoteDataTrackSnapshot>{};
}

std::shared_ptr<RemoteDataTrackInterface>
RoomInterface::GetRemoteDataTrack(std::string participant_identity, std::string track_sid) {
	auto* room = dynamic_cast<Room*>(this);
	return room != nullptr
	           ? room->GetRemoteDataTrack(std::move(participant_identity), std::move(track_sid))
	           : nullptr;
}

RoomInterface::RoomState RoomInterface::State() const {
	auto* room = dynamic_cast<const Room*>(this);
	if (room != nullptr) {
		return room->State();
	}
	return const_cast<RoomInterface*>(this)->IsConnected() ? RoomState::Connected
	                                                       : RoomState::Disconnected;
}

DisconnectReason RoomInterface::LastDisconnectReason() const {
	auto* room = dynamic_cast<const Room*>(this);
	return room != nullptr ? room->LastDisconnectReason() : DisconnectReason::Unknown;
}

RemoteParticipantInterface* RoomInterface::GetRemoteParticipantByIdentity(std::string identity) {
	for (auto* participant : GetRemoteParticipants()) {
		if (participant != nullptr && participant->Identity() == identity) {
			return participant;
		}
	}
	return nullptr;
}

ParticipantInterface* RoomInterface::GetParticipantByIdentity(std::string identity) {
	auto* local = GetLocalParticipant();
	if (local != nullptr && local->Identity() == identity) {
		return local;
	}
	return GetRemoteParticipantByIdentity(std::move(identity));
}

bool RoomInterface::SetLocalTrackMuted(std::string track_sid, bool muted) {
	auto* room = dynamic_cast<Room*>(this);
	return room != nullptr && room->SetLocalTrackMutedInternal(std::move(track_sid), muted);
}

bool RoomInterface::SetRemoteTrackSubscribed(std::string participant_sid, std::string track_sid,
                                             bool subscribed) {
	auto* room = dynamic_cast<Room*>(this);
	return room != nullptr && room->SetRemoteTrackSubscribedInternal(
	                              std::move(participant_sid), std::move(track_sid), subscribed);
}

bool RoomInterface::UpdateRemoteTrackSettings(std::string participant_sid, std::string track_sid,
                                              const RemoteTrackSettings& settings) {
	auto* room = dynamic_cast<Room*>(this);
	return room != nullptr && room->UpdateRemoteTrackSettingsInternal(
	                              std::move(participant_sid), std::move(track_sid), settings);
}

void Room::ConnectedEvent(livekit::JoinResponse join_resp) {
	SetState(RoomState::Connected);
	local_participant_->ResendTrackSubscriptionPermissions();
	if (auto* listener = event_listener_.load()) {
		listener->OnConnected();
	}
}

void Room::ReconnectingEvent(bool full_reconnect) {
	if (full_reconnect && !full_reconnect_prepared_.exchange(true)) {
		if (e2ee_manager_) {
			E2EEManagerNativeAccess::DetachAll(*e2ee_manager_);
		}
		local_participant_->DetachTrackTransceiversForReconnect();
	}
	if (!TransitionState(RoomState::Connected, RoomState::Reconnecting)) {
		return;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnReconnecting();
	}
}

void Room::SignalResumedEvent() {
	std::vector<livekit::TrackPublishedResponse> published_tracks;
	for (auto* publication : local_participant_->GetTrackPublications()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication);
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrack*>(local_publication->Track())
		                        : nullptr;
		if (local_track == nullptr || local_track->media_track() == nullptr ||
		    local_track->media_track()->rtc_track() == nullptr) {
			continue;
		}
		auto* response = &published_tracks.emplace_back();
		response->set_cid(local_track->media_track()->rtc_track()->id());
		response->mutable_track()->CopyFrom(local_publication->Info());
	}
	rtc_engine_->SendSyncState(published_tracks);
}

void Room::ResumedEvent() {
	local_participant_->ResendTrackSubscriptionPermissions();
	ResendRemoteTrackPreferences();
	ResendRemoteDataTrackSubscriptions();
	if (!TransitionState(RoomState::Reconnecting, RoomState::Connected)) {
		return;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnReconnected();
	}
}

void Room::ReconnectedEvent(livekit::JoinResponse join_resp) {
	if (state_.load() != RoomState::Reconnecting || !join_resp.has_room()) {
		return;
	}
	ApplyJoinResponse(join_resp, true);
	local_participant_->ResendTrackSubscriptionPermissions();
	ResendRemoteTrackPreferences();
	local_participant_->RepublishAllTracksAfterReconnect();
	local_participant_->RepublishAllDataTracksAfterReconnect();
	ResendRemoteDataTrackSubscriptions();
	full_reconnect_prepared_ = false;
	SetState(RoomState::Connected);
	if (auto* listener = event_listener_.load()) {
		listener->OnReconnected();
	}
}

void Room::SignalDisconnectedEvent(livekit::DisconnectReason reason) {
	auto current = state_.load();
	while (current != RoomState::Disconnecting && current != RoomState::Disconnected &&
	       current != RoomState::Failed) {
		if (state_.compare_exchange_weak(current, RoomState::Failed)) {
			if (auto* listener = event_listener_.load()) {
				listener->OnConnectionStateChanged(RoomState::Failed);
			}
			break;
		}
	}
	if (current != RoomState::Disconnecting && current != RoomState::Disconnected) {
		{
			std::lock_guard<std::mutex> guard(data_track_schema_cache_mutex_);
			data_track_schema_cache_.clear();
		}
		FailIncomingDataStreams("connection closed");
		NotifyDisconnectedOnce(from_disconnect_reason(reason));
	}
}

void Room::FailIncomingDataStreams(const std::string& reason) {
	std::vector<std::pair<TextStreamHandler, TextStreamEvent>> text_events;
	std::vector<std::pair<ByteStreamHandler, ByteStreamEvent>> byte_events;
	{
		std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
		for (auto& [id, incoming] : incoming_texts_) {
			if (incoming.handler) {
				text_events.push_back(
				    {incoming.handler,
				     {incoming.info, DataStreamEventType::Failed, "", 0, reason}});
			}
		}
		for (auto& [id, incoming] : incoming_files_) {
			if (incoming.handler) {
				byte_events.push_back(
				    {incoming.handler,
				     {incoming.info, DataStreamEventType::Failed, {}, 0, reason}});
			}
		}
		incoming_texts_.clear();
		incoming_files_.clear();
	}
	{
		std::lock_guard<std::mutex> guard(transcription_mutex_);
		transcription_received_times_.clear();
	}
	for (const auto& [handler, event] : text_events) {
		handler(event);
	}
	for (const auto& [handler, event] : byte_events) {
		handler(event);
	}
}

void Room::NotifyDisconnectedOnce(DisconnectReason reason) {
	if (!disconnected_event_emitted_.exchange(true)) {
		disconnect_reason_ = reason;
		if (auto* listener = event_listener_.load()) {
			listener->OnDisconnected(reason);
		}
	}
}

bool Room::SetState(RoomState state) {
	if (state_.exchange(state) == state) {
		return false;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnConnectionStateChanged(state);
	}
	return true;
}

bool Room::TransitionState(RoomState expected, RoomState state) {
	if (!state_.compare_exchange_strong(expected, state)) {
		return false;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnConnectionStateChanged(state);
	}
	return true;
}

void Room::ApplyJoinResponse(const livekit::JoinResponse& join_response, bool reconnecting,
                             bool emit_participant_events) {
	if (join_response.has_server_info()) {
		server_info_ = from_proto(join_response.server_info());
	} else if (!join_response.server_region().empty() || !join_response.server_version().empty()) {
		server_info_.region = join_response.server_region();
		server_info_.version = join_response.server_version();
	}
	if (join_response.has_participant()) {
		if (reconnecting) {
			local_participant_->UpdateFromInfoPreservingTracks(join_response.participant());
		} else {
			local_participant_->UpdateFromInfo(join_response.participant());
		}
		rtc_engine_->SetE2EEManager(e2ee_manager_.get(), local_participant_->Identity());
	}
	{
		std::lock_guard<std::mutex> guard(room_info_mutex_);
		room_info_ = join_response.room();
	}

	std::vector<livekit::ParticipantInfo> participants;
	participants.reserve(static_cast<std::size_t>(join_response.other_participants_size()));
	std::set<std::string> participant_sids;
	for (const auto& participant : join_response.other_participants()) {
		participants.push_back(participant);
		participant_sids.insert(participant.sid());
	}

	std::map<std::string, std::shared_ptr<RemoteTrack>> detached_tracks;
	if (reconnecting) {
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& [sid, track] : remote_tracks_) {
			auto participant = FindRemoteParticipantForTrack(sid);
			if (participant) {
				for (auto* publication : participant->GetTrackPublications()) {
					if (publication->Sid() == sid) {
						if (auto* concrete = dynamic_cast<TrackPublication*>(publication)) {
							concrete->SetTrack(nullptr);
						}
						if (auto* remote = dynamic_cast<RemoteTrackPublication*>(publication)) {
							remote->SetTrackAttached(false);
						}
						break;
					}
				}
			}
		}
		detached_tracks.swap(remote_tracks_);
		pending_media_tracks_.clear();
		for (auto participant = remote_participants_.begin();
		     participant != remote_participants_.end();) {
			if (participant_sids.count(participant->first) == 0) {
				participant = remote_participants_.erase(participant);
			} else {
				++participant;
			}
		}
	}
	// A media track can wait for an in-flight frame callback while being destroyed. Those callbacks
	// also take participants_mutex_, so release track ownership only after leaving the critical
	// section above.
	detached_tracks.clear();
	ApplyParticipantUpdates(participants, emit_participant_events);
}

void Room::ParticipantUpdateEvent(const std::vector<livekit::ParticipantInfo>& updates) {
	ApplyParticipantUpdates(updates);
}

void Room::RemoteMuteChangedEvent(const std::string& sid, bool muted) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<TrackPublicationInterface> publication;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForTrack(sid);
		if (!participant) {
			return;
		}
		auto publications = participant->TrackPublicationsSnapshot();
		auto found = publications.find(sid);
		if (found == publications.end()) {
			return;
		}
		publication = found->second;
		if (publication->IsMuted() == muted) {
			return;
		}
		if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
			concrete->SetMuted(muted);
		}
	}
	if (auto* listener = event_listener_.load()) {
		if (muted) {
			listener->OnTrackMuted(publication.get(), participant.get());
		} else {
			listener->OnTrackUnmuted(publication.get(), participant.get());
		}
	}
}

void Room::LocalTrackUnpublishedEvent(const std::string& sid) {
	auto publications = local_participant_->TrackPublicationsSnapshot();
	auto found = publications.find(sid);
	if (found == publications.end()) {
		return;
	}
	auto publication = found->second;
	if (auto* local_track = dynamic_cast<LocalTrack*>(publication->Track())) {
		if (local_participant_->UnpublishTrack(local_track, true)) {
			return;
		}
		if (e2ee_manager_) {
			E2EEManagerNativeAccess::Detach(*e2ee_manager_, sid, FrameCryptorDirection::Sender);
		}
		local_track->SetTransceiver(nullptr);
		local_track->SetEnabled(false);
	}
	local_participant_->RemoveTrackPublication(sid);
	if (auto* listener = event_listener_.load()) {
		listener->OnLocalTrackUnpublished(publication.get(), local_participant_.get());
	}
}

void Room::SpeakersChangedEvent(const std::vector<livekit::SpeakerInfo>& updates) {
	std::vector<ParticipantInterface*> active_speakers;
	std::vector<std::shared_ptr<RemoteParticipant>> retained_participants;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& update : updates) {
			Participant* participant = nullptr;
			if (update.sid() == local_participant_->Sid()) {
				participant = local_participant_.get();
			} else {
				auto found = remote_participants_.find(update.sid());
				if (found != remote_participants_.end()) {
					retained_participants.push_back(found->second);
					participant = found->second.get();
				}
			}
			if (participant == nullptr) {
				continue;
			}
			participant->SetSpeakerInfo(update.level(), update.active());
		}
		if (local_participant_->IsSpeaking()) {
			active_speakers.push_back(local_participant_.get());
		}
		retained_participants.clear();
		for (const auto& [sid, participant] : remote_participants_) {
			if (participant->IsSpeaking()) {
				retained_participants.push_back(participant);
				active_speakers.push_back(participant.get());
			}
		}
	}
	std::sort(active_speakers.begin(), active_speakers.end(),
	          [](ParticipantInterface* left, ParticipantInterface* right) {
		          return left->AudioLevel() > right->AudioLevel();
	          });
	if (auto* listener = event_listener_.load()) {
		listener->OnActiveSpeakersChanged(active_speakers);
	}
}

void Room::RoomUpdateEvent(const livekit::Room& update) {
	bool metadata_changed = false;
	bool recording_changed = false;
	std::string previous_sid;
	{
		std::lock_guard<std::mutex> guard(room_info_mutex_);
		previous_sid = room_info_.sid();
		metadata_changed = room_info_.metadata() != update.metadata();
		recording_changed = room_info_.active_recording() != update.active_recording();
		room_info_ = update;
	}
	if (auto* listener = event_listener_.load()) {
		const RoomSnapshot snapshot{update.sid(), update.name(), update.metadata(),
		                            update.active_recording()};
		listener->OnRoomUpdated(snapshot);
		if (previous_sid != update.sid()) {
			listener->OnRoomSidChanged(previous_sid, update.sid());
		}
		if (metadata_changed) {
			listener->OnRoomMetadataChanged(update.metadata());
		}
		if (recording_changed) {
			listener->OnRecordingStatusChanged(update.active_recording());
		}
	}
}

void Room::TokenRefreshedEvent() {
	if (auto* listener = event_listener_.load()) {
		listener->OnTokenRefreshed();
	}
}

void Room::RoomMovedEvent(const livekit::RoomMovedResponse& response) {
	if (!response.has_room()) {
		return;
	}
	std::string previous_sid;
	{
		std::lock_guard<std::mutex> guard(room_info_mutex_);
		previous_sid = room_info_.sid();
	}
	livekit::JoinResponse join_response;
	*join_response.mutable_room() = response.room();
	if (response.has_participant()) {
		*join_response.mutable_participant() = response.participant();
	}
	std::vector<livekit::ParticipantInfo> moved_participants;
	moved_participants.reserve(static_cast<std::size_t>(response.other_participants_size()));
	for (const auto& participant : response.other_participants()) {
		moved_participants.push_back(participant);
	}
	// A room move is a boundary between participant sets. Report the old set leaving before the
	// new snapshot is ingested, matching the lifecycle visible in other LiveKit clients.
	std::vector<livekit::ParticipantInfo> disconnected;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		disconnected.reserve(remote_participants_.size());
		for (const auto& [sid, participant] : remote_participants_) {
			livekit::ParticipantInfo info;
			info.set_sid(sid);
			info.set_identity(participant->Identity());
			info.set_state(livekit::ParticipantInfo_State_DISCONNECTED);
			disconnected.push_back(std::move(info));
		}
	}
	ApplyParticipantUpdates(disconnected, true);
	ApplyJoinResponse(join_response, true, false);
	if (auto* listener = event_listener_.load()) {
		const RoomSnapshot snapshot{response.room().sid(), response.room().name(),
		                            response.room().metadata(), response.room().active_recording()};
		listener->OnRoomMoved(snapshot);
		listener->OnRoomUpdated(snapshot);
		if (previous_sid != response.room().sid()) {
			listener->OnRoomSidChanged(previous_sid, response.room().sid());
		}
	}
	ApplyParticipantUpdates(moved_participants, true);
}

void Room::ConnectionQualityEvent(const std::vector<livekit::ConnectionQualityInfo>& updates) {
	struct QualityEvent {
		ConnectionQuality quality;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	std::vector<QualityEvent> events;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& update : updates) {
			const auto quality = from_connection_quality(update.quality());
			if (update.participant_sid() == local_participant_->Sid()) {
				if (local_participant_->GetConnectionQuality() != quality) {
					local_participant_->SetConnectionQuality(quality);
					events.push_back({quality, local_participant_.get(), nullptr});
				}
				continue;
			}
			auto found = remote_participants_.find(update.participant_sid());
			if (found != remote_participants_.end() &&
			    found->second->GetConnectionQuality() != quality) {
				found->second->SetConnectionQuality(quality);
				events.push_back({quality, found->second.get(), found->second});
			}
		}
	}
	if (auto* listener = event_listener_.load()) {
		for (const auto& event : events) {
			listener->OnConnectionQualityChanged(event.quality, event.participant);
		}
	}
}

void Room::SubscriptionPermissionUpdateEvent(const livekit::SubscriptionPermissionUpdate& update) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<TrackPublicationInterface> publication;
	bool changed = false;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto found = remote_participants_.find(update.participant_sid());
		if (found == remote_participants_.end()) {
			return;
		}
		participant = found->second;
		auto publications = participant->TrackPublicationsSnapshot();
		auto track = publications.find(update.track_sid());
		if (track == publications.end()) {
			return;
		}
		publication = track->second;
		if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
			changed = concrete->SetSubscriptionAllowed(update.allowed());
		}
	}
	if (changed) {
		if (auto* listener = event_listener_.load()) {
			listener->OnTrackSubscriptionPermissionChanged(publication.get(), participant.get(),
			                                               update.allowed());
		}
	}
}

void Room::SubscriptionErrorEvent(const livekit::SubscriptionResponse& response) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<TrackPublicationInterface> publication;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForTrack(response.track_sid());
		if (!participant) {
			return;
		}
		auto publications = participant->TrackPublicationsSnapshot();
		auto found = publications.find(response.track_sid());
		if (found == publications.end()) {
			return;
		}
		publication = found->second;
		if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
			concrete->SetSubscriptionError(
			    static_cast<SubscriptionError>(static_cast<int>(response.err())));
		}
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnTrackSubscriptionFailed(
		    response.track_sid(), participant.get(),
		    static_cast<SubscriptionError>(static_cast<int>(response.err())));
	}
}

void Room::MediaTrackEvent(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> rtc_track,
                           webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                           std::function<std::string()> stats_provider) {
	if (!rtc_track) {
		return;
	}
	const std::string track_sid = rtc_track->id();
	std::string participant_sid;
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> subscribed_track;
	std::shared_ptr<TrackPublicationInterface> publication;
	TrackSubscriptionStatus previous_status = TrackSubscriptionStatus::Desired;
	TrackSubscriptionStatus current_status = TrackSubscriptionStatus::Desired;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForTrack(track_sid);
		if (!participant) {
			pending_media_tracks_[track_sid] = {rtc_track, receiver, std::move(stats_provider)};
			return;
		}
		if (remote_tracks_.count(track_sid) != 0) {
			return;
		}
		participant_sid = participant->Sid();
		auto publications = participant->TrackPublicationsSnapshot();
		auto found = publications.find(track_sid);
		if (found != publications.end()) {
			publication = found->second;
		}
		const std::string track_name = publication != nullptr ? publication->Name() : track_sid;
		if (rtc_track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
			auto media =
			    std::make_unique<AudioTrack>(webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
			        static_cast<webrtc::AudioTrackInterface*>(rtc_track.get())));
			auto remote = std::make_shared<RemoteAudioTrack>(
			    track_sid, track_name, std::move(media),
			    [this, participant_sid, track_sid](const AudioFrame& frame) {
				    NotifyAudioFrame(participant_sid, track_sid, frame);
			    });
			subscribed_track = std::move(remote);
			remote_tracks_.emplace(track_sid, subscribed_track);
		} else if (rtc_track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
			auto media =
			    std::make_unique<VideoTrack>(webrtc::scoped_refptr<webrtc::VideoTrackInterface>(
			        static_cast<webrtc::VideoTrackInterface*>(rtc_track.get())));
			auto remote = std::make_shared<RemoteVideoTrack>(
			    track_sid, track_name, std::move(media),
			    [this, participant_sid, track_sid](const VideoFrame& frame) {
				    NotifyVideoFrame(participant_sid, track_sid, frame);
			    });
			subscribed_track = std::move(remote);
			remote_tracks_.emplace(track_sid, subscribed_track);
		}
		if (subscribed_track) {
			subscribed_track->SetStatsProvider(std::move(stats_provider));
			subscribed_track->SetStreamState(TrackStreamState::Active);
			if (found != publications.end()) {
				if (auto* remote = dynamic_cast<RemoteTrackPublication*>(publication.get())) {
					previous_status = remote->SubscriptionStatus();
					remote->SetTrackAttached(true);
					current_status = remote->SubscriptionStatus();
				}
				if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
					concrete->SetTrack(subscribed_track.get());
					concrete->SetSubscriptionError(std::nullopt);
				}
			}
		}
	}
	if (subscribed_track) {
		if (e2ee_manager_ && receiver && publication &&
		    publication->Encryption() == EncryptionType::Gcm) {
			E2EEManagerNativeAccess::AttachReceiver(*e2ee_manager_, track_sid,
			                                        participant->Identity(),
			                                        subscribed_track->Kind(), std::move(receiver));
		}
		if (auto* listener = event_listener_.load()) {
			listener->OnTrackSubscribed(subscribed_track.get(), participant.get());
			if (publication && current_status != previous_status) {
				listener->OnTrackSubscriptionStatusChanged(publication.get(), participant.get(),
				                                           current_status);
			}
		}
	}
}

void Room::MediaTrackRemovedEvent(const std::string& track_sid) {
	if (e2ee_manager_) {
		E2EEManagerNativeAccess::Detach(*e2ee_manager_, track_sid, FrameCryptorDirection::Receiver);
	}
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> track;
	std::shared_ptr<TrackPublicationInterface> publication;
	TrackSubscriptionStatus previous_status = TrackSubscriptionStatus::Unsubscribed;
	TrackSubscriptionStatus current_status = TrackSubscriptionStatus::Unsubscribed;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForTrack(track_sid);
		auto found_track = remote_tracks_.find(track_sid);
		if (!participant || found_track == remote_tracks_.end()) {
			return;
		}
		track = found_track->second;
		auto publications = participant->TrackPublicationsSnapshot();
		auto found_publication = publications.find(track_sid);
		if (found_publication == publications.end()) {
			return;
		}
		publication = found_publication->second;
		if (auto* remote = dynamic_cast<RemoteTrackPublication*>(publication.get())) {
			previous_status = remote->SubscriptionStatus();
			remote->SetTrackAttached(false);
			current_status = remote->SubscriptionStatus();
		}
		if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
			concrete->SetTrack(nullptr);
		}
		remote_tracks_.erase(found_track);
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnTrackUnsubscribed(track.get(), publication.get(), participant.get());
		if (current_status != previous_status) {
			listener->OnTrackSubscriptionStatusChanged(publication.get(), participant.get(),
			                                           current_status);
		}
	}
}

void Room::StreamStateUpdateEvent(const std::vector<livekit::StreamStateInfo>& updates) {
	struct StreamEvent {
		std::shared_ptr<TrackPublicationInterface> publication;
		std::shared_ptr<RemoteParticipant> participant;
		TrackStreamState state;
	};
	std::vector<StreamEvent> events;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& update : updates) {
			auto participant = remote_participants_.find(update.participant_sid());
			auto track = remote_tracks_.find(update.track_sid());
			if (participant == remote_participants_.end() || track == remote_tracks_.end()) {
				continue;
			}
			auto publications = participant->second->TrackPublicationsSnapshot();
			auto publication = publications.find(update.track_sid());
			if (publication == publications.end()) {
				continue;
			}
			const auto state = update.state() == livekit::StreamState::PAUSED
			                       ? TrackStreamState::Paused
			                       : TrackStreamState::Active;
			if (track->second->SetStreamState(state)) {
				events.push_back({publication->second, participant->second, state});
			}
		}
	}
	if (auto* listener = event_listener_.load()) {
		for (const auto& event : events) {
			listener->OnTrackStreamStateChanged(event.publication.get(), event.participant.get(),
			                                    event.state);
		}
	}
}

void Room::DataChannelBufferStatusEvent(const DataChannelBufferStatus& status) {
	if (auto* listener = event_listener_.load()) {
		listener->OnDataChannelBufferStatusChanged(status);
	}
}

void Room::LocalTrackSubscribedEvent(const std::string& track_sid) {
	local_participant_->LocalTrackSubscribed(track_sid);
}

void Room::SubscribedQualityUpdateEvent(const livekit::SubscribedQualityUpdate& update) {
	SubscribedQualityUpdate converted;
	converted.track_sid = update.track_sid();
	converted.qualities.reserve(update.subscribed_qualities_size());
	for (const auto& quality : update.subscribed_qualities()) {
		converted.qualities.push_back({from_video_quality(quality.quality()), quality.enabled()});
	}
	converted.codecs.reserve(update.subscribed_codecs_size());
	for (const auto& codec : update.subscribed_codecs()) {
		auto& converted_codec = converted.codecs.emplace_back();
		converted_codec.codec = codec.codec();
		converted_codec.qualities.reserve(codec.qualities_size());
		for (const auto& quality : codec.qualities()) {
			converted_codec.qualities.push_back(
			    {from_video_quality(quality.quality()), quality.enabled()});
		}
	}
	local_participant_->SubscribedQualityUpdate(std::move(converted));
}

void Room::DataTrackFrameEvent(const std::string& track_sid, DataTrackFrame frame) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<DataTrackInterface> interface;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForDataTrack(track_sid);
		if (!participant) {
			return;
		}
		auto tracks = participant->DataTracksSnapshot();
		auto found = tracks.find(track_sid);
		if (found == tracks.end()) {
			return;
		}
		interface = found->second;
	}
	auto* track = dynamic_cast<RemoteDataTrack*>(interface.get());
	if (track == nullptr) {
		return;
	}
	track->PushFrame(frame);
	if (auto* listener = event_listener_.load()) {
		listener->OnDataTrackFrame(track, participant.get(), frame);
	}
}

void Room::LocalDataTrackUnpublishedEvent(uint16_t publisher_handle) {
	local_participant_->LocalDataTrackUnpublished(publisher_handle);
}

void Room::DataPacketEvent(const livekit::DataPacket& packet) {
	if (packet.has_user()) {
		DataReceivedEvent event;
		event.payload.assign(packet.user().payload().begin(), packet.user().payload().end());
		event.topic = packet.user().has_topic() ? packet.user().topic() : "";
		event.participant_identity = packet.participant_identity();
		if (event.participant_identity.empty()) {
			event.participant_identity = packet.user().participant_identity();
		}
		event.reliable = packet.kind() != livekit::DataPacket_Kind_LOSSY;
		if (auto* listener = event_listener_.load()) {
			listener->OnDataReceived(event);
		}
		return;
	}
	if (packet.has_sip_dtmf()) {
		if (auto* listener = event_listener_.load()) {
			listener->OnSipDtmfReceived({packet.sip_dtmf().code(), packet.sip_dtmf().digit(),
			                             packet.participant_identity()});
		}
		return;
	}
	if (packet.has_chat_message()) {
		const auto& chat = packet.chat_message();
		ChatMessage event;
		event.id = chat.id();
		event.timestamp = chat.timestamp();
		if (chat.has_edit_timestamp()) {
			event.edit_timestamp = chat.edit_timestamp();
		}
		event.message = chat.message();
		event.deleted = chat.deleted();
		event.generated = chat.generated();
		event.participant_identity = packet.participant_identity();
		if (auto* listener = event_listener_.load()) {
			listener->OnChatMessageReceived(event);
		}
		return;
	}
	if (packet.has_transcription()) {
		const auto& transcription = packet.transcription();
		TranscriptionReceivedEvent event;
		event.transcribed_participant_identity = transcription.transcribed_participant_identity();
		event.track_id = transcription.track_id();
		event.segments.reserve(static_cast<std::size_t>(transcription.segments_size()));
		const auto received_time = CurrentTimestampMilliseconds();
		{
			std::lock_guard<std::mutex> guard(transcription_mutex_);
			for (const auto& input : transcription.segments()) {
				TranscriptionSegment segment;
				segment.id = input.id();
				segment.text = input.text();
				segment.language = input.language();
				segment.start_time = input.start_time();
				segment.end_time = input.end_time();
				segment.final = input.final();
				auto position =
				    transcription_received_times_.try_emplace(segment.id, received_time).first;
				segment.first_received_time = position->second;
				segment.last_received_time = received_time;
				if (segment.final) {
					transcription_received_times_.erase(position);
				}
				event.segments.push_back(std::move(segment));
			}
		}
		if (auto* listener = event_listener_.load()) {
			listener->OnTranscriptionReceived(event);
		}
		return;
	}
	if (packet.has_metrics()) {
		const auto& metrics = packet.metrics();
		MetricsReceivedEvent event;
		event.timestamp_ms = metrics.timestamp_ms();
		if (metrics.has_normalized_timestamp()) {
			event.normalized_timestamp = MetricTimestamp{metrics.normalized_timestamp().seconds(),
			                                             metrics.normalized_timestamp().nanos()};
		}
		event.string_data.assign(metrics.str_data().begin(), metrics.str_data().end());
		event.participant_identity = packet.participant_identity();
		event.time_series.reserve(static_cast<std::size_t>(metrics.time_series_size()));
		for (const auto& input : metrics.time_series()) {
			TimeSeriesMetric series;
			series.label = input.label();
			series.participant_identity = input.participant_identity();
			series.track_sid = input.track_sid();
			series.rid = input.rid();
			series.samples.reserve(static_cast<std::size_t>(input.samples_size()));
			for (const auto& input_sample : input.samples()) {
				MetricSample sample;
				sample.timestamp_ms = input_sample.timestamp_ms();
				if (input_sample.has_normalized_timestamp()) {
					sample.normalized_timestamp =
					    MetricTimestamp{input_sample.normalized_timestamp().seconds(),
					                    input_sample.normalized_timestamp().nanos()};
				}
				sample.value = input_sample.value();
				series.samples.push_back(std::move(sample));
			}
			event.time_series.push_back(std::move(series));
		}
		event.events.reserve(static_cast<std::size_t>(metrics.events_size()));
		for (const auto& input : metrics.events()) {
			EventMetric metric;
			metric.label = input.label();
			metric.participant_identity = input.participant_identity();
			metric.track_sid = input.track_sid();
			metric.rid = input.rid();
			metric.start_timestamp_ms = input.start_timestamp_ms();
			if (input.has_end_timestamp_ms()) {
				metric.end_timestamp_ms = input.end_timestamp_ms();
			}
			if (input.has_normalized_start_timestamp()) {
				metric.normalized_start_timestamp =
				    MetricTimestamp{input.normalized_start_timestamp().seconds(),
				                    input.normalized_start_timestamp().nanos()};
			}
			if (input.has_normalized_end_timestamp()) {
				metric.normalized_end_timestamp =
				    MetricTimestamp{input.normalized_end_timestamp().seconds(),
				                    input.normalized_end_timestamp().nanos()};
			}
			metric.metadata = input.metadata();
			event.events.push_back(std::move(metric));
		}
		if (auto* listener = event_listener_.load()) {
			listener->OnMetricsReceived(event);
		}
		return;
	}
	if (packet.has_stream_header() && packet.stream_header().has_text_header()) {
		const auto& header = packet.stream_header();
		if (header.stream_id().empty() || !IsSupportedCompression(header.compression())) {
			return;
		}
		IncomingText incoming;
		incoming.event.stream_id = header.stream_id();
		incoming.event.topic = header.topic();
		incoming.event.participant_identity = packet.participant_identity();
		incoming.event.reply_to_stream_id = header.text_header().reply_to_stream_id();
		incoming.event.attached_stream_ids.assign(
		    header.text_header().attached_stream_ids().begin(),
		    header.text_header().attached_stream_ids().end());
		incoming.event.attributes.insert(header.attributes().begin(), header.attributes().end());
		incoming.event.timestamp = header.timestamp();
		incoming.info.stream_id = incoming.event.stream_id;
		incoming.info.mime_type = header.mime_type();
		incoming.info.topic = incoming.event.topic;
		incoming.info.participant_identity = incoming.event.participant_identity;
		incoming.info.reply_to_stream_id = incoming.event.reply_to_stream_id;
		incoming.info.attached_stream_ids = incoming.event.attached_stream_ids;
		incoming.info.attributes = incoming.event.attributes;
		incoming.info.timestamp = incoming.event.timestamp;
		{
			std::lock_guard<std::mutex> guard(stream_handlers_mutex_);
			auto handler = text_stream_handlers_.find(header.topic());
			if (handler != text_stream_handlers_.end()) {
				incoming.handler = handler->second;
			}
		}
		if (header.has_total_length()) {
			incoming.expected_length = header.total_length();
			incoming.info.total_size = header.total_length();
			if ((header.compression() == livekit::DataStream_CompressionType_DEFLATE_RAW &&
			     *incoming.expected_length > kMaximumBufferedDataStreamSize) ||
			    (!incoming.handler &&
			     (*incoming.expected_length > std::numeric_limits<std::size_t>::max() ||
			      *incoming.expected_length > kMaximumBufferedDataStreamSize))) {
				return;
			}
			if (!incoming.handler) {
				try {
					incoming.event.text.reserve(
					    static_cast<std::size_t>(*incoming.expected_length));
				} catch (const std::exception&) {
					return;
				}
			}
		}
		if (header.compression() == livekit::DataStream_CompressionType_DEFLATE_RAW) {
			incoming.inflater =
			    std::make_unique<detail::InflateRawStream>(kMaximumBufferedDataStreamSize);
			if (!incoming.inflater->IsValid()) {
				return;
			}
		}
		if (header.has_inline_content()) {
			std::vector<uint8_t> content;
			if (!DecodeInlineContent(header, content) ||
			    (incoming.expected_length && content.size() != *incoming.expected_length)) {
				return;
			}
			if (incoming.handler) {
				TextStreamEvent event{incoming.info, DataStreamEventType::Open};
				incoming.handler(event);
				event.type = DataStreamEventType::Chunk;
				event.content.assign(content.begin(), content.end());
				incoming.handler(event);
				event.type = DataStreamEventType::Closed;
				event.content.clear();
				incoming.handler(event);
			} else {
				incoming.event.text.assign(content.begin(), content.end());
				if (auto* listener = event_listener_.load()) {
					listener->OnTextReceived(incoming.event);
				}
			}
			return;
		}
		auto handler = incoming.handler;
		auto info = incoming.info;
		{
			std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
			incoming_files_.erase(header.stream_id());
			incoming_texts_[header.stream_id()] = std::move(incoming);
		}
		if (handler) {
			handler(TextStreamEvent{std::move(info), DataStreamEventType::Open});
		}
		return;
	}
	if (packet.has_stream_header() && packet.stream_header().has_byte_header()) {
		const auto& header = packet.stream_header();
		if (header.stream_id().empty() || !IsSupportedCompression(header.compression())) {
			return;
		}
		IncomingFile incoming;
		incoming.event.stream_id = header.stream_id();
		incoming.event.name = header.byte_header().name();
		incoming.event.mime_type = header.mime_type();
		incoming.event.topic = header.topic();
		incoming.event.participant_identity = packet.participant_identity();
		incoming.event.attributes.insert(header.attributes().begin(), header.attributes().end());
		incoming.event.timestamp = header.timestamp();
		incoming.info.stream_id = incoming.event.stream_id;
		incoming.info.mime_type = incoming.event.mime_type;
		incoming.info.topic = incoming.event.topic;
		incoming.info.participant_identity = incoming.event.participant_identity;
		incoming.info.attributes = incoming.event.attributes;
		incoming.info.timestamp = incoming.event.timestamp;
		incoming.info.name = incoming.event.name;
		{
			std::lock_guard<std::mutex> guard(stream_handlers_mutex_);
			auto handler = byte_stream_handlers_.find(header.topic());
			if (handler != byte_stream_handlers_.end()) {
				incoming.handler = handler->second;
			}
		}
		if (header.has_total_length()) {
			incoming.expected_length = header.total_length();
			incoming.info.total_size = header.total_length();
			if ((header.compression() == livekit::DataStream_CompressionType_DEFLATE_RAW &&
			     *incoming.expected_length > kMaximumBufferedDataStreamSize) ||
			    (!incoming.handler &&
			     (*incoming.expected_length > std::numeric_limits<std::size_t>::max() ||
			      *incoming.expected_length > kMaximumBufferedDataStreamSize))) {
				return;
			}
			if (!incoming.handler) {
				try {
					incoming.event.data.reserve(
					    static_cast<std::size_t>(*incoming.expected_length));
				} catch (const std::exception&) {
					return;
				}
			}
		}
		if (header.compression() == livekit::DataStream_CompressionType_DEFLATE_RAW) {
			incoming.inflater =
			    std::make_unique<detail::InflateRawStream>(kMaximumBufferedDataStreamSize);
			if (!incoming.inflater->IsValid()) {
				return;
			}
		}
		if (header.has_inline_content()) {
			std::vector<uint8_t> content;
			if (!DecodeInlineContent(header, content) ||
			    (incoming.expected_length && content.size() != *incoming.expected_length)) {
				return;
			}
			if (incoming.handler) {
				ByteStreamEvent event{incoming.info, DataStreamEventType::Open};
				incoming.handler(event);
				event.type = DataStreamEventType::Chunk;
				event.content = content;
				incoming.handler(event);
				event.type = DataStreamEventType::Closed;
				event.content.clear();
				incoming.handler(event);
			} else {
				incoming.event.data = std::move(content);
				if (auto* listener = event_listener_.load()) {
					ByteReceivedEvent byte_event;
					static_cast<FileReceivedEvent&>(byte_event) = incoming.event;
					listener->OnByteReceived(byte_event);
					if (!incoming.event.name.empty()) {
						listener->OnFileReceived(incoming.event);
					}
				}
			}
			return;
		}
		auto handler = incoming.handler;
		auto info = incoming.info;
		{
			std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
			incoming_texts_.erase(header.stream_id());
			incoming_files_[header.stream_id()] = std::move(incoming);
		}
		if (handler) {
			handler(ByteStreamEvent{std::move(info), DataStreamEventType::Open});
		}
		return;
	}
	if (packet.has_stream_chunk()) {
		const auto& chunk = packet.stream_chunk();
		TextStreamHandler text_handler;
		TextStreamEvent text_event;
		ByteStreamHandler byte_handler;
		ByteStreamEvent byte_event;
		{
			std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
			auto text = incoming_texts_.find(chunk.stream_id());
			if (text != incoming_texts_.end()) {
				std::string content = chunk.content();
				bool content_valid = true;
				if (text->second.inflater) {
					const auto compressed_size = static_cast<uint64_t>(chunk.content().size());
					std::vector<uint8_t> decoded;
					content_valid = compressed_size <= kMaximumBufferedDataStreamSize -
					                                       text->second.compressed_length &&
					                text->second.inflater->Write(
					                    reinterpret_cast<const uint8_t*>(chunk.content().data()),
					                    chunk.content().size(), decoded);
					if (content_valid) {
						text->second.compressed_length += compressed_size;
						content.assign(decoded.begin(), decoded.end());
					}
				}
				const auto content_size = static_cast<uint64_t>(content.size());
				if (!content_valid || chunk.chunk_index() != text->second.next_chunk ||
				    content_size >
				        std::numeric_limits<uint64_t>::max() - text->second.received_length ||
				    (text->second.expected_length &&
				     (text->second.received_length > *text->second.expected_length ||
				      content_size >
				          *text->second.expected_length - text->second.received_length)) ||
				    (!text->second.handler && content_size > kMaximumBufferedDataStreamSize -
				                                                 text->second.event.text.size())) {
					text_handler = text->second.handler;
					text_event = {text->second.info, DataStreamEventType::Failed, "",
					              chunk.chunk_index(), "invalid stream chunk"};
					incoming_texts_.erase(text);
				} else {
					text->second.received_length += content_size;
					++text->second.next_chunk;
					if (text->second.handler) {
						text_handler = text->second.handler;
						text_event = {text->second.info, DataStreamEventType::Chunk,
						              std::move(content), chunk.chunk_index(), ""};
					} else {
						text->second.event.text.append(content);
					}
				}
			} else {
				auto bytes = incoming_files_.find(chunk.stream_id());
				if (bytes == incoming_files_.end()) {
					return;
				}
				std::vector<uint8_t> content(chunk.content().begin(), chunk.content().end());
				bool content_valid = true;
				if (bytes->second.inflater) {
					const auto compressed_size = static_cast<uint64_t>(chunk.content().size());
					content.clear();
					content_valid = compressed_size <= kMaximumBufferedDataStreamSize -
					                                       bytes->second.compressed_length &&
					                bytes->second.inflater->Write(
					                    reinterpret_cast<const uint8_t*>(chunk.content().data()),
					                    chunk.content().size(), content);
					if (content_valid) {
						bytes->second.compressed_length += compressed_size;
					}
				}
				const auto content_size = static_cast<uint64_t>(content.size());
				if (!content_valid || chunk.chunk_index() != bytes->second.next_chunk ||
				    content_size >
				        std::numeric_limits<uint64_t>::max() - bytes->second.received_length ||
				    (bytes->second.expected_length &&
				     (bytes->second.received_length > *bytes->second.expected_length ||
				      content_size >
				          *bytes->second.expected_length - bytes->second.received_length)) ||
				    (!bytes->second.handler &&
				     content_size >
				         kMaximumBufferedDataStreamSize - bytes->second.event.data.size())) {
					byte_handler = bytes->second.handler;
					byte_event = {bytes->second.info,
					              DataStreamEventType::Failed,
					              {},
					              chunk.chunk_index(),
					              "invalid stream chunk"};
					incoming_files_.erase(bytes);
				} else {
					bytes->second.received_length += content_size;
					++bytes->second.next_chunk;
					if (bytes->second.handler) {
						byte_handler = bytes->second.handler;
						byte_event.info = bytes->second.info;
						byte_event.type = DataStreamEventType::Chunk;
						byte_event.content = std::move(content);
						byte_event.chunk_index = chunk.chunk_index();
					} else {
						bytes->second.event.data.insert(bytes->second.event.data.end(),
						                                content.begin(), content.end());
					}
				}
			}
		}
		if (text_handler) {
			text_handler(text_event);
		}
		if (byte_handler) {
			byte_handler(byte_event);
		}
		return;
	}
	if (packet.has_stream_trailer()) {
		TextReceivedEvent text_event;
		bool has_text_event = false;
		FileReceivedEvent event;
		bool has_byte_event = false;
		TextStreamHandler text_handler;
		TextStreamEvent text_stream_event;
		ByteStreamHandler byte_handler;
		ByteStreamEvent byte_stream_event;
		{
			std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
			auto text = incoming_texts_.find(packet.stream_trailer().stream_id());
			if (text != incoming_texts_.end()) {
				const bool complete =
				    packet.stream_trailer().reason().empty() &&
				    (!text->second.inflater || text->second.inflater->Finished()) &&
				    (!text->second.expected_length ||
				     text->second.received_length == *text->second.expected_length);
				for (const auto& [key, value] : packet.stream_trailer().attributes()) {
					text->second.event.attributes[key] = value;
					text->second.info.attributes[key] = value;
				}
				if (text->second.handler) {
					text_handler = text->second.handler;
					text_stream_event.info = text->second.info;
					text_stream_event.type =
					    complete ? DataStreamEventType::Closed : DataStreamEventType::Failed;
					text_stream_event.reason = packet.stream_trailer().reason();
					if (!complete && text_stream_event.reason.empty()) {
						text_stream_event.reason = "incomplete stream";
					}
				} else if (complete) {
					text_event = std::move(text->second.event);
					has_text_event = true;
				}
				incoming_texts_.erase(text);
			}
			auto it = incoming_files_.find(packet.stream_trailer().stream_id());
			if (it != incoming_files_.end()) {
				const bool complete = packet.stream_trailer().reason().empty() &&
				                      (!it->second.inflater || it->second.inflater->Finished()) &&
				                      (!it->second.expected_length ||
				                       it->second.received_length == *it->second.expected_length);
				for (const auto& [key, value] : packet.stream_trailer().attributes()) {
					it->second.event.attributes[key] = value;
					it->second.info.attributes[key] = value;
				}
				if (it->second.handler) {
					byte_handler = it->second.handler;
					byte_stream_event.info = it->second.info;
					byte_stream_event.type =
					    complete ? DataStreamEventType::Closed : DataStreamEventType::Failed;
					byte_stream_event.reason = packet.stream_trailer().reason();
					if (!complete && byte_stream_event.reason.empty()) {
						byte_stream_event.reason = "incomplete stream";
					}
				} else if (complete) {
					event = std::move(it->second.event);
					has_byte_event = true;
				}
				incoming_files_.erase(it);
			}
		}
		if (text_handler) {
			text_handler(text_stream_event);
		}
		if (byte_handler) {
			byte_handler(byte_stream_event);
		}
		if (auto* listener = event_listener_.load()) {
			if (has_text_event) {
				listener->OnTextReceived(text_event);
			}
			if (has_byte_event) {
				ByteReceivedEvent byte_event;
				static_cast<FileReceivedEvent&>(byte_event) = event;
				listener->OnByteReceived(byte_event);
				if (!event.name.empty()) {
					listener->OnFileReceived(event);
				}
			}
		}
	}
}

std::shared_ptr<RemoteParticipant>
Room::FindRemoteParticipantForTrack(const std::string& track_sid) {
	for (auto& [sid, participant] : remote_participants_) {
		if (participant->HasTrackSid(track_sid)) {
			return participant;
		}
	}
	return nullptr;
}

std::shared_ptr<RemoteParticipant>
Room::FindRemoteParticipantForDataTrack(const std::string& track_sid) {
	for (auto& [sid, participant] : remote_participants_) {
		if (participant->GetDataTrackBySid(track_sid) != nullptr) {
			return participant;
		}
	}
	return nullptr;
}

void Room::NotifyAudioFrame(const std::string& participant_sid, const std::string& track_sid,
                            const AudioFrame& frame) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> track;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto it = remote_participants_.find(participant_sid);
		auto track_it = remote_tracks_.find(track_sid);
		if (it == remote_participants_.end() || track_it == remote_tracks_.end()) {
			return;
		}
		participant = it->second;
		track = track_it->second;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnAudioFrame(track.get(), participant.get(), frame);
	}
}

void Room::NotifyVideoFrame(const std::string& participant_sid, const std::string& track_sid,
                            const VideoFrame& frame) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> track;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto it = remote_participants_.find(participant_sid);
		auto track_it = remote_tracks_.find(track_sid);
		if (it == remote_participants_.end() || track_it == remote_tracks_.end()) {
			return;
		}
		participant = it->second;
		track = track_it->second;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnVideoFrame(track.get(), participant.get(), frame);
	}
}

void Room::ApplyParticipantUpdates(const std::vector<livekit::ParticipantInfo>& updates,
                                   bool emit_events) {
	struct PublicationEvent {
		std::shared_ptr<TrackPublicationInterface> publication;
		std::shared_ptr<RemoteParticipant> participant;
	};
	struct ParticipantValueEvent {
		std::string value;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	struct AttributesEvent {
		std::map<std::string, std::string> changes;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	struct PermissionsEvent {
		ParticipantPermissions previous;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	struct MuteEvent {
		std::shared_ptr<TrackPublicationInterface> publication;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
		bool muted;
	};
	struct UnsubscriptionEvent {
		std::shared_ptr<RemoteTrack> track;
		std::shared_ptr<TrackPublicationInterface> publication;
		std::shared_ptr<RemoteParticipant> participant;
		bool status_changed = false;
		TrackSubscriptionStatus status = TrackSubscriptionStatus::Unsubscribed;
	};
	struct DataTrackEvent {
		std::shared_ptr<DataTrackInterface> track;
		std::shared_ptr<RemoteParticipant> participant;
	};

	std::vector<PendingMediaTrack> ready_tracks;
	std::vector<std::shared_ptr<RemoteParticipant>> connected;
	std::vector<std::shared_ptr<RemoteParticipant>> disconnected;
	std::vector<PublicationEvent> published;
	std::vector<PublicationEvent> unpublished;
	std::vector<UnsubscriptionEvent> unsubscribed;
	std::vector<DataTrackEvent> data_tracks_published;
	std::vector<DataTrackEvent> data_tracks_unpublished;
	std::vector<ParticipantValueEvent> metadata_changed;
	std::vector<ParticipantValueEvent> name_changed;
	std::vector<AttributesEvent> attributes_changed;
	std::vector<PermissionsEvent> permissions_changed;
	std::vector<MuteEvent> mute_changed;
	std::vector<std::string> removed_cryptors;

	auto attribute_changes = [](const std::map<std::string, std::string>& old_attributes,
	                            const std::map<std::string, std::string>& new_attributes) {
		std::map<std::string, std::string> changes;
		for (const auto& [key, value] : new_attributes) {
			auto old = old_attributes.find(key);
			if (old == old_attributes.end() || old->second != value) {
				changes[key] = value;
			}
		}
		for (const auto& [key, value] : old_attributes) {
			if (new_attributes.count(key) == 0) {
				changes[key] = "";
			}
		}
		return changes;
	};

	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto remove_participant = [&](auto participant) {
			auto retained = participant->second;
			for (const auto& [sid, track] : retained->DataTracksSnapshot()) {
				if (auto* remote = dynamic_cast<RemoteDataTrack*>(track.get())) {
					remote->MarkUnpublished();
				}
				if (emit_events) {
					data_tracks_unpublished.push_back({track, retained});
				}
			}
			for (const auto& [sid, publication] : retained->TrackPublicationsSnapshot()) {
				auto track = remote_tracks_.find(sid);
				if (track != remote_tracks_.end() && emit_events) {
					UnsubscriptionEvent event{track->second, publication, retained};
					if (auto* remote = dynamic_cast<RemoteTrackPublication*>(publication.get())) {
						const auto previous = remote->SubscriptionStatus();
						remote->SetTrackAttached(false);
						event.status = remote->SubscriptionStatus();
						event.status_changed = event.status != previous;
					}
					unsubscribed.push_back(std::move(event));
				}
				if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
					concrete->SetTrack(nullptr);
				}
				remote_tracks_.erase(sid);
				pending_media_tracks_.erase(sid);
				removed_cryptors.push_back(sid);
				if (emit_events) {
					unpublished.push_back({publication, retained});
				}
			}
			remote_participants_.erase(participant);
			if (emit_events) {
				disconnected.push_back(std::move(retained));
			}
		};
		for (const auto& info : updates) {
			if (info.sid().empty()) {
				continue;
			}
			if (info.sid() == local_participant_->Sid() ||
			    (!info.identity().empty() && info.identity() == local_participant_->Identity())) {
				const auto old_metadata = local_participant_->Metadata();
				const auto old_name = local_participant_->Name();
				const auto old_attributes = local_participant_->Attributes();
				const auto old_permissions = local_participant_->Permissions();
				const auto old_publications = local_participant_->TrackPublicationsSnapshot();
				std::map<std::string, bool> old_mutes;
				for (const auto& [sid, publication] : old_publications) {
					old_mutes[sid] = publication->IsMuted();
				}
				local_participant_->UpdateFromInfo(info);
				if (emit_events && old_metadata != local_participant_->Metadata()) {
					metadata_changed.push_back({old_metadata, local_participant_.get(), nullptr});
				}
				if (emit_events && old_name != local_participant_->Name()) {
					name_changed.push_back(
					    {local_participant_->Name(), local_participant_.get(), nullptr});
				}
				if (emit_events) {
					auto changes =
					    attribute_changes(old_attributes, local_participant_->Attributes());
					if (!changes.empty()) {
						attributes_changed.push_back(
						    {std::move(changes), local_participant_.get(), nullptr});
					}
					if (old_permissions != local_participant_->Permissions()) {
						permissions_changed.push_back(
						    {old_permissions, local_participant_.get(), nullptr});
					}
					auto new_publications = local_participant_->TrackPublicationsSnapshot();
					for (const auto& [sid, publication] : new_publications) {
						auto old = old_mutes.find(sid);
						if (old != old_mutes.end() && old->second != publication->IsMuted()) {
							mute_changed.push_back({publication, local_participant_.get(), nullptr,
							                        publication->IsMuted()});
						}
					}
				}
				continue;
			}

			auto participant = remote_participants_.find(info.sid());
			if (info.state() == livekit::ParticipantInfo_State_DISCONNECTED) {
				if (participant != remote_participants_.end()) {
					remove_participant(participant);
				}
				continue;
			}

			if (participant == remote_participants_.end()) {
				if (!info.identity().empty()) {
					// A full reconnect assigns a new participant SID. The replacement may arrive
					// before the server reports the previous participant as disconnected.
					auto replaced =
					    std::find_if(remote_participants_.begin(), remote_participants_.end(),
					                 [&info](const auto& entry) {
						                 return entry.second->Identity() == info.identity();
					                 });
					if (replaced != remote_participants_.end()) {
						remove_participant(replaced);
					}
				}
				auto added = std::make_shared<RemoteParticipant>(
				    info, options_.auto_subscribe, CreateRemotePublicationHandlers(info.sid()),
				    [this](const std::string& track_sid, bool subscribe,
				           const DataTrackSubscriptionOptions& options) {
					    return rtc_engine_->UpdateDataTrackSubscription(track_sid, subscribe,
					                                                    options);
				    });
				remote_participants_.emplace(info.sid(), added);
				if (emit_events) {
					connected.push_back(added);
					for (const auto& [sid, publication] : added->TrackPublicationsSnapshot()) {
						published.push_back({publication, added});
					}
					for (const auto& [sid, track] : added->DataTracksSnapshot()) {
						data_tracks_published.push_back({track, added});
					}
				}
				continue;
			}

			auto retained = participant->second;
			const auto old_metadata = retained->Metadata();
			const auto old_name = retained->Name();
			const auto old_attributes = retained->Attributes();
			const auto old_permissions = retained->Permissions();
			const auto old_publications = retained->TrackPublicationsSnapshot();
			const auto old_data_tracks = retained->DataTracksSnapshot();
			std::map<std::string, bool> old_mutes;
			for (const auto& [sid, publication] : old_publications) {
				old_mutes[sid] = publication->IsMuted();
			}
			retained->UpdateFromInfo(info);
			const auto new_publications = retained->TrackPublicationsSnapshot();
			const auto new_data_tracks = retained->DataTracksSnapshot();
			if (!emit_events) {
				continue;
			}
			if (old_metadata != retained->Metadata()) {
				metadata_changed.push_back({old_metadata, retained.get(), retained});
			}
			if (old_name != retained->Name()) {
				name_changed.push_back({retained->Name(), retained.get(), retained});
			}
			auto changes = attribute_changes(old_attributes, retained->Attributes());
			if (!changes.empty()) {
				attributes_changed.push_back({std::move(changes), retained.get(), retained});
			}
			if (old_permissions != retained->Permissions()) {
				permissions_changed.push_back({old_permissions, retained.get(), retained});
			}
			for (const auto& [sid, publication] : new_publications) {
				auto old = old_publications.find(sid);
				if (old == old_publications.end()) {
					published.push_back({publication, retained});
				} else if (old_mutes[sid] != publication->IsMuted()) {
					mute_changed.push_back(
					    {publication, retained.get(), retained, publication->IsMuted()});
				}
			}
			for (const auto& [sid, publication] : old_publications) {
				if (new_publications.count(sid) == 0) {
					auto track = remote_tracks_.find(sid);
					if (track != remote_tracks_.end()) {
						UnsubscriptionEvent event{track->second, publication, retained};
						if (auto* remote =
						        dynamic_cast<RemoteTrackPublication*>(publication.get())) {
							const auto previous = remote->SubscriptionStatus();
							remote->SetTrackAttached(false);
							event.status = remote->SubscriptionStatus();
							event.status_changed = event.status != previous;
						}
						unsubscribed.push_back(std::move(event));
					}
					if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
						concrete->SetTrack(nullptr);
					}
					remote_tracks_.erase(sid);
					pending_media_tracks_.erase(sid);
					removed_cryptors.push_back(sid);
					unpublished.push_back({publication, retained});
				}
			}
			for (const auto& [sid, track] : new_data_tracks) {
				// A stable publisher handle can retain the track object while its SID changes.
				// Treat that as a rekey, not an unpublish followed by a new publication.
				const auto retained_track =
				    std::find_if(old_data_tracks.begin(), old_data_tracks.end(),
				                 [&track](const auto& entry) { return entry.second == track; });
				if (!old_data_tracks.contains(sid) && retained_track == old_data_tracks.end()) {
					data_tracks_published.push_back({track, retained});
				}
			}
			for (const auto& [sid, track] : old_data_tracks) {
				const auto retained_track =
				    std::find_if(new_data_tracks.begin(), new_data_tracks.end(),
				                 [&track](const auto& entry) { return entry.second == track; });
				if (!new_data_tracks.contains(sid) && retained_track == new_data_tracks.end()) {
					if (auto* remote = dynamic_cast<RemoteDataTrack*>(track.get())) {
						remote->MarkUnpublished();
					}
					data_tracks_unpublished.push_back({track, retained});
				}
			}
		}

		for (auto it = pending_media_tracks_.begin(); it != pending_media_tracks_.end();) {
			if (FindRemoteParticipantForTrack(it->first)) {
				ready_tracks.push_back(it->second);
				it = pending_media_tracks_.erase(it);
			} else {
				++it;
			}
		}
	}
	if (e2ee_manager_) {
		for (const auto& track_id : removed_cryptors) {
			E2EEManagerNativeAccess::Detach(*e2ee_manager_, track_id,
			                                FrameCryptorDirection::Receiver);
		}
	}

	if (auto* listener = event_listener_.load()) {
		for (const auto& participant : connected) {
			listener->OnParticipantConnected(participant.get());
		}
		for (const auto& event : published) {
			listener->OnTrackPublished(event.publication.get(), event.participant.get());
		}
		for (const auto& event : mute_changed) {
			if (event.muted) {
				listener->OnTrackMuted(event.publication.get(), event.participant);
			} else {
				listener->OnTrackUnmuted(event.publication.get(), event.participant);
			}
		}
		for (const auto& event : unsubscribed) {
			listener->OnTrackUnsubscribed(event.track.get(), event.publication.get(),
			                              event.participant.get());
			if (event.status_changed) {
				listener->OnTrackSubscriptionStatusChanged(event.publication.get(),
				                                           event.participant.get(), event.status);
			}
		}
		for (const auto& event : unpublished) {
			listener->OnTrackUnpublished(event.publication.get(), event.participant.get());
		}
		for (const auto& event : data_tracks_published) {
			listener->OnDataTrackPublished(
			    dynamic_cast<RemoteDataTrackInterface*>(event.track.get()),
			    event.participant.get());
		}
		for (const auto& event : data_tracks_unpublished) {
			listener->OnDataTrackUnpublished(event.track.get(), event.participant.get());
		}
		for (const auto& event : metadata_changed) {
			listener->OnParticipantMetadataChanged(event.value, event.participant);
		}
		for (const auto& event : name_changed) {
			listener->OnParticipantNameChanged(event.value, event.participant);
		}
		for (const auto& event : attributes_changed) {
			listener->OnParticipantAttributesChanged(event.changes, event.participant);
		}
		for (const auto& event : permissions_changed) {
			listener->OnParticipantPermissionsChanged(event.previous, event.participant);
		}
		for (const auto& participant : disconnected) {
			listener->OnParticipantDisconnected(participant.get());
		}
	}

	for (auto& track : ready_tracks) {
		MediaTrackEvent(std::move(track.track), std::move(track.receiver),
		                std::move(track.stats_provider));
	}
}

RoomInterface* CreateRoom() { return new Room(); }

std::unique_ptr<RoomInterface> CreateRoomUnique(RoomOptions options) {
	return std::make_unique<Room>(std::move(options));
}

} // namespace core
} // namespace livekit
