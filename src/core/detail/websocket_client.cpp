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

#include "websocket_client.h"
#include "logging.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#if defined(_WIN32) && defined(LWS_WITH_MBEDTLS)
#include <mbedtls/x509_crt.h>
#include <wincrypt.h>
#include <windows.h>
#endif

namespace livekit {
namespace core {

namespace {

constexpr std::size_t kMaxWebsocketMessageBytes = 16U * 1024U * 1024U;

} // namespace

#if defined(_WIN32) && defined(LWS_WITH_MBEDTLS)
namespace {

bool IsMbedTlsCertificateSupported(const CERT_CONTEXT& certificate) {
	mbedtls_x509_crt parsed;
	mbedtls_x509_crt_init(&parsed);
	const int result =
	    mbedtls_x509_crt_parse_der(&parsed, certificate.pbCertEncoded, certificate.cbCertEncoded);
	mbedtls_x509_crt_free(&parsed);
	return result == 0;
}

void LoadWindowsRootCertificates(struct lws* wsi) {
	auto* vhost = lws_get_vhost(wsi);
	HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
	if (store == nullptr) {
		LKC_LOG_ERROR << "failed to open Windows root certificate store: code=" << GetLastError();
		return;
	}

	std::size_t loaded = 0;
	std::size_t rejected = 0;
	PCCERT_CONTEXT certificate = nullptr;
	while ((certificate = CertEnumCertificatesInStore(store, certificate)) != nullptr) {
		// libwebsockets 4.3.3's mbedTLS wrapper frees the complete accumulated CA chain when one
		// certificate fails to parse. Preflight each Windows root independently so an obsolete or
		// unsupported root cannot discard all previously accepted trust anchors.
		if (!IsMbedTlsCertificateSupported(*certificate)) {
			++rejected;
			continue;
		}
		if (lws_tls_client_vhost_extra_cert_mem(vhost, certificate->pbCertEncoded,
		                                        certificate->cbCertEncoded) == 0) {
			++loaded;
		} else {
			++rejected;
		}
	}
	CertCloseStore(store, 0);

	if (loaded == 0) {
		LKC_LOG_ERROR << "Windows root certificate store did not provide a usable TLS root";
	} else {
		LKC_LOG_DEBUG << "loaded Windows TLS root certificates: accepted=" << loaded
		              << ", rejected=" << rejected;
	}
}

} // namespace
#endif

WebsocketClient::WebsocketClient(const WebsocketConnectionOptions& connection_options,
                                 std::string uri)
    : connection_options_(connection_options), uri_(std::move(uri)),
      ws_uri_(WebsocketUri::parse_and_validate(uri_)) {

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));

	info.port = CONTEXT_PORT_NO_LISTEN; // We don't run a server
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	info.pt_serv_buf_size = 32 * 1024;
	info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
	if (ws_uri_.is_secure()) {
		info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	}
	info.user =
	    this; // this is used in the callback_wrapper (lws_context_user(lws_get_context(wsi)))
	/*
	 * since we know this lws context is only ever going to be used with
	 * one client wsis / fds / sockets at a time, let lws know it doesn't
	 * have to use the default allocations for fd tables up to ulimit -n.
	 * It will just allocate for 1 internal and 1 (+ 1 http2 nwsi) that we
	 * will use.
	 */
	info.fd_limit_per_thread = 1 + 1 + 1;

	context_ = lws_create_context(&info);
	if (context_ == NULL) {
		LKC_LOG_ERROR << "failed to create WebSocket context";
		throw std::runtime_error("lws context creation failed");
	}
	LKC_LOG_DEBUG << "WebSocket context created";
}

WebsocketClient::~WebsocketClient() {
	disconnect();
	func_recv_cb_ = nullptr;
	func_event_cb_ = nullptr;
	if (context_ != nullptr) {
		lws_context_destroy(context_);
		context_ = nullptr;
	}
}

void WebsocketClient::connect() {
	LKC_LOG_DEBUG << "starting WebSocket connection";
	static const uint32_t backoff_ms[] = {1000, 2000, 3000, 4000, 5000};
	static const lws_retry_bo_t retry = {
	    .retry_ms_table = backoff_ms, // i dont use this, i think its just for sul w event loops?
	    .retry_ms_table_count = LWS_ARRAY_SIZE(backoff_ms),
	    .conceal_count = 500,

	    .secs_since_valid_ping = 24,   /* force PINGs after secs idle */
	    .secs_since_valid_hangup = 52, /* hangup after secs idle */

	    .jitter_percent = 20,
	};

	std::string ws_relative_url = ws_uri_.get_relative_url();
	struct lws_client_connect_info connect_info = {
	    .context = context_,
	    .address = ws_uri_.get_hostname().c_str(), //"127.0.0.1"
	    .port = ws_uri_.get_port(),                // 7880,
	    .ssl_connection = ws_uri_.is_secure() ? LCCSCF_USE_SSL : 0,
	    .path = ws_relative_url.c_str(),
	    .host = ws_uri_.get_hostname().c_str(),
	    .origin = ws_uri_.get_hostname().c_str(),
	    .protocol = protocols[0].name,
	    .ietf_version_or_minus_one = -1,
	    //.userdata = static_cast<void*>(this),
	    .retry_and_idle_policy = &retry,
	};

	wsi_ = lws_client_connect_via_info(&connect_info);
	if (wsi_ == NULL) {
		LKC_LOG_ERROR << "failed to start WebSocket connection";
		throw std::runtime_error("lws connection failed");
	}
}

void WebsocketClient::service() {
	if (service_thread_.joinable()) {
		throw std::logic_error("WebSocket service is already running");
	}
	stop_ = false;
	service_thread_ = std::thread(&WebsocketClient::run_service_loop, this);
}

void WebsocketClient::disconnect() {
	stop_ = true;
	if (context_ != nullptr) {
		lws_cancel_service(context_);
	}
	if (service_thread_.joinable() && service_thread_.get_id() != std::this_thread::get_id()) {
		service_thread_.join();
	}
}

void WebsocketClient::send(WebsocketData message) {
	if (!conn_established_ || wsi_ == nullptr) {
		LKC_LOG_WARNING << "cannot send because WebSocket is not connected";
		return;
	}
	{
		std::lock_guard<std::mutex> guard(lock_);
		msg_tx_queue_.push(std::move(message));
	}

	const auto res = lws_callback_on_writable(wsi_);
	if (res < 0) {
		LKC_LOG_ERROR << "failed to schedule WebSocket write: code=" << res;
	}
	lws_cancel_service(context_);
	return;
}

bool WebsocketClient::flush(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> guard(lock_);
	return tx_condition_.wait_for(guard, timeout,
	                              [this] { return msg_tx_queue_.empty() && !write_in_progress_; });
}

void WebsocketClient::set_recv_cb(const std::function<void(const WebsocketData&)>& cb) {
	func_recv_cb_ = cb;
}

void WebsocketClient::set_event_cb(const std::function<void(enum EventCode, EventReason)>& cb) {
	func_event_cb_ = cb;
}

int WebsocketClient::callback_wrapper(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                                      void* in, size_t len) {
	void* context_user = lws_context_user(lws_get_context(wsi));
	auto* client = static_cast<WebsocketClient*>(context_user);
	return client->handle_callback(wsi, reason, in, len);
}

int WebsocketClient::handle_callback(struct lws* wsi, enum lws_callback_reasons reason, void* in,
                                     size_t len) {
	switch (reason) {
#if defined(_WIN32) && defined(LWS_WITH_MBEDTLS)
	case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
		LoadWindowsRootCertificates(wsi);
		break;
	}
#endif
	case LWS_CALLBACK_CLIENT_ESTABLISHED: {
		LKC_LOG_INFO << "WebSocket connection established";
		this->conn_established_ = true;
		this->reconnect_attempts_ = 0;
		if (this->func_event_cb_)
			this->func_event_cb_(EventCode::Connected, std::string());
		lws_callback_on_writable(wsi);
		break;
	}
	case LWS_CALLBACK_CLIENT_RECEIVE: {
		const auto type =
		    lws_frame_is_binary(wsi) ? WebsocketDataType::Binary : WebsocketDataType::Text;
		if (lws_is_first_fragment(wsi)) {
			rx_message_buffer_.clear();
			rx_message_type_ = type;
		} else if (rx_message_type_ == WebsocketDataType::Unknown) {
			LKC_LOG_ERROR << "received WebSocket continuation without an initial fragment";
			return -1;
		}
		if (len > kMaxWebsocketMessageBytes - rx_message_buffer_.size()) {
			LKC_LOG_ERROR << "WebSocket message exceeds the configured receive limit";
			rx_message_buffer_.clear();
			rx_message_type_ = WebsocketDataType::Unknown;
			return -1;
		}
		const auto* bytes = static_cast<const std::uint8_t*>(in);
		if (len != 0) {
			rx_message_buffer_.insert(rx_message_buffer_.end(), bytes, bytes + len);
		}
		if (lws_is_final_fragment(wsi) && lws_remaining_packet_payload(wsi) == 0) {
			const WebsocketData data(rx_message_buffer_.data(), rx_message_buffer_.size(),
			                         rx_message_type_);
			rx_message_buffer_.clear();
			rx_message_type_ = WebsocketDataType::Unknown;
			if (this->func_recv_cb_) {
				this->func_recv_cb_(data);
			}
		}

		break;
	}
	case LWS_CALLBACK_WSI_DESTROY: {
		LKC_LOG_DEBUG << "WebSocket instance destroyed";
		if (this->func_event_cb_)
			this->func_event_cb_(EventCode::Disconnected, std::string());
		break;
	}
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
		const std::string connection_error =
		    in == nullptr ? std::string() : std::string(static_cast<const char*>(in));
		LKC_LOG_WARNING << "WebSocket connection error"
		                << (connection_error.empty() ? std::string() : ": " + connection_error);
		if (this->conn_established_) {
			break;
		}
		[[fallthrough]];
	}
	case LWS_CALLBACK_CLIENT_CLOSED: {
		// try to reconnect
		LKC_LOG_WARNING << "WebSocket connection closed";
		this->conn_established_ = false;
		this->wsi_ = nullptr;
		this->rx_message_buffer_.clear();
		this->rx_message_type_ = WebsocketDataType::Unknown;
		this->restart_after_ =
		    std::chrono::steady_clock::now() +
		    std::chrono::seconds(std::max(2 * (int)this->reconnect_attempts_, 10));
		break;
	}
	case LWS_CALLBACK_CLIENT_WRITEABLE: {
		std::optional<WebsocketData> message;
		{
			std::lock_guard<std::mutex> guard(lock_);
			if (!msg_tx_queue_.empty() && this->conn_established_) {
				message.emplace(std::move(msg_tx_queue_.front()));
				msg_tx_queue_.pop();
				write_in_progress_ = true;
			}
		}
		int write_result = 0;
		if (message) {
			std::vector<unsigned char> payload(LWS_PRE + message->size());
			if (!message->empty()) {
				std::copy(message->data(), message->data() + message->size(),
				          payload.begin() + LWS_PRE);
			}
			const auto mode =
			    message->type() == WebsocketDataType::Binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;
			const int written = lws_write(wsi, payload.data() + LWS_PRE, message->size(), mode);
			if (written < 0 || static_cast<std::size_t>(written) != message->size()) {
				LKC_LOG_ERROR << "WebSocket write failed: wrote=" << written
				              << ", expected=" << message->size();
				write_result = -1;
			}
		}
		{
			std::lock_guard<std::mutex> guard(lock_);
			write_in_progress_ = false;
			if (!msg_tx_queue_.empty() && this->conn_established_) {
				lws_callback_on_writable(wsi);
			}
		}
		tx_condition_.notify_all();
		if (write_result != 0) {
			return write_result;
		}
		break;
	}
	default: {
		break;
	}
	}

	return 0;
}

void WebsocketClient::run_service_loop() {
	LKC_LOG_DEBUG << "WebSocket service loop started";
	while (!stop_) {
		lws_service(context_, 10);
	}
	LKC_LOG_DEBUG << "WebSocket service loop stopped";
}

} // namespace core
} // namespace livekit
