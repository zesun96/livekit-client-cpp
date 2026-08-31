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
#include "../e2ee/e2ee_manager_internal.h"
#include "data_track_proto.h"
#include "internals.h"
#include "rtc_session.h"
#include "signal_client.h"
#include "tracing.h"

#include "rtc_base/crypto_random.h"

#include <algorithm>
#include <future>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
constexpr auto kAddTrackTimeout = std::chrono::seconds(10);
constexpr auto kDataChannelOpenTimeout = std::chrono::seconds(10);
constexpr auto kDataChannelDrainTimeout = std::chrono::seconds(10);
constexpr uint64_t kDataChannelHighWaterMark = 4ULL * 1024 * 1024;
constexpr uint64_t kDataChannelLowWaterMark = 1ULL * 1024 * 1024;
constexpr uint64_t kDataTrackHighWaterMark = 1ULL * 1024 * 1024;
constexpr auto kDataTrackRequestTimeout = std::chrono::seconds(10);
constexpr auto kRpcAckTimeout = std::chrono::milliseconds(7'000);
constexpr auto kMinimumRpcTimeout = std::chrono::milliseconds(8'000);
constexpr uint32_t kRpcVersion = 1;

std::string RpcErrorMessage(livekit::core::RpcErrorCode code) {
	using livekit::core::RpcErrorCode;
	switch (code) {
	case RpcErrorCode::UnsupportedMethod:
		return "Method not supported at destination";
	case RpcErrorCode::RecipientNotFound:
		return "Recipient not found";
	case RpcErrorCode::RequestPayloadTooLarge:
		return "Request payload too large";
	case RpcErrorCode::UnsupportedServer:
		return "RPC not supported by server";
	case RpcErrorCode::UnsupportedVersion:
		return "Unsupported RPC version";
	case RpcErrorCode::ApplicationError:
		return "Application error in method handler";
	case RpcErrorCode::ConnectionTimeout:
		return "Connection timeout";
	case RpcErrorCode::ResponseTimeout:
		return "Response timeout";
	case RpcErrorCode::RecipientDisconnected:
		return "Recipient disconnected";
	case RpcErrorCode::ResponsePayloadTooLarge:
		return "Response payload too large";
	case RpcErrorCode::SendFailed:
		return "Failed to send";
	}
	return "RPC error";
}

std::string TruncateUtf8(const std::string& value, std::size_t maximum_bytes) {
	if (value.size() <= maximum_bytes) {
		return value;
	}
	std::size_t end = maximum_bytes;
	while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) {
		--end;
	}
	return value.substr(0, end);
}

livekit::ReconnectReason ToReconnectReason(livekit::DisconnectReason reason) {
	if (reason == livekit::DisconnectReason::MEDIA_FAILURE) {
		return livekit::ReconnectReason::RR_PUBLISHER_FAILED;
	}
	if (reason == livekit::DisconnectReason::SIGNAL_CLOSE) {
		return livekit::ReconnectReason::RR_SIGNAL_DISCONNECTED;
	}
	return livekit::ReconnectReason::RR_UNKNOWN;
}

livekit::core::ReconnectReason ToPublicReconnectReason(livekit::DisconnectReason reason) {
	if (reason == livekit::DisconnectReason::MEDIA_FAILURE) {
		return livekit::core::ReconnectReason::MediaFailure;
	}
	if (reason == livekit::DisconnectReason::SIGNAL_CLOSE) {
		return livekit::core::ReconnectReason::SignalDisconnected;
	}
	return livekit::core::ReconnectReason::Unknown;
}

const char* ReconnectReasonName(livekit::DisconnectReason reason) {
	if (reason == livekit::DisconnectReason::MEDIA_FAILURE) {
		return "media_failure";
	}
	if (reason == livekit::DisconnectReason::SIGNAL_CLOSE) {
		return "signal_disconnected";
	}
	return "unknown";
}

livekit::core::DataTrackError RequestResponseError(const livekit::RequestResponse& response) {
	using Code = livekit::core::DataTrackErrorCode;
	Code code = Code::ProtocolError;
	switch (response.reason()) {
	case livekit::RequestResponse::NOT_FOUND:
		code = Code::NotFound;
		break;
	case livekit::RequestResponse::NOT_ALLOWED:
		code = Code::NotAllowed;
		break;
	case livekit::RequestResponse::LIMIT_EXCEEDED:
		code = Code::HandleLimitReached;
		break;
	case livekit::RequestResponse::INVALID_HANDLE:
	case livekit::RequestResponse::DUPLICATE_HANDLE:
		code = Code::ProtocolError;
		break;
	case livekit::RequestResponse::INVALID_NAME:
		code = Code::InvalidName;
		break;
	case livekit::RequestResponse::DUPLICATE_NAME:
		code = Code::DuplicateName;
		break;
	case livekit::RequestResponse::INVALID_REQUEST:
	case livekit::RequestResponse::UNSUPPORTED_TYPE:
		code = Code::InvalidSchema;
		break;
	default:
		break;
	}
	return {code, response.message().empty() ? "data track request rejected" : response.message()};
}
} // namespace

namespace livekit {
namespace core {

namespace {

bool MakeEncryptablePayload(const livekit::DataPacket& packet,
                            livekit::EncryptedPacketPayload& payload) {
	if (packet.has_user()) {
		payload.mutable_user()->CopyFrom(packet.user());
	} else if (packet.has_chat_message()) {
		payload.mutable_chat_message()->CopyFrom(packet.chat_message());
	} else if (packet.has_rpc_request()) {
		payload.mutable_rpc_request()->CopyFrom(packet.rpc_request());
	} else if (packet.has_rpc_ack()) {
		payload.mutable_rpc_ack()->CopyFrom(packet.rpc_ack());
	} else if (packet.has_rpc_response()) {
		payload.mutable_rpc_response()->CopyFrom(packet.rpc_response());
	} else if (packet.has_stream_header()) {
		payload.mutable_stream_header()->CopyFrom(packet.stream_header());
	} else if (packet.has_stream_chunk()) {
		payload.mutable_stream_chunk()->CopyFrom(packet.stream_chunk());
	} else if (packet.has_stream_trailer()) {
		payload.mutable_stream_trailer()->CopyFrom(packet.stream_trailer());
	} else {
		return false;
	}
	return true;
}

bool RestoreEncryptedPayload(const livekit::EncryptedPacketPayload& payload,
                             livekit::DataPacket& packet) {
	if (payload.has_user()) {
		packet.mutable_user()->CopyFrom(payload.user());
	} else if (payload.has_chat_message()) {
		packet.mutable_chat_message()->CopyFrom(payload.chat_message());
	} else if (payload.has_rpc_request()) {
		packet.mutable_rpc_request()->CopyFrom(payload.rpc_request());
	} else if (payload.has_rpc_ack()) {
		packet.mutable_rpc_ack()->CopyFrom(payload.rpc_ack());
	} else if (payload.has_rpc_response()) {
		packet.mutable_rpc_response()->CopyFrom(payload.rpc_response());
	} else if (payload.has_stream_header()) {
		packet.mutable_stream_header()->CopyFrom(payload.stream_header());
	} else if (payload.has_stream_chunk()) {
		packet.mutable_stream_chunk()->CopyFrom(payload.stream_chunk());
	} else if (payload.has_stream_trailer()) {
		packet.mutable_stream_trailer()->CopyFrom(payload.stream_trailer());
	} else {
		return false;
	}
	return true;
}

} // namespace

class RtcEngine::DataChannelObserverProxy final : public webrtc::DataChannelObserver {
public:
	DataChannelObserverProxy(RtcEngine* engine,
	                         webrtc::scoped_refptr<webrtc::DataChannelInterface> channel,
	                         bool reliable, bool monitor_buffer)
	    : engine_(engine), channel_(std::move(channel)), reliable_(reliable),
	      monitor_buffer_(monitor_buffer) {}

	void OnStateChange() override {
		engine_->OnDataChannelStateChange(channel_, reliable_, monitor_buffer_);
	}

	void OnMessage(const webrtc::DataBuffer& buffer) override {
		engine_->OnDataChannelMessage(channel_, buffer);
	}

	void OnBufferedAmountChange(uint64_t) override {
		engine_->OnDataChannelBufferedAmountChange(channel_, reliable_, monitor_buffer_);
	}

private:
	RtcEngine* engine_;
	webrtc::scoped_refptr<webrtc::DataChannelInterface> channel_;
	bool reliable_;
	bool monitor_buffer_;
};

RpcError RpcError::BuiltIn(RpcErrorCode code, std::string data) {
	return {code, RpcErrorMessage(code), std::move(data)};
}

RtcEngine::RtcEngine()
    : is_subscriber_primary_(true),
      data_channel_backpressure_(kDataChannelHighWaterMark, kDataChannelLowWaterMark) {
	StartRpcWorkers();
}

RtcEngine::~RtcEngine() {
	LKC_LOG_DEBUG << "RTC engine shutdown";
	room_listener_.store(nullptr);
	Disconnect();
	StopRpcWorkers();
}

livekit::JoinResponse RtcEngine::Connect(std::string url, std::string token,
                                         EngineOptions options) {
	LKC_TRACE_SPAN(TraceCategory::Transport, "transport.connect");
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
	LKC_LOG_DEBUG << "received join response for room=" << response.room().name();
	if (!response.has_room()) {
		ResetTransport(false);
		return response;
	}
	recovery_allowed_ = true;
	return response;
}

livekit::JoinResponse RtcEngine::ConnectTransport(const std::string& url, const std::string& token,
                                                  const EngineOptions& options) {
	LKC_TRACE_SPAN(TraceCategory::Transport, "transport.create");
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
	{
		std::lock_guard<std::mutex> participants_guard(rpc_participants_mutex_);
		rpc_participant_identities_.clear();
		if (!response.participant().identity().empty()) {
			rpc_participant_identities_.insert(response.participant().identity());
		}
		for (const auto& participant : response.other_participants()) {
			if (!participant.identity().empty() &&
			    participant.state() != livekit::ParticipantInfo_State_DISCONNECTED) {
				rpc_participant_identities_.insert(participant.identity());
			}
		}
	}
	is_subscriber_primary_ = response.subscriber_primary();
	std::shared_ptr<PeerTransportFactory> peer_factory;
	{
		std::lock_guard<std::mutex> factory_guard(peer_factory_lock_);
		if (!peer_factory_) {
			peer_factory_ = PeerTransportFactory::Create();
		}
		peer_factory = peer_factory_;
	}
	rtc_session_ = RtcSession::Create(response, options, std::move(peer_factory));
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
	LKC_TRACE_SPAN(TraceCategory::Transport, "transport.resume");
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
	const auto reconnect_timeout =
	    (std::max)(options.reconnect_timeout, std::chrono::milliseconds(1));
	const bool connected = rtc_connected_cv_.wait_for(guard, reconnect_timeout, [this]() {
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
	CancelPendingRpc(std::nullopt, RpcErrorCode::RecipientDisconnected);
	std::vector<std::shared_ptr<PendingDataTrackRequest>> pending_data_tracks;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		for (const auto& [handle, pending] : pending_data_track_publishes_) {
			pending_data_tracks.push_back(pending);
		}
		for (const auto& [handle, pending] : pending_data_track_unpublishes_) {
			pending_data_tracks.push_back(pending);
		}
		pending_data_track_publishes_.clear();
		pending_data_track_unpublishes_.clear();
		published_data_tracks_.clear();
		data_track_packetizers_.clear();
		incoming_data_track_routes_.clear();
		data_track_depacketizers_.clear();
	}
	for (const auto& pending : pending_data_tracks) {
		{
			std::lock_guard<std::mutex> guard(pending->mutex);
			pending->completed = true;
			pending->error = {DataTrackErrorCode::Disconnected, "room is disconnected"};
		}
		pending->cv.notify_all();
	}
	CancelPendingDataBlobRequests({DataTrackErrorCode::Disconnected, "room is disconnected"});
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

void RtcEngine::CancelPendingDataBlobRequests(DataTrackError error) {
	std::vector<std::shared_ptr<PendingDataBlobRequest>> pending_requests;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		for (const auto& [request_id, pending] : pending_data_blob_requests_) {
			pending_requests.push_back(pending);
		}
		pending_data_blob_requests_.clear();
	}
	for (const auto& pending : pending_requests) {
		{
			std::lock_guard<std::mutex> guard(pending->mutex);
			pending->completed = true;
			pending->error = error;
		}
		pending->cv.notify_all();
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
		dataTrackDC_ = nullptr;
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
	{
		std::lock_guard<std::mutex> guard(rpc_participants_mutex_);
		rpc_participant_identities_.clear();
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
			LKC_TRACE_INSTANT(TraceCategory::Transport, "connection.recovery.escalated");
			rtc_connected_cv_.notify_all();
			if (auto* listener = room_listener_.load()) {
				listener->ReconnectingEvent(true);
			}
		}
		return;
	}
	if (detail::IsTraceEnabled(TraceCategory::Transport)) {
		const auto trace_id = detail::NextTraceCorrelationId();
		recovery_trace_id_.store(trace_id);
		detail::EmitTrace(TraceCategory::Transport, TracePhase::AsyncBegin, "connection.recovery",
		                  trace_id);
	}
	LKC_LOG_INFO << "connection recovery started: reason=" << ReconnectReasonName(reason)
	             << ", force_full_reconnect=" << force_full_reconnect;
	recovering_connection_ = true;
	CancelPendingDataBlobRequests(
	    {DataTrackErrorCode::Disconnected, "connection is recovering; retry the schema request"});
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
	auto close_recovery_trace = [this] {
		const auto trace_id = recovery_trace_id_.exchange(0);
		if (trace_id != 0) {
			detail::EmitTrace(TraceCategory::Transport, TracePhase::AsyncEnd, "connection.recovery",
			                  trace_id);
		}
	};
	std::string url;
	EngineOptions options;
	{
		std::lock_guard<std::mutex> guard(connection_params_mutex_);
		url = connection_url_;
		options = connection_options_;
	}
	const auto attempts = (std::max)(uint32_t{1}, options.join_retries);
	const auto recovery_started = std::chrono::steady_clock::now();
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
			LKC_TRACE_INSTANT(TraceCategory::Transport, "connection.recovery.resumed");
			LKC_LOG_INFO << "connection recovery completed with signal resume";
			if (auto* listener = room_listener_.load()) {
				listener->ResumedEvent();
			}
			recovering_connection_ = false;
			recovery_in_progress_ = false;
			close_recovery_trace();
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
		LKC_TRACE_INSTANT(TraceCategory::Transport, "connection.recovery.full_attempt");
		ReconnectContext context;
		context.retry_count = attempt;
		context.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		    std::chrono::steady_clock::now() - recovery_started);
		context.reason = ToPublicReconnectReason(recovery_failure_reason_.load());
		context.server_url = url;
		std::optional<std::chrono::milliseconds> retry_delay;
		try {
			retry_delay = options.reconnect_policy != nullptr
			                  ? options.reconnect_policy->NextRetryDelay(context)
			                  : CreateDefaultReconnectPolicy()->NextRetryDelay(context);
		} catch (const std::exception& error) {
			LKC_LOG_ERROR << "connection recovery policy failed: " << error.what();
			break;
		} catch (...) {
			LKC_LOG_ERROR << "connection recovery policy failed with an unknown exception";
			break;
		}
		if (!retry_delay.has_value()) {
			LKC_LOG_WARNING << "connection recovery policy stopped retries after " << attempt
			                << " attempt(s)";
			break;
		}
		LKC_LOG_INFO << "full reconnect attempt " << (attempt + 1) << "/" << attempts
		             << ", delay_ms=" << retry_delay->count();
		if (*retry_delay > std::chrono::milliseconds::zero()) {
			std::unique_lock<std::mutex> guard(rtc_connected_mutex_);
			rtc_connected_cv_.wait_for(guard, *retry_delay,
			                           [this]() { return recovery_stop_.load(); });
			if (recovery_stop_) {
				break;
			}
		}
		{
			std::lock_guard<std::mutex> guard(rtc_connected_mutex_);
			rtc_connected_ = false;
		}
		ResetTransport(false);
		std::string token;
		if (options.token_source) {
			auto credentials = options.token_source->Fetch(options.token_source_options, true);
			if (!credentials) {
				LKC_LOG_WARNING << "full reconnect token source failed: " << credentials.error;
				continue;
			}
			url = std::move(credentials.response.server_url);
			token = std::move(credentials.response.participant_token);
			{
				std::lock_guard<std::mutex> guard(access_token_mutex_);
				access_token_ = token;
			}
			{
				std::lock_guard<std::mutex> guard(connection_params_mutex_);
				connection_url_ = url;
			}
			if (auto* listener = room_listener_.load()) {
				listener->TokenRefreshedEvent();
			}
		} else {
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
			const auto reconnect_timeout =
			    (std::max)(options.reconnect_timeout, std::chrono::milliseconds(1));
			recovered = rtc_connected_cv_.wait_for(guard, reconnect_timeout, [this]() {
				return rtc_connected_ || recovery_stop_.load();
			});
			recovered = recovered && rtc_connected_ && !recovery_stop_;
		}
		if (recovered) {
			break;
		}
	}

	if (recovered) {
		LKC_TRACE_INSTANT(TraceCategory::Transport, "connection.recovery.reconnected");
		LKC_LOG_INFO << "connection recovery completed with full reconnect in "
		             << std::chrono::duration_cast<std::chrono::milliseconds>(
		                    std::chrono::steady_clock::now() - recovery_started)
		                    .count()
		             << "ms";
		if (auto* listener = room_listener_.load()) {
			listener->ReconnectedEvent(std::move(recovered_response));
		}
	} else if (!recovery_stop_) {
		LKC_TRACE_INSTANT(TraceCategory::Transport, "connection.recovery.failed");
		LKC_LOG_ERROR << "connection recovery exhausted after "
		              << std::chrono::duration_cast<std::chrono::milliseconds>(
		                     std::chrono::steady_clock::now() - recovery_started)
		                     .count()
		              << "ms";
		ResetTransport(false);
		recovery_allowed_ = false;
		if (auto* listener = room_listener_.load()) {
			listener->SignalDisconnectedEvent(recovery_failure_reason_.load());
			listener->RoomEosEvent();
		}
	}
	force_full_reconnect_ = false;
	recovering_connection_ = false;
	recovery_in_progress_ = false;
	close_recovery_trace();
}

void RtcEngine::SetRoomObserver(RtcEngineListener* listener) { room_listener_.store(listener); }

void RtcEngine::SetE2EEManager(E2EEManager* manager, std::string local_participant_identity) {
	std::lock_guard<std::mutex> guard(e2ee_mutex_);
	e2ee_manager_ = manager;
	e2ee_local_identity_ = std::move(local_participant_identity);
}

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

webrtc::scoped_refptr<AudioDevice> RtcEngine::GetAudioDevice() {
	std::lock_guard<std::mutex> guard(peer_factory_lock_);
	return peer_factory_ != nullptr ? peer_factory_->GetAudioDevice()
	                                : webrtc::scoped_refptr<AudioDevice>{};
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

bool RtcEngine::SendAdditionalCodecTrack(const livekit::AddTrackRequest& req) {
	if (req.cid().empty() || req.sid().empty()) {
		return false;
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	signal_client->SendAddTrack(req);
	return true;
}

webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
RtcEngine::CreateSender(LocalTrack* track, TrackPublishOptions options,
                        std::vector<webrtc::RtpEncodingParameters> send_encodings) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		auto transceiver = rtc_session_->CreateSender(track, options, send_encodings);
		if (track != nullptr && transceiver != nullptr) {
			track->SetStatsProvider(rtc_session_->CreatePublisherStatsProvider(transceiver));
		}
		return transceiver;
	}
	return nullptr;
}

webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
RtcEngine::CreateSender(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> media_track,
                        TrackPublishOptions options,
                        std::vector<webrtc::RtpEncodingParameters> send_encodings) {
	std::lock_guard<std::mutex> guard(session_lock_);
	return rtc_session_ != nullptr
	           ? rtc_session_->CreateSender(std::move(media_track), TrackKind::Video,
	                                        std::move(options), std::move(send_encodings))
	           : nullptr;
}

bool RtcEngine::SupportsVideoCodec(VideoCodec codec) const {
	std::lock_guard<std::mutex> guard(session_lock_);
	return rtc_session_ != nullptr && rtc_session_->SupportsVideoCodec(codec);
}

bool RtcEngine::RemoveSender(LocalTrack* track) {
	std::lock_guard<std::mutex> guard(session_lock_);
	const bool removed = rtc_session_ != nullptr && rtc_session_->RemoveSender(track);
	if (removed && track != nullptr) {
		track->SetStatsProvider({});
	}
	return removed;
}

bool RtcEngine::RemoveSender(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
	std::lock_guard<std::mutex> guard(session_lock_);
	return rtc_session_ != nullptr && rtc_session_->RemoveSender(std::move(transceiver));
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

	livekit::DataPacket outbound(packet);
	{
		std::lock_guard<std::mutex> guard(e2ee_mutex_);
		if (e2ee_manager_ != nullptr && e2ee_manager_->Enabled()) {
			livekit::EncryptedPacketPayload payload;
			if (MakeEncryptablePayload(packet, payload)) {
				std::string serialized_payload;
				if (!payload.SerializeToString(&serialized_payload)) {
					return false;
				}
				const std::vector<std::uint8_t> plaintext(serialized_payload.begin(),
				                                          serialized_payload.end());
				auto encrypted = E2EEManagerNativeAccess::EncryptData(
				    *e2ee_manager_, e2ee_local_identity_, plaintext);
				if (!encrypted) {
					return false;
				}
				auto* encrypted_packet = outbound.mutable_encrypted_packet();
				encrypted_packet->set_encryption_type(livekit::Encryption_Type_GCM);
				encrypted_packet->set_iv(encrypted->iv.data(), encrypted->iv.size());
				encrypted_packet->set_key_index(static_cast<std::uint32_t>(encrypted->key_index));
				encrypted_packet->set_encrypted_value(encrypted->payload.data(),
				                                      encrypted->payload.size());
			}
		}
	}

	std::string serialized;
	if (!outbound.SerializeToString(&serialized)) {
		return false;
	}
	std::lock_guard<std::mutex> send_guard(reliable ? reliable_data_channel_send_mutex_
	                                                : lossy_data_channel_send_mutex_);
	UpdateDataChannelBufferStatus(channel, reliable);
	if (!WaitForDataChannelBuffer(channel, reliable)) {
		return false;
	}
	const bool sent = channel->Send(
	    webrtc::DataBuffer(webrtc::CopyOnWriteBuffer(serialized.data(), serialized.size()), true));
	UpdateDataChannelBufferStatus(channel, reliable);
	return sent;
}

std::pair<std::optional<livekit::DataTrackInfo>, DataTrackError>
RtcEngine::PublishDataTrack(const DataTrackPublishOptions& options, bool encrypted) {
	if (options.name.empty() || options.name.size() > 256) {
		return {std::nullopt,
		        {DataTrackErrorCode::InvalidName, "data track name must contain 1 to 256 bytes"}};
	}
	livekit::PublishDataTrackRequest request;
	request.set_name(options.name);
	request.set_encryption(encrypted ? livekit::Encryption_Type_GCM
	                                 : livekit::Encryption_Type_NONE);
	if (options.frame_encoding &&
	    !detail::ToProto(*options.frame_encoding, *request.mutable_frame_encoding())) {
		return {std::nullopt,
		        {DataTrackErrorCode::InvalidSchema, "invalid data track frame encoding"}};
	}
	if (options.schema && !detail::ToProto(*options.schema, *request.mutable_schema())) {
		return {std::nullopt, {DataTrackErrorCode::InvalidSchema, "invalid data track schema"}};
	}

	auto pending = std::make_shared<PendingDataTrackRequest>();
	uint16_t handle = 0;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		for (std::size_t attempt = 0; attempt < std::numeric_limits<uint16_t>::max(); ++attempt) {
			const auto candidate = static_cast<uint16_t>(next_data_track_handle_++);
			if (next_data_track_handle_ > std::numeric_limits<uint16_t>::max()) {
				next_data_track_handle_ = 1;
			}
			if (candidate != 0 && !published_data_tracks_.contains(candidate) &&
			    !pending_data_track_publishes_.contains(candidate)) {
				handle = candidate;
				break;
			}
		}
		if (handle == 0) {
			return {std::nullopt,
			        {DataTrackErrorCode::HandleLimitReached,
			         "all data track publisher handles are in use"}};
		}
		pending_data_track_publishes_[handle] = pending;
	}
	request.set_pub_handle(handle);
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		pending_data_track_publishes_.erase(handle);
		return {std::nullopt, {DataTrackErrorCode::Disconnected, "room is disconnected"}};
	}
	signal_client->SendPublishDataTrack(request);

	std::unique_lock<std::mutex> lock(pending->mutex);
	if (!pending->cv.wait_for(lock, kDataTrackRequestTimeout,
	                          [&pending]() { return pending->completed; })) {
		lock.unlock();
		{
			std::lock_guard<std::mutex> guard(data_tracks_mutex_);
			pending_data_track_publishes_.erase(handle);
		}
		if (auto current_signal = SignalClientSnapshot()) {
			current_signal->SendUnpublishDataTrack(handle);
		}
		return {std::nullopt, {DataTrackErrorCode::Timeout, "data track publish timed out"}};
	}
	return {pending->info, pending->error};
}

DataTrackError RtcEngine::UnpublishDataTrack(uint16_t publisher_handle) {
	if (publisher_handle == 0) {
		return {DataTrackErrorCode::Unpublished, "data track is unpublished"};
	}
	auto pending = std::make_shared<PendingDataTrackRequest>();
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		if (!published_data_tracks_.contains(publisher_handle)) {
			return {DataTrackErrorCode::Unpublished, "data track is unpublished"};
		}
		pending_data_track_unpublishes_[publisher_handle] = pending;
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		pending_data_track_unpublishes_.erase(publisher_handle);
		return {DataTrackErrorCode::Disconnected, "room is disconnected"};
	}
	signal_client->SendUnpublishDataTrack(publisher_handle);
	std::unique_lock<std::mutex> lock(pending->mutex);
	if (!pending->cv.wait_for(lock, kDataTrackRequestTimeout,
	                          [&pending]() { return pending->completed; })) {
		lock.unlock();
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		pending_data_track_unpublishes_.erase(publisher_handle);
		return {DataTrackErrorCode::Timeout, "data track unpublish timed out"};
	}
	return pending->error;
}

DataTrackError RtcEngine::PushDataTrackFrame(uint16_t publisher_handle,
                                             const DataTrackFrame& frame) {
	webrtc::scoped_refptr<webrtc::DataChannelInterface> channel;
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		channel = dataTrackDC_;
	}
	if (!channel) {
		negotiate();
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		channel = dataTrackDC_;
	}
	if (!channel) {
		return {DataTrackErrorCode::Disconnected, "data track channel is unavailable"};
	}
	const auto deadline = std::chrono::steady_clock::now() + kDataChannelOpenTimeout;
	while (channel->state() == webrtc::DataChannelInterface::kConnecting &&
	       std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (channel->state() != webrtc::DataChannelInterface::kOpen) {
		return {DataTrackErrorCode::Disconnected, "data track channel is not open"};
	}

	DataTrackFrame outbound = frame;
	std::optional<detail::DataTrackPacketExtensions> extensions;
	std::unique_lock<std::mutex> tracks_lock(data_tracks_mutex_);
	auto info = published_data_tracks_.find(publisher_handle);
	auto packetizer = data_track_packetizers_.find(publisher_handle);
	if (info == published_data_tracks_.end() || packetizer == data_track_packetizers_.end()) {
		return {DataTrackErrorCode::Unpublished, "data track is unpublished"};
	}
	if (info->second.encryption() != livekit::Encryption_Type_NONE) {
		std::lock_guard<std::mutex> e2ee_guard(e2ee_mutex_);
		if (e2ee_manager_ == nullptr || !e2ee_manager_->Enabled()) {
			return {DataTrackErrorCode::SendFailed, "data track E2EE is unavailable"};
		}
		auto encrypted = E2EEManagerNativeAccess::EncryptData(*e2ee_manager_, e2ee_local_identity_,
		                                                      outbound.payload);
		if (!encrypted || encrypted->key_index > std::numeric_limits<uint8_t>::max() ||
		    encrypted->iv.size() != 12) {
			return {DataTrackErrorCode::SendFailed, "failed to encrypt data track frame"};
		}
		outbound.payload = std::move(encrypted->payload);
		extensions.emplace();
		extensions->key_index = static_cast<uint8_t>(encrypted->key_index);
		extensions->iv = std::move(encrypted->iv);
	}
	if (outbound.user_timestamp) {
		if (!extensions) {
			extensions.emplace();
		}
		extensions->user_timestamp = outbound.user_timestamp;
	}
	auto packets = packetizer->second->Packetize(outbound, extensions);
	tracks_lock.unlock();
	if (!packets) {
		return {DataTrackErrorCode::InvalidFrame, "data track frame exceeds protocol limits"};
	}
	std::size_t bytes = 0;
	for (const auto& packet : *packets) {
		bytes += packet.size();
	}
	std::lock_guard<std::mutex> send_guard(data_track_send_mutex_);
	if (channel->buffered_amount() + bytes > kDataTrackHighWaterMark) {
		return {DataTrackErrorCode::QueueFull, "data track channel queue is full"};
	}
	for (const auto& packet : *packets) {
		if (!channel->Send(webrtc::DataBuffer(
		        webrtc::CopyOnWriteBuffer(packet.data(), packet.size()), true))) {
			return {DataTrackErrorCode::SendFailed, "failed to send data track packet"};
		}
	}
	return {};
}

bool RtcEngine::UpdateDataTrackSubscription(const std::string& track_sid, bool subscribe,
                                            const DataTrackSubscriptionOptions& options) {
	if (track_sid.empty() || options.buffer_capacity == 0 || options.max_partial_frames == 0) {
		return false;
	}
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		if (subscribe) {
			data_track_partial_frame_limits_[track_sid] = options.max_partial_frames;
			auto found = data_track_depacketizers_.find(track_sid);
			if (found != data_track_depacketizers_.end()) {
				found->second.SetMaximumPartialFrames(options.max_partial_frames);
			}
		} else {
			data_track_partial_frame_limits_.erase(track_sid);
			data_track_depacketizers_.erase(track_sid);
		}
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	signal_client->SendUpdateDataSubscription(track_sid, subscribe, options.target_fps);
	return true;
}

bool RtcEngine::RepublishDataTracks() {
	std::vector<livekit::DataTrackInfo> tracks;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		for (const auto& [handle, info] : published_data_tracks_) {
			tracks.push_back(info);
		}
	}
	bool success = true;
	for (const auto& info : tracks) {
		livekit::PublishDataTrackRequest request;
		request.set_pub_handle(info.pub_handle());
		request.set_name(info.name());
		request.set_encryption(info.encryption());
		if (info.has_frame_encoding()) {
			request.mutable_frame_encoding()->CopyFrom(info.frame_encoding());
		}
		if (info.has_schema()) {
			request.mutable_schema()->CopyFrom(info.schema());
		}
		auto pending = std::make_shared<PendingDataTrackRequest>();
		{
			std::lock_guard<std::mutex> guard(data_tracks_mutex_);
			pending_data_track_publishes_[static_cast<uint16_t>(info.pub_handle())] = pending;
		}
		auto signal_client = SignalClientSnapshot();
		if (!signal_client) {
			success = false;
			break;
		}
		signal_client->SendPublishDataTrack(request);
		std::unique_lock<std::mutex> lock(pending->mutex);
		if (!pending->cv.wait_for(lock, kDataTrackRequestTimeout,
		                          [&pending]() { return pending->completed; }) ||
		    pending->error) {
			success = false;
		}
		{
			std::lock_guard<std::mutex> guard(data_tracks_mutex_);
			pending_data_track_publishes_.erase(static_cast<uint16_t>(info.pub_handle()));
		}
	}
	return success;
}

std::optional<livekit::DataTrackInfo>
RtcEngine::PublishedDataTrackInfo(uint16_t publisher_handle) const {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	auto found = published_data_tracks_.find(publisher_handle);
	return found == published_data_tracks_.end()
	           ? std::optional<livekit::DataTrackInfo>{}
	           : std::optional<livekit::DataTrackInfo>{found->second};
}

DataTrackError RtcEngine::StoreDataTrackSchema(const DataTrackSchema& schema) {
	if (schema.definition.size() > kMaximumDataTrackSchemaDefinitionSize) {
		return {DataTrackErrorCode::InvalidSchema,
		        "data track schema definition exceeds the 50 KB protocol limit"};
	}
	livekit::StoreDataBlobRequest request;
	if (!detail::ToProto(schema.id, *request.mutable_blob()->mutable_key()->mutable_schema_id())) {
		return {DataTrackErrorCode::InvalidSchema, "invalid data track schema identifier"};
	}
	if (schema.definition.empty()) {
		request.mutable_blob()->clear_contents();
	} else {
		request.mutable_blob()->set_contents(
		    reinterpret_cast<const char*>(schema.definition.data()), schema.definition.size());
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return {DataTrackErrorCode::Disconnected, "room is disconnected"};
	}
	auto pending = std::make_shared<PendingDataBlobRequest>();
	uint32_t request_id = 0;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		if (recovering_connection_ || recovery_stop_) {
			return {DataTrackErrorCode::Disconnected,
			        "connection is recovering; retry the schema request"};
		}
		for (std::size_t attempt = 0; attempt <= pending_data_blob_requests_.size(); ++attempt) {
			const auto candidate = signal_client->NextRequestId();
			if (candidate != 0 && !pending_data_blob_requests_.contains(candidate)) {
				request_id = candidate;
				break;
			}
		}
		if (request_id == 0) {
			return {DataTrackErrorCode::QueueFull, "data blob request identifiers are exhausted"};
		}
		pending_data_blob_requests_[request_id] = pending;
	}
	request.set_request_id(request_id);
	signal_client->SendStoreDataBlob(request);
	std::unique_lock<std::mutex> lock(pending->mutex);
	if (!pending->cv.wait_for(lock, kDataTrackRequestTimeout,
	                          [&pending]() { return pending->completed; })) {
		lock.unlock();
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		pending_data_blob_requests_.erase(request_id);
		return {DataTrackErrorCode::Timeout, "data track schema storage timed out"};
	}
	if (pending->error) {
		return pending->error;
	}
	if (!pending->key || !pending->key->has_schema_id() ||
	    detail::FromProto(pending->key->schema_id()) != schema.id) {
		return {DataTrackErrorCode::ProtocolError,
		        "server returned a mismatched data track schema identifier"};
	}
	return {};
}

DataTrackSchemaResult RtcEngine::GetDataTrackSchema(const std::string& participant_identity,
                                                    const DataTrackSchemaId& schema_id) {
	if (participant_identity.empty()) {
		return {{}, {DataTrackErrorCode::InvalidSchema, "participant identity is required"}};
	}
	livekit::GetDataBlobRequest request;
	request.set_participant_identity(participant_identity);
	if (!detail::ToProto(schema_id, *request.mutable_key()->mutable_schema_id())) {
		return {{}, {DataTrackErrorCode::InvalidSchema, "invalid data track schema identifier"}};
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return {{}, {DataTrackErrorCode::Disconnected, "room is disconnected"}};
	}
	auto pending = std::make_shared<PendingDataBlobRequest>();
	uint32_t request_id = 0;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		if (recovering_connection_ || recovery_stop_) {
			return {{},
			        {DataTrackErrorCode::Disconnected,
			         "connection is recovering; retry the schema request"}};
		}
		for (std::size_t attempt = 0; attempt <= pending_data_blob_requests_.size(); ++attempt) {
			const auto candidate = signal_client->NextRequestId();
			if (candidate != 0 && !pending_data_blob_requests_.contains(candidate)) {
				request_id = candidate;
				break;
			}
		}
		if (request_id == 0) {
			return {{},
			        {DataTrackErrorCode::QueueFull, "data blob request identifiers are exhausted"}};
		}
		pending_data_blob_requests_[request_id] = pending;
	}
	request.set_request_id(request_id);
	signal_client->SendGetDataBlob(request);
	std::unique_lock<std::mutex> lock(pending->mutex);
	if (!pending->cv.wait_for(lock, kDataTrackRequestTimeout,
	                          [&pending]() { return pending->completed; })) {
		lock.unlock();
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		pending_data_blob_requests_.erase(request_id);
		return {{}, {DataTrackErrorCode::Timeout, "data track schema lookup timed out"}};
	}
	if (pending->error) {
		return {{}, pending->error};
	}
	if (!pending->blob || !pending->blob->has_key() || !pending->blob->key().has_schema_id()) {
		return {{}, {DataTrackErrorCode::NotFound, "data track schema was not found"}};
	}
	const auto returned_id = detail::FromProto(pending->blob->key().schema_id());
	if (returned_id != schema_id ||
	    pending->blob->contents().size() > kMaximumDataTrackSchemaDefinitionSize) {
		return {{},
		        {DataTrackErrorCode::ProtocolError,
		         "server returned an invalid data track schema definition"}};
	}
	DataTrackSchema schema;
	schema.id = returned_id;
	schema.definition.assign(pending->blob->contents().begin(), pending->blob->contents().end());
	return {std::move(schema), {}};
}

bool RtcEngine::WaitForDataChannelBuffer(
    const webrtc::scoped_refptr<webrtc::DataChannelInterface>& channel, bool reliable) {
	return channel &&
	       data_channel_backpressure_.WaitUntilWritable(
	           reliable, [channel] { return channel->buffered_amount(); },
	           [channel] { return channel->state() == webrtc::DataChannelInterface::kOpen; },
	           std::chrono::duration_cast<std::chrono::milliseconds>(kDataChannelDrainTimeout));
}

void RtcEngine::UpdateDataChannelBufferStatus(
    const webrtc::scoped_refptr<webrtc::DataChannelInterface>& channel, bool reliable) {
	if (!channel) {
		return;
	}
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		const auto& current = reliable ? reliableDC_ : lossyDC_;
		if (!current || current.get() != channel.get()) {
			return;
		}
	}
	const auto amount = channel->buffered_amount();
	const auto transition = data_channel_backpressure_.Update(reliable, amount);
	if (!transition.changed) {
		return;
	}
	QueueDataChannelBufferStatusEvent({reliable, amount, data_channel_backpressure_.HighWaterMark(),
	                                   data_channel_backpressure_.LowWaterMark(),
	                                   transition.backpressured});
}

void RtcEngine::QueueDataChannelBufferStatusEvent(DataChannelBufferStatus status) {
	{
		std::lock_guard<std::mutex> guard(rpc_tasks_mutex_);
		if (rpc_workers_stopping_) {
			return;
		}
		rpc_tasks_.emplace_back([this, status] {
			if (auto* listener = room_listener_.load()) {
				listener->DataChannelBufferStatusEvent(status);
			}
		});
	}
	rpc_tasks_cv_.notify_one();
}

bool RtcEngine::RegisterRpcMethod(std::string method, RpcHandler handler) {
	if (method.empty() || !handler) {
		return false;
	}
	std::lock_guard<std::mutex> guard(rpc_handlers_mutex_);
	return rpc_handlers_.emplace(std::move(method), std::move(handler)).second;
}

bool RtcEngine::UnregisterRpcMethod(const std::string& method) {
	std::lock_guard<std::mutex> guard(rpc_handlers_mutex_);
	return rpc_handlers_.erase(method) != 0;
}

RpcResult RtcEngine::PerformRpc(const PerformRpcParams& params) {
	LKC_TRACE_SPAN(TraceCategory::Rpc, "rpc.perform");
	if (params.destination_identity.empty()) {
		return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::RecipientNotFound));
	}
	if (params.method.empty()) {
		return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::UnsupportedMethod));
	}
	if (params.payload.size() > kMaximumRpcPayloadBytes) {
		return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::RequestPayloadTooLarge));
	}
	{
		std::lock_guard<std::mutex> guard(rpc_participants_mutex_);
		if (!rpc_participant_identities_.contains(params.destination_identity)) {
			return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::RecipientNotFound));
		}
	}

	const auto timeout = (std::max)(params.response_timeout, kMinimumRpcTimeout);
	const auto timeout_ms =
	    (std::min)(timeout, std::chrono::milliseconds((std::numeric_limits<uint32_t>::max)()));
	const auto request_id = webrtc::CreateRandomUuid();
	auto pending = std::make_shared<PendingRpcCall>();
	pending->destination_identity = params.destination_identity;
	{
		std::lock_guard<std::mutex> guard(pending_rpc_mutex_);
		pending_rpc_calls_.emplace(request_id, pending);
	}

	livekit::DataPacket packet;
	packet.set_kind(livekit::DataPacket_Kind_RELIABLE);
	packet.add_destination_identities(params.destination_identity);
	auto* request = packet.mutable_rpc_request();
	request->set_id(request_id);
	request->set_method(params.method);
	request->set_payload(params.payload);
	request->set_response_timeout_ms(static_cast<uint32_t>(timeout_ms.count()));
	request->set_version(kRpcVersion);

	if (!SendDataPacket(packet, true)) {
		std::lock_guard<std::mutex> guard(pending_rpc_mutex_);
		pending_rpc_calls_.erase(request_id);
		return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::SendFailed));
	}

	const auto started_at = std::chrono::steady_clock::now();
	const auto response_deadline = started_at + timeout_ms;
	const auto ack_deadline = (std::min)(started_at + kRpcAckTimeout, response_deadline);
	std::unique_lock<std::mutex> pending_guard(pending->mutex);
	if (!pending->cv.wait_until(pending_guard, ack_deadline, [&pending]() {
		    return pending->acknowledged || pending->completed;
	    })) {
		pending->completed = true;
		pending->result = RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::ConnectionTimeout));
	}
	if (!pending->completed &&
	    !pending->cv.wait_until(pending_guard, response_deadline,
	                            [&pending]() { return pending->completed; })) {
		pending->completed = true;
		pending->result = RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::ResponseTimeout));
	}
	auto result = pending->result;
	pending_guard.unlock();
	{
		std::lock_guard<std::mutex> guard(pending_rpc_mutex_);
		auto found = pending_rpc_calls_.find(request_id);
		if (found != pending_rpc_calls_.end() && found->second == pending) {
			pending_rpc_calls_.erase(found);
		}
	}
	return result;
}

bool RtcEngine::UpdateSubscriptionPermissions(
    bool all_participants_allowed,
    const std::vector<ParticipantTrackPermission>& participant_permissions) {
	std::vector<livekit::TrackPermission> permissions;
	permissions.reserve(participant_permissions.size());
	for (const auto& permission : participant_permissions) {
		if (permission.participant_sid.empty() && permission.participant_identity.empty()) {
			return false;
		}
		livekit::TrackPermission converted;
		converted.set_participant_sid(permission.participant_sid);
		converted.set_participant_identity(permission.participant_identity);
		converted.set_all_tracks(permission.allow_all);
		for (const auto& track_sid : permission.allowed_track_sids) {
			if (track_sid.empty()) {
				return false;
			}
			converted.add_track_sids(track_sid);
		}
		permissions.push_back(std::move(converted));
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	signal_client->SendUpdateSubscriptionPermissions(all_participants_allowed, permissions);
	return true;
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

bool RtcEngine::UpdateTrackSettings(const std::string& track_sid,
                                    const RemoteTrackSettings& settings) {
	if (track_sid.empty()) {
		return false;
	}
	auto signal_client = SignalClientSnapshot();
	if (!signal_client) {
		return false;
	}
	livekit::UpdateTrackSettings update;
	update.add_track_sids(track_sid);
	update.set_disabled(!settings.enabled);
	if (settings.video_quality.has_value()) {
		update.set_quality(
		    static_cast<livekit::VideoQuality>(static_cast<int>(*settings.video_quality)));
	}
	if (settings.video_dimensions.has_value()) {
		update.set_width(settings.video_dimensions->width);
		update.set_height(settings.video_dimensions->height);
	}
	update.set_fps(settings.video_fps);
	update.set_priority(settings.priority);
	signal_client->SendUpdateTrackSettings(update);
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
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		for (const auto& [handle, info] : published_data_tracks_) {
			(void)handle;
			sync.add_publish_data_tracks()->mutable_info()->CopyFrom(info);
		}
	}
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		for (const auto& channel : {lossyDC_, reliableDC_, dataTrackDC_}) {
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

bool RtcEngine::SimulateMediaFailureForTesting() {
	if (!recovery_allowed_ || recovery_stop_) {
		return false;
	}
	OnStateChange(RtcSession::State::kFailed,
	              webrtc::PeerConnectionInterface::PeerConnectionState::kFailed,
	              webrtc::PeerConnectionInterface::PeerConnectionState::kFailed);
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
	LKC_LOG_DEBUG << "received track publication: cid=" << response.cid()
	              << ", sid=" << response.track().sid();

	auto& cid = response.cid();
	{
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		auto it = pending_track_resolvers_.find(cid);
		if (it == pending_track_resolvers_.end()) {
			LKC_LOG_WARNING << "missing track resolver for cid=" << cid;
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
	if (update.track_sid().empty()) {
		return;
	}
	if (auto* listener = room_listener_.load()) {
		listener->SubscribedQualityUpdateEvent(update);
	}
}
void RtcEngine::OnTokenRefresh(const std::string& token) {
	if (token.empty()) {
		return;
	}
	{
		std::lock_guard<std::mutex> guard(access_token_mutex_);
		access_token_ = token;
	}
	if (auto* listener = room_listener_.load()) {
		listener->TokenRefreshedEvent();
	}
}

void RtcEngine::OnRoomMoved(const livekit::RoomMovedResponse& response) {
	if (response.has_room() && !response.room().name().empty()) {
		std::lock_guard<std::mutex> guard(connection_params_mutex_);
		connection_options_.token_source_options.room_name = response.room().name();
	}
	{
		std::lock_guard<std::mutex> guard(session_lock_);
		if (response.has_room()) {
			*join_resp_.mutable_room() = response.room();
		}
		if (response.has_participant()) {
			*join_resp_.mutable_participant() = response.participant();
		}
		join_resp_.clear_other_participants();
		for (const auto& participant : response.other_participants()) {
			*join_resp_.add_other_participants() = participant;
		}
	}
	{
		std::lock_guard<std::mutex> guard(rpc_participants_mutex_);
		rpc_participant_identities_.clear();
		if (response.has_participant() && !response.participant().identity().empty()) {
			rpc_participant_identities_.insert(response.participant().identity());
		}
		for (const auto& participant : response.other_participants()) {
			if (!participant.identity().empty() &&
			    participant.state() != livekit::ParticipantInfo_State_DISCONNECTED) {
				rpc_participant_identities_.insert(participant.identity());
			}
		}
	}
	if (auto* listener = room_listener_.load()) {
		listener->RoomMovedEvent(response);
	}
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
			listener->RoomEosEvent();
		}
	}
}
void RtcEngine::OnParticipantUpdate(const std::vector<livekit::ParticipantInfo>& updates) {
	for (const auto& participant : updates) {
		if (participant.identity().empty()) {
			continue;
		}
		const bool disconnected =
		    participant.state() == livekit::ParticipantInfo_State_DISCONNECTED;
		{
			std::lock_guard<std::mutex> guard(rpc_participants_mutex_);
			if (disconnected) {
				rpc_participant_identities_.erase(participant.identity());
			} else {
				rpc_participant_identities_.insert(participant.identity());
			}
		}
		if (disconnected) {
			CancelPendingRpc(participant.identity(), RpcErrorCode::RecipientDisconnected);
		}
	}
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
void RtcEngine::OnStreamStateUpdate(const std::vector<livekit::StreamStateInfo>& update) {
	if (auto* listener = room_listener_.load()) {
		listener->StreamStateUpdateEvent(update);
	}
}
void RtcEngine::OnSubscriptionPermissionUpdate(
    const livekit::SubscriptionPermissionUpdate& update) {
	if (auto* listener = room_listener_.load()) {
		listener->SubscriptionPermissionUpdateEvent(update);
	}
}
void RtcEngine::OnSubscriptionError(const livekit::SubscriptionResponse& response) {
	if (auto* listener = room_listener_.load()) {
		listener->SubscriptionErrorEvent(response);
	}
}
void RtcEngine::OnRequestResponse(const livekit::RequestResponse& response) {
	if (response.reason() == livekit::RequestResponse::OK) {
		return;
	}
	uint16_t handle = 0;
	std::shared_ptr<PendingDataTrackRequest> pending;
	std::shared_ptr<PendingDataBlobRequest> pending_blob;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		if (response.has_publish_data_track()) {
			handle = static_cast<uint16_t>(response.publish_data_track().pub_handle());
			auto found = pending_data_track_publishes_.find(handle);
			if (found != pending_data_track_publishes_.end()) {
				pending = found->second;
				pending_data_track_publishes_.erase(found);
			}
		} else if (response.has_unpublish_data_track()) {
			handle = static_cast<uint16_t>(response.unpublish_data_track().pub_handle());
			auto found = pending_data_track_unpublishes_.find(handle);
			if (found != pending_data_track_unpublishes_.end()) {
				pending = found->second;
				pending_data_track_unpublishes_.erase(found);
			}
		} else if (response.request_id() != 0) {
			auto found = pending_data_blob_requests_.find(response.request_id());
			if (found != pending_data_blob_requests_.end()) {
				pending_blob = found->second;
				pending_data_blob_requests_.erase(found);
			}
		}
	}
	if (pending) {
		{
			std::lock_guard<std::mutex> guard(pending->mutex);
			pending->completed = true;
			pending->error = RequestResponseError(response);
		}
		pending->cv.notify_all();
	}
	if (pending_blob) {
		{
			std::lock_guard<std::mutex> guard(pending_blob->mutex);
			pending_blob->completed = true;
			pending_blob->error = RequestResponseError(response);
		}
		pending_blob->cv.notify_all();
	}
}

void RtcEngine::OnDataTrackPublished(const livekit::PublishDataTrackResponse& response) {
	if (!response.has_info() || response.info().pub_handle() == 0 ||
	    response.info().pub_handle() > std::numeric_limits<uint16_t>::max()) {
		return;
	}
	const auto handle = static_cast<uint16_t>(response.info().pub_handle());
	std::shared_ptr<PendingDataTrackRequest> pending;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		auto found = pending_data_track_publishes_.find(handle);
		if (found == pending_data_track_publishes_.end()) {
			return;
		}
		pending = found->second;
		pending_data_track_publishes_.erase(found);
		published_data_tracks_[handle] = response.info();
		if (!data_track_packetizers_.contains(handle)) {
			data_track_packetizers_[handle] = std::make_unique<detail::DataTrackPacketizer>(handle);
		}
	}
	{
		std::lock_guard<std::mutex> guard(pending->mutex);
		pending->completed = true;
		pending->info = response.info();
	}
	pending->cv.notify_all();
}

void RtcEngine::OnDataTrackUnpublished(const livekit::UnpublishDataTrackResponse& response) {
	if (!response.has_info() || response.info().pub_handle() == 0 ||
	    response.info().pub_handle() > std::numeric_limits<uint16_t>::max()) {
		return;
	}
	const auto handle = static_cast<uint16_t>(response.info().pub_handle());
	std::shared_ptr<PendingDataTrackRequest> pending;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		auto found = pending_data_track_unpublishes_.find(handle);
		if (found != pending_data_track_unpublishes_.end()) {
			pending = found->second;
			pending_data_track_unpublishes_.erase(found);
		}
		published_data_tracks_.erase(handle);
		data_track_packetizers_.erase(handle);
	}
	if (pending) {
		{
			std::lock_guard<std::mutex> guard(pending->mutex);
			pending->completed = true;
			pending->info = response.info();
		}
		pending->cv.notify_all();
	}
	if (auto* listener = room_listener_.load()) {
		listener->LocalDataTrackUnpublishedEvent(handle);
	}
}

void RtcEngine::OnDataTrackSubscriberHandles(const livekit::DataTrackSubscriberHandles& handles) {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	for (const auto& [wire_handle, track] : handles.sub_handles()) {
		if (wire_handle == 0 || wire_handle > std::numeric_limits<uint16_t>::max() ||
		    track.track_sid().empty()) {
			continue;
		}
		const auto handle = static_cast<uint16_t>(wire_handle);
		for (auto route = incoming_data_track_routes_.begin();
		     route != incoming_data_track_routes_.end();) {
			if (route->first != handle && route->second.track_sid == track.track_sid()) {
				route = incoming_data_track_routes_.erase(route);
			} else {
				++route;
			}
		}
		incoming_data_track_routes_[handle] = {track.track_sid(), track.publisher_identity()};
		const auto limit = data_track_partial_frame_limits_.contains(track.track_sid())
		                       ? data_track_partial_frame_limits_[track.track_sid()]
		                       : 1;
		auto [found, inserted] = data_track_depacketizers_.try_emplace(track.track_sid(), limit);
		found->second.SetMaximumPartialFrames(limit);
	}
}

void RtcEngine::OnDataBlobStored(const livekit::StoreDataBlobResponse& response) {
	std::shared_ptr<PendingDataBlobRequest> pending;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		auto found = pending_data_blob_requests_.find(response.request_id());
		if (found == pending_data_blob_requests_.end()) {
			return;
		}
		pending = found->second;
		pending_data_blob_requests_.erase(found);
	}
	{
		std::lock_guard<std::mutex> guard(pending->mutex);
		pending->completed = true;
		if (response.has_key()) {
			pending->key = response.key();
		} else {
			pending->error = {DataTrackErrorCode::ProtocolError,
			                  "server returned an empty data blob storage response"};
		}
	}
	pending->cv.notify_all();
}

void RtcEngine::OnDataBlobReceived(const livekit::GetDataBlobResponse& response) {
	std::shared_ptr<PendingDataBlobRequest> pending;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		auto found = pending_data_blob_requests_.find(response.request_id());
		if (found == pending_data_blob_requests_.end()) {
			return;
		}
		pending = found->second;
		pending_data_blob_requests_.erase(found);
	}
	{
		std::lock_guard<std::mutex> guard(pending->mutex);
		pending->completed = true;
		if (response.has_blob()) {
			pending->blob = response.blob();
		} else {
			pending->error = {DataTrackErrorCode::NotFound, "data blob was not found"};
		}
	}
	pending->cv.notify_all();
}

void RtcEngine::OnLocalTrackSubscribed(const std::string& track_sid) {
	if (track_sid.empty()) {
		return;
	}
	if (auto* listener = room_listener_.load()) {
		listener->LocalTrackSubscribedEvent(track_sid);
	}
}

void RtcEngine::OnLocalOffer(PeerTransport::Target target,
                             std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	if (auto signal_client = SignalClientSnapshot()) {
		signal_client->SendOffer(std::move(offer));
	}
}

void RtcEngine::OnStateChange(RtcSession::State connection_state,
                              webrtc::PeerConnectionInterface::PeerConnectionState pub_state,
                              webrtc::PeerConnectionInterface::PeerConnectionState sub_state) {
	LKC_LOG_DEBUG << "RTC session state changed: state=" << int(connection_state)
	              << ", publisher=" << int(pub_state) << ", subscriber=" << int(sub_state);
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
                        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver,
                        std::function<std::string()> stats_provider) {
	if (target != PeerTransport::Target::SUBSCRIBER || !transceiver || !transceiver->receiver()) {
		return;
	}
	auto track = transceiver->receiver()->track();
	if (track) {
		if (auto* listener = room_listener_.load()) {
			listener->MediaTrackEvent(std::move(track), transceiver->receiver(),
			                          std::move(stats_provider));
		}
	}
}

void RtcEngine::OnRemoveTrack(PeerTransport::Target target,
                              webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
	if (target != PeerTransport::Target::SUBSCRIBER || !receiver || !receiver->track()) {
		return;
	}
	if (auto* listener = room_listener_.load()) {
		listener->MediaTrackRemovedEvent(receiver->track()->id());
	}
}

void RtcEngine::OnInterestingUsage(PeerTransport::Target target, int usagePattern) {}

void RtcEngine::negotiate() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (!rtc_session_) {
		return;
	}
	bool has_publisher_data_channels = false;
	{
		std::lock_guard<std::mutex> data_guard(data_channels_lock_);
		has_publisher_data_channels = lossyDC_ && reliableDC_ && dataTrackDC_;
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

	struct webrtc::DataChannelInit data_track_init;
	data_track_init.ordered = false;
	data_track_init.reliable = false;
	data_track_init.maxRetransmits = 0;
	auto data_track_channel =
	    this->rtc_session_->CreateDataChannel("_data_track", &data_track_init);
	registerDataChannel(std::move(data_track_channel), true);
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
	const bool reliable = channel->label() == "_reliable";
	auto observer =
	    std::make_unique<DataChannelObserverProxy>(this, channel, reliable, publisher_channel);
	channel->RegisterObserver(observer.get());
	data_channels_.push_back(channel);
	data_channel_observers_.push_back(std::move(observer));
	if (publisher_channel && channel->label() == "_reliable") {
		reliableDC_ = channel;
	} else if (publisher_channel && channel->label() == "_lossy") {
		lossyDC_ = channel;
	} else if (publisher_channel && channel->label() == "_data_track") {
		dataTrackDC_ = channel;
	}
}

void RtcEngine::unregisterDataChannels() {
	{
		std::lock_guard<std::mutex> guard(data_channels_lock_);
		for (const auto& channel : data_channels_) {
			channel->UnregisterObserver();
		}
		data_channels_.clear();
		data_channel_observers_.clear();
		reliableDC_ = nullptr;
		lossyDC_ = nullptr;
		dataTrackDC_ = nullptr;
	}
	data_channel_backpressure_.Reset();
}

void RtcEngine::StartRpcWorkers() {
	std::lock_guard<std::mutex> guard(rpc_tasks_mutex_);
	rpc_workers_stopping_ = false;
	for (std::size_t index = 0; index < 2; ++index) {
		rpc_workers_.emplace_back([this]() { RunRpcWorker(); });
	}
}

void RtcEngine::StopRpcWorkers() {
	{
		std::lock_guard<std::mutex> guard(rpc_tasks_mutex_);
		rpc_workers_stopping_ = true;
		rpc_tasks_.clear();
	}
	rpc_tasks_cv_.notify_all();
	for (auto& worker : rpc_workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}
	rpc_workers_.clear();
}

void RtcEngine::RunRpcWorker() {
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> guard(rpc_tasks_mutex_);
			rpc_tasks_cv_.wait(guard,
			                   [this]() { return rpc_workers_stopping_ || !rpc_tasks_.empty(); });
			if (rpc_workers_stopping_ && rpc_tasks_.empty()) {
				return;
			}
			task = std::move(rpc_tasks_.front());
			rpc_tasks_.pop_front();
		}
		task();
	}
}

void RtcEngine::HandleRpcPacket(const livekit::DataPacket& packet) {
	if (packet.has_rpc_request()) {
		HandleRpcRequest(packet);
	} else if (packet.has_rpc_ack()) {
		HandleRpcAck(packet);
	} else if (packet.has_rpc_response()) {
		HandleRpcResponse(packet);
	}
}

void RtcEngine::HandleRpcRequest(const livekit::DataPacket& packet) {
	const auto caller_identity = packet.participant_identity();
	const auto request = packet.rpc_request();
	if (caller_identity.empty() || request.id().empty()) {
		return;
	}

	livekit::DataPacket ack_packet;
	ack_packet.set_kind(livekit::DataPacket_Kind_RELIABLE);
	ack_packet.add_destination_identities(caller_identity);
	ack_packet.mutable_rpc_ack()->set_request_id(request.id());
	if (!SendDataPacket(ack_packet, true)) {
		return;
	}

	{
		std::lock_guard<std::mutex> guard(rpc_tasks_mutex_);
		if (rpc_workers_stopping_) {
			return;
		}
		rpc_tasks_.emplace_back([this, caller_identity, request]() {
			if (request.version() != kRpcVersion) {
				SendRpcResponse(
				    caller_identity, request.id(),
				    RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::UnsupportedVersion)));
				return;
			}

			RpcHandler handler;
			{
				std::lock_guard<std::mutex> guard(rpc_handlers_mutex_);
				auto found = rpc_handlers_.find(request.method());
				if (found != rpc_handlers_.end()) {
					handler = found->second;
				}
			}
			if (!handler) {
				SendRpcResponse(
				    caller_identity, request.id(),
				    RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::UnsupportedMethod)));
				return;
			}

			RpcResult result;
			try {
				result = handler({request.id(), caller_identity, request.payload(),
				                  std::chrono::milliseconds(request.response_timeout_ms())});
			} catch (...) {
				result = RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::ApplicationError));
			}
			SendRpcResponse(caller_identity, request.id(), result);
		});
	}
	rpc_tasks_cv_.notify_one();
}

void RtcEngine::HandleRpcAck(const livekit::DataPacket& packet) {
	std::shared_ptr<PendingRpcCall> pending;
	{
		std::lock_guard<std::mutex> guard(pending_rpc_mutex_);
		auto found = pending_rpc_calls_.find(packet.rpc_ack().request_id());
		if (found == pending_rpc_calls_.end()) {
			return;
		}
		pending = found->second;
	}
	if (!packet.participant_identity().empty() &&
	    packet.participant_identity() != pending->destination_identity) {
		return;
	}
	{
		std::lock_guard<std::mutex> guard(pending->mutex);
		pending->acknowledged = true;
	}
	pending->cv.notify_all();
}

void RtcEngine::HandleRpcResponse(const livekit::DataPacket& packet) {
	const auto& response = packet.rpc_response();
	std::shared_ptr<PendingRpcCall> pending;
	{
		std::lock_guard<std::mutex> guard(pending_rpc_mutex_);
		auto found = pending_rpc_calls_.find(response.request_id());
		if (found == pending_rpc_calls_.end()) {
			return;
		}
		pending = found->second;
	}
	if (!packet.participant_identity().empty() &&
	    packet.participant_identity() != pending->destination_identity) {
		return;
	}
	{
		std::lock_guard<std::mutex> guard(pending->mutex);
		if (pending->completed) {
			return;
		}
		pending->acknowledged = true;
		pending->completed = true;
		if (response.has_error()) {
			pending->result =
			    RpcResult::Failure({static_cast<RpcErrorCode>(response.error().code()),
			                        response.error().message(), response.error().data()});
		} else {
			pending->result = RpcResult::Success(response.payload());
		}
	}
	pending->cv.notify_all();
}

void RtcEngine::SendRpcResponse(const std::string& destination_identity,
                                const std::string& request_id, const RpcResult& result) {
	livekit::DataPacket packet;
	packet.set_kind(livekit::DataPacket_Kind_RELIABLE);
	packet.add_destination_identities(destination_identity);
	auto* response = packet.mutable_rpc_response();
	response->set_request_id(request_id);
	if (result.error) {
		auto* error = response->mutable_error();
		error->set_code(static_cast<uint32_t>(result.error->code));
		error->set_message(TruncateUtf8(result.error->message, kMaximumRpcErrorMessageBytes));
		error->set_data(TruncateUtf8(result.error->data, kMaximumRpcPayloadBytes));
	} else if (result.payload.size() > kMaximumRpcPayloadBytes) {
		auto* error = response->mutable_error();
		const auto rpc_error = RpcError::BuiltIn(RpcErrorCode::ResponsePayloadTooLarge);
		error->set_code(static_cast<uint32_t>(rpc_error.code));
		error->set_message(rpc_error.message);
	} else {
		response->set_payload(result.payload);
	}
	SendDataPacket(packet, true);
}

void RtcEngine::CancelPendingRpc(const std::optional<std::string>& participant_identity,
                                 RpcErrorCode code) {
	std::vector<std::shared_ptr<PendingRpcCall>> cancelled;
	{
		std::lock_guard<std::mutex> guard(pending_rpc_mutex_);
		for (auto current = pending_rpc_calls_.begin(); current != pending_rpc_calls_.end();) {
			if (!participant_identity ||
			    current->second->destination_identity == *participant_identity) {
				cancelled.push_back(current->second);
				current = pending_rpc_calls_.erase(current);
			} else {
				++current;
			}
		}
	}
	for (const auto& pending : cancelled) {
		{
			std::lock_guard<std::mutex> guard(pending->mutex);
			if (pending->completed) {
				continue;
			}
			pending->completed = true;
			pending->result = RpcResult::Failure(RpcError::BuiltIn(code));
		}
		pending->cv.notify_all();
	}
}

void RtcEngine::OnDataChannelStateChange(
    const webrtc::scoped_refptr<webrtc::DataChannelInterface>& channel, bool reliable,
    bool monitor_buffer) {
	data_channel_backpressure_.Notify();
	if (monitor_buffer) {
		QueueDataChannelBufferStatusUpdate(channel, reliable);
	}
}

void RtcEngine::OnDataChannelMessage(
    const webrtc::scoped_refptr<webrtc::DataChannelInterface>& channel,
    const webrtc::DataBuffer& buffer) {
	if (channel && channel->label() == "_data_track") {
		OnDataTrackMessage(buffer);
		return;
	}
	if (buffer.data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		return;
	}
	livekit::DataPacket packet;
	if (!packet.ParseFromArray(buffer.data.data(), static_cast<int>(buffer.data.size()))) {
		return;
	}
	if (packet.has_encrypted_packet()) {
		std::lock_guard<std::mutex> guard(e2ee_mutex_);
		const auto encryption_type = packet.encrypted_packet().encryption_type();
		// Official JS clients omit this proto3 field and rely on EncryptedPacket itself to
		// imply AES-GCM. Accept the default NONE value for compatibility, while still
		// rejecting explicitly unsupported algorithms.
		if (e2ee_manager_ == nullptr || !e2ee_manager_->Enabled() ||
		    (encryption_type != livekit::Encryption_Type_NONE &&
		     encryption_type != livekit::Encryption_Type_GCM)) {
			return;
		}
		const auto& encrypted_packet = packet.encrypted_packet();
		E2EEManagerNativeAccess::EncryptedData encrypted{
		    {encrypted_packet.encrypted_value().begin(), encrypted_packet.encrypted_value().end()},
		    {encrypted_packet.iv().begin(), encrypted_packet.iv().end()},
		    encrypted_packet.key_index()};
		auto decrypted = E2EEManagerNativeAccess::DecryptData(
		    *e2ee_manager_, packet.participant_identity(), encrypted);
		if (!decrypted ||
		    decrypted->size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
			return;
		}
		livekit::EncryptedPacketPayload payload;
		if (!payload.ParseFromArray(decrypted->data(), static_cast<int>(decrypted->size())) ||
		    !RestoreEncryptedPayload(payload, packet)) {
			return;
		}
	}
	if (packet.has_rpc_request() || packet.has_rpc_ack() || packet.has_rpc_response()) {
		HandleRpcPacket(packet);
		return;
	}
	if (auto* listener = room_listener_.load()) {
		listener->DataPacketEvent(packet);
	}
}

void RtcEngine::OnDataTrackMessage(const webrtc::DataBuffer& buffer) {
	auto packet = detail::DeserializeDataTrackPacket(buffer.data.data(), buffer.data.size());
	if (!packet) {
		return;
	}
	detail::DataTrackAssembledFrame assembled;
	std::string track_sid;
	std::string publisher_identity;
	{
		std::lock_guard<std::mutex> guard(data_tracks_mutex_);
		auto route = incoming_data_track_routes_.find(packet->track_handle);
		if (route == incoming_data_track_routes_.end()) {
			return;
		}
		track_sid = route->second.track_sid;
		publisher_identity = route->second.publisher_identity;
		const auto limit = data_track_partial_frame_limits_.contains(track_sid)
		                       ? data_track_partial_frame_limits_[track_sid]
		                       : 1;
		auto [depacketizer, inserted] = data_track_depacketizers_.try_emplace(track_sid, limit);
		auto result = depacketizer->second.Push(std::move(*packet));
		if (!result) {
			return;
		}
		assembled = std::move(*result);
	}
	if (assembled.extensions.key_index) {
		std::lock_guard<std::mutex> guard(e2ee_mutex_);
		if (e2ee_manager_ == nullptr || !e2ee_manager_->Enabled() ||
		    assembled.extensions.iv.size() != 12) {
			return;
		}
		E2EEManagerNativeAccess::EncryptedData encrypted{std::move(assembled.frame.payload),
		                                                 assembled.extensions.iv,
		                                                 *assembled.extensions.key_index};
		auto decrypted =
		    E2EEManagerNativeAccess::DecryptData(*e2ee_manager_, publisher_identity, encrypted);
		if (!decrypted) {
			return;
		}
		assembled.frame.payload = std::move(*decrypted);
	}
	if (auto* listener = room_listener_.load()) {
		listener->DataTrackFrameEvent(track_sid, std::move(assembled.frame));
	}
}

void RtcEngine::OnDataChannelBufferedAmountChange(
    const webrtc::scoped_refptr<webrtc::DataChannelInterface>& channel, bool reliable,
    bool monitor_buffer) {
	data_channel_backpressure_.Notify();
	if (monitor_buffer) {
		QueueDataChannelBufferStatusUpdate(channel, reliable);
	}
}

void RtcEngine::QueueDataChannelBufferStatusUpdate(
    webrtc::scoped_refptr<webrtc::DataChannelInterface> channel, bool reliable) {
	{
		std::lock_guard<std::mutex> guard(rpc_tasks_mutex_);
		if (rpc_workers_stopping_) {
			return;
		}
		rpc_tasks_.emplace_back([this, channel = std::move(channel), reliable] {
			UpdateDataChannelBufferStatus(channel, reliable);
		});
	}
	rpc_tasks_cv_.notify_one();
}

} // namespace core
} // namespace livekit
