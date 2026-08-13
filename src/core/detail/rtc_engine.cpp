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

#include "rtc_engine.h"
#include "internals.h"
#include "rtc_session.h"
#include "signal_client.h"

#include <algorithm>
#include <future>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
constexpr auto kAddTrackTimeout = std::chrono::seconds(10);
constexpr auto kDataChannelOpenTimeout = std::chrono::seconds(10);
constexpr auto kRecoveryRtcTimeout = std::chrono::seconds(15);

livekit::ReconnectReason ToReconnectReason(livekit::DisconnectReason reason) {
	if (reason == livekit::DisconnectReason::MEDIA_FAILURE) {
		return livekit::ReconnectReason::RR_PUBLISHER_FAILED;
	}
	if (reason == livekit::DisconnectReason::SIGNAL_CLOSE) {
		return livekit::ReconnectReason::RR_SIGNAL_DISCONNECTED;
	}
	return livekit::ReconnectReason::RR_UNKNOWN;
}
} // namespace

namespace livekit {
namespace core {

RtcEngine::RtcEngine() : is_subscriber_primary_(true) {}

RtcEngine::~RtcEngine() {
	std::cout << "RtcEngine::~RtcEngine()" << std::endl;
	room_listener_.store(nullptr);
	Disconnect();
}

livekit::JoinResponse RtcEngine::Connect(std::string url, std::string token,
                                         EngineOptions options) {
	Disconnect();
	{
		std::lock_guard<std::mutex> guard(access_token_mutex_);
		access_token_ = token;
	}
	{
		std::lock_guard<std::mutex> guard(connection_params_mutex_);
		connection_url_ = url;
		connection_options_ = options;
	}
	recovery_stop_ = false;
	recovering_connection_ = false;
	force_full_reconnect_ = false;
	recovery_failure_reason_ = livekit::DisconnectReason::UNKNOWN_REASON;
	{
		std::lock_guard<std::mutex> guard(rtc_connected_mutex_);
		rtc_connected_ = false;
	}

	livekit::JoinResponse response = ConnectTransport(url, token, options);
	PLOG_DEBUG << "received JoinResponse: " << response.room().name();
	if (!response.has_room()) {
		ResetTransport(false);
		return response;
	}
	recovery_allowed_ = true;
	return response;
}

livekit::JoinResponse RtcEngine::ConnectTransport(const std::string& url, const std::string& token,
                                                  const EngineOptions& options) {
	auto created = SignalClient::Create(url, token, options.signal_options);
	if (!created) {
		return {};
	}
	auto signal_client = std::shared_ptr<SignalClient>(std::move(created));
	signal_client->AddObserver(this);
	{
		std::lock_guard<std::mutex> guard(signal_client_lock_);
		signal_client_ = signal_client;
	}

	livekit::JoinResponse response = signal_client->Connect();
	if (!response.has_room() || recovery_stop_) {
		return {};
	}

	std::lock_guard<std::mutex> guard(session_lock_);
	join_resp_ = response;
	is_subscriber_primary_ = response.subscriber_primary();
	if (!peer_factory_) {
		peer_factory_ = PeerTransportFactory::Create();
	}
	rtc_session_ = RtcSession::Create(response, options, peer_factory_);
	if (!rtc_session_) {
		join_resp_.Clear();
		return livekit::JoinResponse();
	}
	rtc_session_->AddObserver(this);
	if (!is_subscriber_primary_ || response.fast_publish()) {
		initial_negotiation_thread_ = std::thread([this]() { negotiate(); });
	}

	return response;
}

bool RtcEngine::ResumeTransport(const std::string& url, const std::string& token,
                                const EngineOptions& options, livekit::DisconnectReason reason) {
	SignalOptions signal_options = options.signal_options;
	int ping_timeout = 0;
	int ping_interval = 0;
	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (!rtc_session_ || !join_resp_.has_participant()) {
			return false;
		}
		signal_options.participant_sid = join_resp_.participant().sid();
		ping_timeout = join_resp_.ping_timeout();
		ping_interval = join_resp_.ping_interval();
	}
	signal_options.reconnect = true;
	signal_options.reconnect_reason = static_cast<int>(ToReconnectReason(reason));

	auto created = SignalClient::Create(url, token, signal_options);
	if (!created) {
		return false;
	}
	auto resumed_signal = std::shared_ptr<SignalClient>(std::move(created));
	resumed_signal->SetPingConfig(ping_timeout, ping_interval);
	resumed_signal->AddObserver(this);

	std::shared_ptr<SignalClient> previous_signal;
	{
		std::lock_guard<std::mutex> guard(signal_client_lock_);
		previous_signal.swap(signal_client_);
		signal_client_ = resumed_signal;
	}
	if (previous_signal) {
		previous_signal->RemoveObserver();
		previous_signal->Close();
	}

	if (!resumed_signal->Resume() || recovery_stop_ || force_full_reconnect_) {
		return false;
	}
	const auto reconnect_response = resumed_signal->ReconnectResponseSnapshot();
	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (!rtc_session_ || !rtc_session_->UpdateConfiguration(reconnect_response)) {
			return false;
		}
		if (reconnect_response.has_server_info()) {
			join_resp_.mutable_server_info()->CopyFrom(reconnect_response.server_info());
		}
		if (reconnect_response.has_client_configuration()) {
			join_resp_.mutable_client_configuration()->CopyFrom(
			    reconnect_response.client_configuration());
		}
		if (reconnect_response.ice_servers_size() > 0) {
			join_resp_.clear_ice_servers();
			for (const auto& ice_server : reconnect_response.ice_servers()) {
				join_resp_.add_ice_servers()->CopyFrom(ice_server);
			}
		}
	}
	if (auto* listener = room_listener_.load()) {
		listener->SignalResumedEvent();
	}

	bool restart_publisher = false;
	{
		std::lock_guard<std::mutex> guard(rtc_connected_mutex_);
		rtc_connected_ = false;
	}
	publisher_answer_received_ = false;
	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (!rtc_session_) {
			return false;
		}
		restart_publisher = rtc_session_->ShouldRestartPublisherIce();
		if (!rtc_session_->RestartPublisherIce()) {
			return false;
		}
	}
	if (!restart_publisher) {
		return !recovery_stop_ && !force_full_reconnect_;
	}

	std::unique_lock<std::mutex> guard(rtc_connected_mutex_);
	const bool connected = rtc_connected_cv_.wait_for(guard, kRecoveryRtcTimeout, [this]() {
		if (recovery_stop_.load() || force_full_reconnect_.load()) {
			return true;
		}
		if (!publisher_answer_received_.load()) {
			return false;
		}
		std::lock_guard<std::mutex> session_guard(session_lock_);
		return rtc_session_ != nullptr && rtc_session_->IsConnected();
	});
	return connected && !recovery_stop_ && !force_full_reconnect_;
}

void RtcEngine::Disconnect() {
	recovery_allowed_ = false;
	recovery_stop_ = true;
	rtc_connected_cv_.notify_all();
	ResetTransport(true);
	StopRecovery();
	ResetTransport(false);
	{
		std::lock_guard<std::mutex> guard(access_token_mutex_);
		access_token_.clear();
	}
	{
		std::lock_guard<std::mutex> guard(connection_params_mutex_);
		connection_url_.clear();
		connection_options_ = {};
	}
}

void RtcEngine::ResetTransport(bool send_leave) {
	// Signal callbacks run on the WebSocket service thread. Detach and stop that thread before
	// releasing the RTC session it may call into.
	std::shared_ptr<SignalClient> signal_client;
	{
		std::lock_guard<std::mutex> guard(signal_client_lock_);
		signal_client.swap(signal_client_);
	}
	if (signal_client) {
		signal_client->RemoveObserver();
		if (send_leave) {
			signal_client->SendLeave();
		}
		signal_client->Close();
	}

	{
		std::lock_guard<std::mutex> guard(initial_negotiation_mutex_);
		if (initial_negotiation_thread_.joinable()) {
			initial_negotiation_thread_.join();
		}
	}
	unregisterDataChannels();

	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		lossyDC_ = nullptr;
		reliableDC_ = nullptr;
	}

	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (rtc_session_) {
			rtc_session_->RemoveObserver();
			rtc_session_.reset();
		}
		join_resp_.Clear();
	}

	{
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		pending_track_resolvers_.clear();
	}
}

std::shared_ptr<SignalClient> RtcEngine::SignalClientSnapshot() const {
	std::lock_guard<std::mutex> guard(signal_client_lock_);
	return signal_client_;
}

void RtcEngine::StopRecovery() {
	recovery_stop_ = true;
	rtc_connected_cv_.notify_all();
	std::thread recovery_thread;
	{
		std::lock_guard<std::mutex> guard(recovery_thread_mutex_);
		if (recovery_thread_.joinable() &&
		    recovery_thread_.get_id() != std::this_thread::get_id()) {
			recovery_thread = std::move(recovery_thread_);
		}
	}
	if (recovery_thread.joinable()) {
		recovery_thread.join();
	}
	recovery_in_progress_ = false;
	recovering_connection_ = false;
	force_full_reconnect_ = false;
}

void RtcEngine::StartRecovery(livekit::DisconnectReason reason, bool force_full_reconnect) {
	if (!recovery_allowed_ || recovery_stop_) {
		return;
	}
	recovery_failure_reason_ = reason;
	if (force_full_reconnect) {
		force_full_reconnect_ = true;
	}
	bool expected = false;
	if (!recovery_in_progress_.compare_exchange_strong(expected, true)) {
		if (force_full_reconnect) {
			rtc_connected_cv_.notify_all();
			if (auto* listener = room_listener_.load()) {
				listener->ReconnectingEvent(true);
			}
		}
		return;
	}
	recovering_connection_ = true;
	{
		std::lock_guard<std::mutex> guard(rtc_connected_mutex_);
		rtc_connected_ = false;
	}
	if (auto* listener = room_listener_.load()) {
		listener->ReconnectingEvent(force_full_reconnect);
	}

	std::thread completed_thread;
	{
		std::lock_guard<std::mutex> guard(recovery_thread_mutex_);
		if (recovery_thread_.joinable()) {
			completed_thread = std::move(recovery_thread_);
		}
	}
	if (completed_thread.joinable()) {
		completed_thread.join();
	}
	std::lock_guard<std::mutex> guard(recovery_thread_mutex_);
	recovery_thread_ = std::thread([this]() { RunRecovery(); });
}

void RtcEngine::RunRecovery() {
	std::string url;
	EngineOptions options;
	{
		std::lock_guard<std::mutex> guard(connection_params_mutex_);
		url = connection_url_;
		options = connection_options_;
	}
	const auto attempts = (std::max)(uint32_t{1}, options.join_retries);
	livekit::JoinResponse recovered_response;
	bool recovered = false;

	if (!force_full_reconnect_ && !recovery_stop_) {
		std::string token;
		{
			std::lock_guard<std::mutex> guard(access_token_mutex_);
			token = access_token_;
		}
		if (!url.empty() && !token.empty()) {
			recovered = ResumeTransport(url, token, options, recovery_failure_reason_.load());
		}
		if (recovered && !force_full_reconnect_) {
			if (auto* listener = room_listener_.load()) {
				listener->ResumedEvent();
			}
			recovering_connection_ = false;
			recovery_in_progress_ = false;
			return;
		}
	}

	force_full_reconnect_ = true;
	if (!recovery_stop_) {
		if (auto* listener = room_listener_.load()) {
			listener->ReconnectingEvent(true);
		}
	}
	recovered = false;

	for (uint32_t attempt = 0; attempt < attempts && !recovery_stop_; ++attempt) {
		{
			std::lock_guard<std::mutex> guard(rtc_connected_mutex_);
			rtc_connected_ = false;
		}
		ResetTransport(false);
		std::string token;
		{
			std::lock_guard<std::mutex> guard(access_token_mutex_);
			token = access_token_;
		}
		if (url.empty() || token.empty()) {
			break;
		}

		options.signal_options.reconnect = false;
		recovered_response = ConnectTransport(url, token, options);
		if (recovered_response.has_room()) {
			std::unique_lock<std::mutex> guard(rtc_connected_mutex_);
			recovered = rtc_connected_cv_.wait_for(guard, kRecoveryRtcTimeout, [this]() {
				return rtc_connected_ || recovery_stop_.load();
			});
			recovered = recovered && rtc_connected_ && !recovery_stop_;
		}
		if (recovered) {
			break;
		}
		if (attempt + 1 < attempts && !recovery_stop_) {
			std::unique_lock<std::mutex> guard(rtc_connected_mutex_);
			rtc_connected_cv_.wait_for(guard, std::chrono::seconds(attempt + 1),
			                           [this]() { return recovery_stop_.load(); });
		}
	}

	if (recovered) {
		if (auto* listener = room_listener_.load()) {
			listener->ReconnectedEvent(std::move(recovered_response));
		}
	} else if (!recovery_stop_) {
		ResetTransport(false);
		recovery_allowed_ = false;
		if (auto* listener = room_listener_.load()) {
			listener->SignalDisconnectedEvent(recovery_failure_reason_.load());
		}
	}
	force_full_reconnect_ = false;
	recovering_connection_ = false;
	recovery_in_progress_ = false;
}

void RtcEngine::SetRoomObserver(RtcEngineListener* listener) { room_listener_.store(listener); }

void RtcEngine::OnAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) {
	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (rtc_session_) {
			rtc_session_->SetPublisherAnswer(std::move(answer));
		}
	}
	if (recovering_connection_) {
		publisher_answer_received_ = true;
		rtc_connected_cv_.notify_all();
	}
	return;
}

std::shared_ptr<PeerTransportFactory> RtcEngine::GetSessionPeerTransportFactory() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		return rtc_session_->GetPeerTransportFactory();
	}
	return nullptr;
}

std::optional<livekit::TrackInfo> RtcEngine::AddTrack(const livekit::AddTrackRequest& req) {
	if (req.cid().empty()) {
		throw std::runtime_error("cid is empty");
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		throw std::runtime_error("signal client is not connected");
	}

	std::promise<livekit::TrackInfo> promise;
	std::future<livekit::TrackInfo> future = promise.get_future();
	{
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		if (pending_track_resolvers_.count(req.cid()) > 0) {
			throw std::runtime_error("a track with the same ID has already been published");
		}
		pending_track_resolvers_[req.cid()] = std::move(promise);
	}

	try {
		signal_client->SendAddTrack(req);
		auto status = future.wait_for(kAddTrackTimeout);
		if (status == std::future_status::timeout) {
			std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
			pending_track_resolvers_.erase(req.cid());
			return std::nullopt;
		}
		return future.get();
	} catch (...) {
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		pending_track_resolvers_.erase(req.cid());
		throw;
	}
}

webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
RtcEngine::CreateSender(LocalTrack* track, TrackPublishOptions options,
                        std::vector<webrtc::RtpEncodingParameters> send_encodings) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		return rtc_session_->CreateSender(track, options, send_encodings);
	}
	return nullptr;
}

bool RtcEngine::RemoveSender(LocalTrack* track) {
	std::lock_guard<std::mutex> guard(session_lock_);
	return rtc_session_ != nullptr && rtc_session_->RemoveSender(track);
}

void RtcEngine::PublisherNegotiationNeeded() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		return rtc_session_->PublisherNegotiationNeeded();
	}
}

bool RtcEngine::SendDataPacket(const livekit::DataPacket& packet, bool reliable) {
	webrtc::scoped_refptr<webrtc::DataChannelInterface> channel;
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		channel = reliable ? reliableDC_ : lossyDC_;
	}
	if (!channel) {
		std::lock_guard<std::mutex> guard(initial_negotiation_mutex_);
		if (initial_negotiation_thread_.joinable()) {
			initial_negotiation_thread_.join();
		}
	}
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		channel = reliable ? reliableDC_ : lossyDC_;
	}
	if (!channel) {
		negotiate();
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		channel = reliable ? reliableDC_ : lossyDC_;
	}
	if (!channel) {
		return false;
	}

	const auto deadline = std::chrono::steady_clock::now() + kDataChannelOpenTimeout;
	while (channel->state() == webrtc::DataChannelInterface::kConnecting &&
	       std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (channel->state() != webrtc::DataChannelInterface::kOpen) {
		return false;
	}

	std::string serialized;
	if (!packet.SerializeToString(&serialized)) {
		return false;
	}
	return channel->Send(
	    webrtc::DataBuffer(webrtc::CopyOnWriteBuffer(serialized.data(), serialized.size()), true));
}

bool RtcEngine::SetTrackMuted(const std::string& track_sid, bool muted) {
	if (track_sid.empty()) {
		return false;
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	signal_client->SendMuteTrack(track_sid, muted);
	return true;
}

bool RtcEngine::SetTrackSubscribed(const std::string& participant_sid, const std::string& track_sid,
                                   bool subscribed) {
	if (participant_sid.empty() || track_sid.empty()) {
		return false;
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	livekit::UpdateSubscription update;
	update.set_subscribe(subscribed);
	update.add_track_sids(track_sid);
	auto* participant_tracks = update.add_participant_tracks();
	participant_tracks->set_participant_sid(participant_sid);
	participant_tracks->add_track_sids(track_sid);
	signal_client->SendUpdateSubscription(update);
	return true;
}

bool RtcEngine::UpdateLocalMetadata(const std::string& metadata, const std::string& name,
                                    const std::map<std::string, std::string>& attributes) {
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	signal_client->SendUpdateLocalMetadata(metadata, name, attributes);
	return true;
}

std::string RtcEngine::AccessTokenForReconnect() const {
	std::lock_guard<std::mutex> guard(access_token_mutex_);
	return access_token_;
}

void RtcEngine::SendSyncState(
    const std::vector<livekit::TrackPublishedResponse>& published_tracks) {
	livekit::SyncState sync;
	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (!rtc_session_) {
			return;
		}
		rtc_session_->PopulateSyncState(sync);
	}
	{
		std::lock_guard<std::mutex> guard(connection_params_mutex_);
		sync.mutable_subscription()->set_subscribe(
		    !connection_options_.signal_options.auto_subscribe);
	}
	for (const auto& track : published_tracks) {
		sync.add_publish_tracks()->CopyFrom(track);
	}
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		for (const auto& channel : {lossyDC_, reliableDC_}) {
			if (!channel || channel->id() < 0) {
				continue;
			}
			auto* info = sync.add_data_channels();
			info->set_label(channel->label());
			info->set_id(static_cast<uint32_t>(channel->id()));
			info->set_target(livekit::SignalTarget::PUBLISHER);
		}
	}
	if (auto signal_client = SignalClientSnapshot()) {
		signal_client->SendSyncState(sync);
	}
}

bool RtcEngine::SimulateSignalDisconnectForTesting() {
	auto signal_client = SignalClientSnapshot();
	if (!signal_client || !recovery_allowed_) {
		return false;
	}
	signal_client->SimulateDisconnectForTesting();
	return true;
}

bool RtcEngine::SimulateFullReconnectForTesting() {
	if (!recovery_allowed_ || recovery_stop_) {
		return false;
	}
	StartRecovery(livekit::DisconnectReason::SIGNAL_CLOSE, true);
	return true;
}

void RtcEngine::OnLeave(const livekit::LeaveRequest leave) {
	if (leave.action() == livekit::LeaveRequest::DISCONNECT) {
		recovery_allowed_ = false;
		if (auto* listener = room_listener_.load()) {
			listener->SignalDisconnectedEvent(leave.reason());
		}
		return;
	}
	const auto reason = leave.reason() == livekit::DisconnectReason::UNKNOWN_REASON
	                        ? livekit::DisconnectReason::SIGNAL_CLOSE
	                        : leave.reason();
	StartRecovery(reason, leave.action() == livekit::LeaveRequest::RECONNECT);
}

void RtcEngine::OnLocalTrackPublished(const livekit::TrackPublishedResponse& response) {
	std::cout << "received trackPublishedResponse:" << response.cid() << "; "
	          << response.track().sid() << std::endl;

	auto& cid = response.cid();
	{
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		auto it = pending_track_resolvers_.find(cid);
		if (it == pending_track_resolvers_.end()) {
			std::cerr << "missing track resolver for " << cid << std::endl;
			return;
		}
		auto& promise = it->second;
		promise.set_value(response.track());
		pending_track_resolvers_.erase(it);
	}
	return;
}

void RtcEngine::OnLocalTrackUnpublished(const livekit::TrackUnpublishedResponse& response) {
	if (auto* listener = room_listener_.load()) {
		listener->LocalTrackUnpublishedEvent(response.track_sid());
	}
}

void RtcEngine::OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		auto answer = rtc_session_->CreateSubscriberAnswerFromOffer(std::move(offer));
		if (answer) {
			if (auto signal_client = SignalClientSnapshot()) {
				signal_client->SendAnswer(std::move(answer));
			}
		}
	}
	return;
}
void RtcEngine::OnRemoteMuteChanged(std::string sid, bool muted) {
	if (auto* listener = room_listener_.load()) {
		listener->RemoteMuteChangedEvent(sid, muted);
	}
}
void RtcEngine::OnSubscribedQualityUpdate(const livekit::SubscribedQualityUpdate& update) {
	return;
}
void RtcEngine::OnTokenRefresh(const std::string& token) {
	if (token.empty()) {
		return;
	}
	std::lock_guard<std::mutex> guard(access_token_mutex_);
	access_token_ = token;
}

void RtcEngine::OnTrickle(std::string& candidate, livekit::SignalTarget target) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		rtc_session_->AddIceCandidate(candidate, target);
	}
	return;
}
void RtcEngine::OnClose() {
	if (recovery_allowed_ && !recovery_stop_) {
		StartRecovery(livekit::DisconnectReason::SIGNAL_CLOSE);
	} else if (!recovery_in_progress_ && !recovery_stop_) {
		if (auto* listener = room_listener_.load()) {
			listener->SignalDisconnectedEvent(livekit::DisconnectReason::SIGNAL_CLOSE);
		}
	}
}
void RtcEngine::OnParticipantUpdate(const std::vector<livekit::ParticipantInfo>& updates) {
	if (auto* listener = room_listener_.load()) {
		listener->ParticipantUpdateEvent(updates);
	}
}
void RtcEngine::OnSpeakersChanged(std::vector<livekit::SpeakerInfo>& update) {
	if (auto* listener = room_listener_.load()) {
		listener->SpeakersChangedEvent(update);
	}
}
void RtcEngine::OnRoomUpdate(const livekit::Room& update) {
	if (auto* listener = room_listener_.load()) {
		listener->RoomUpdateEvent(update);
	}
}
void RtcEngine::OnConnectionQuality(const std::vector<livekit::ConnectionQualityInfo>& update) {
	if (auto* listener = room_listener_.load()) {
		listener->ConnectionQualityEvent(update);
	}
}
void RtcEngine::OnStreamStateUpdate(const std::vector<livekit::StreamStateInfo>& update) { return; }
void RtcEngine::OnSubscriptionPermissionUpdate(
    const livekit::SubscriptionPermissionUpdate& update) {
	return;
}
void RtcEngine::OnSubscriptionError(const livekit::SubscriptionResponse& response) { return; }
void RtcEngine::OnRequestResponse(const livekit::RequestResponse& response) { return; }
void RtcEngine::OnLocalTrackSubscribed(const std::string& track_sid) { return; }

void RtcEngine::OnLocalOffer(PeerTransport::Target target,
                             std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	if (auto signal_client = SignalClientSnapshot()) {
		signal_client->SendOffer(std::move(offer));
	}
}

void RtcEngine::OnStateChange(RtcSession::State connection_state,
                              webrtc::PeerConnectionInterface::PeerConnectionState pub_state,
                              webrtc::PeerConnectionInterface::PeerConnectionState sub_state) {
	std::cout << "RtcEngine::OnStateChange()" << int(connection_state) << std::endl;
	if (connection_state == RtcSession::State::kConnected) {
		{
			std::lock_guard<std::mutex> guard(rtc_connected_mutex_);
			rtc_connected_ = true;
		}
		rtc_connected_cv_.notify_all();
		if (!recovering_connection_) {
			if (auto* listener = room_listener_.load()) {
				listener->ConnectedEvent(this->join_resp_);
			}
		}
	} else if (connection_state == RtcSession::State::kFailed && recovery_allowed_ &&
	           !recovery_stop_) {
		StartRecovery(livekit::DisconnectReason::MEDIA_FAILURE, true);
	}
}

void RtcEngine::OnSignalingChange(PeerTransport::Target target,
                                  webrtc::PeerConnectionInterface::SignalingState newState) {}

void RtcEngine::OnConnectionChange(PeerTransport::Target target,
                                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
}

void RtcEngine::OnAddStream(PeerTransport::Target target,
                            webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcEngine::OnRemoveStream(PeerTransport::Target target,
                               webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcEngine::OnDataChannel(PeerTransport::Target target,
                              webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {
	registerDataChannel(std::move(dataChannel));
}

void RtcEngine::OnRenegotiationNeeded(PeerTransport::Target target) {}

void RtcEngine::OnIceConnectionChange(
    PeerTransport::Target target, webrtc::PeerConnectionInterface::IceConnectionState newState) {}

void RtcEngine::OnIceGatheringChange(PeerTransport::Target target,
                                     webrtc::PeerConnectionInterface::IceGatheringState newState) {}

void RtcEngine::OnIceCandidate(PeerTransport::Target target,
                               const webrtc::IceCandidateInterface* candidate) {

	std::string candidate_str;
	candidate->ToString(&candidate_str);

	nlohmann::json candidate_json;
	candidate_json["candidate"] = candidate_str;
	candidate_json["sdpMid"] = candidate->sdp_mid();
	candidate_json["sdpMLineIndex"] = candidate->sdp_mline_index();

	auto candidate_json_str = candidate_json.dump();
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return;
	}

	if (target == PeerTransport::Target::PUBLISHER) {
		signal_client->SendIceCandidate(candidate_json_str, livekit::SignalTarget::PUBLISHER);
	} else if (target == PeerTransport::Target::SUBSCRIBER) {
		signal_client->SendIceCandidate(candidate_json_str, livekit::SignalTarget::SUBSCRIBER);
	}

	return;
}

void RtcEngine::OnIceCandidatesRemoved(PeerTransport::Target target,
                                       const std::vector<webrtc::Candidate>& candidates) {}

void RtcEngine::OnIceConnectionReceivingChange(PeerTransport::Target target, bool receiving) {}

void RtcEngine::OnIceCandidateError(PeerTransport::Target target, const std::string& address,
                                    int port, const std::string& url, int error_code,
                                    const std::string& error_text) {}

void RtcEngine::OnAddTrack(
    PeerTransport::Target target, webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {}

void RtcEngine::OnTrack(PeerTransport::Target target,
                        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
	if (target != PeerTransport::Target::SUBSCRIBER || !transceiver || !transceiver->receiver()) {
		return;
	}
	auto track = transceiver->receiver()->track();
	if (track) {
		if (auto* listener = room_listener_.load()) {
			listener->MediaTrackEvent(std::move(track));
		}
	}
}

void RtcEngine::OnRemoveTrack(PeerTransport::Target target,
                              webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {}

void RtcEngine::OnInterestingUsage(PeerTransport::Target target, int usagePattern) {}

void RtcEngine::negotiate() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (!rtc_session_) {
		return;
	}
	bool has_publisher_data_channels = false;
	{
		std::lock_guard<std::mutex> data_guard(data_channels_lock_);
		has_publisher_data_channels = lossyDC_ && reliableDC_;
	}
	if (!has_publisher_data_channels) {
		this->createDataChannels();
	}
	rtc_session_->Negotiate();
}

void RtcEngine::createDataChannels() {
	if (!this->rtc_session_) {
		return;
	}
	struct webrtc::DataChannelInit lossy_init;
	lossy_init.ordered = true;
	lossy_init.reliable = false;
	lossy_init.maxRetransmits = 0;
	auto lossy_channel = this->rtc_session_->CreateDataChannel("_lossy", &lossy_init);
	registerDataChannel(std::move(lossy_channel), true);

	struct webrtc::DataChannelInit reliable_init;
	reliable_init.ordered = true;
	reliable_init.reliable = true;
	auto reliable_channel = this->rtc_session_->CreateDataChannel("_reliable", &reliable_init);
	registerDataChannel(std::move(reliable_channel), true);
}

void RtcEngine::registerDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> channel,
                                    bool publisher_channel) {
	if (!channel) {
		return;
	}
	std::lock_guard<std::mutex> guard(data_channels_lock_);
	for (const auto& existing : data_channels_) {
		if (existing.get() == channel.get()) {
			return;
		}
	}
	channel->RegisterObserver(this);
	data_channels_.push_back(channel);
	if (publisher_channel && channel->label() == "_reliable") {
		reliableDC_ = channel;
	} else if (publisher_channel && channel->label() == "_lossy") {
		lossyDC_ = channel;
	}
}

void RtcEngine::unregisterDataChannels() {
	std::lock_guard<std::mutex> guard(data_channels_lock_);
	for (const auto& channel : data_channels_) {
		channel->UnregisterObserver();
	}
	data_channels_.clear();
}

void RtcEngine::OnStateChange() {}

void RtcEngine::OnMessage(const webrtc::DataBuffer& buffer) {
	if (buffer.data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		return;
	}
	livekit::DataPacket packet;
	if (!packet.ParseFromArray(buffer.data.data(), static_cast<int>(buffer.data.size()))) {
		return;
	}
	if (auto* listener = room_listener_.load()) {
		listener->DataPacketEvent(packet);
	}
}

void RtcEngine::OnBufferedAmountChange(uint64_t sent_data_size) {}

} // namespace core
} // namespace livekit
