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

#include <iostream>
#include <stdexcept>

namespace livekit {
namespace core {

WebsocketClient::WebsocketClient(const WebsocketConnectionOptions& connection_options,
                                 std::string uri)
    : uri_(uri) {

	// lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG, NULL);
	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));

	info.port = CONTEXT_PORT_NO_LISTEN; // We don't run a server
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	info.user =
	    this; // this is used in the callback_wrapper (lws_context_user(lws_get_context(wsi)))

	context_ = lws_create_context(&info);
	if (context_ == NULL)
		throw std::runtime_error("lws context creation failed");
}

WebsocketClient::~WebsocketClient() {
	stop_ = true;
	if (lws_thread_ && lws_thread_->joinable()) {
		lws_thread_->join();
	}
}

void WebsocketClient::connect() {
	static const uint32_t backoff_ms[] = {1000, 2000, 3000, 4000, 5000};
	static const lws_retry_bo_t retry = {
	    .retry_ms_table = backoff_ms, // i dont use this, i think its just for sul w event loops?
	    .retry_ms_table_count = LWS_ARRAY_SIZE(backoff_ms),
	    .conceal_count = 500,

	    .secs_since_valid_ping = 24,   /* force PINGs after secs idle */
	    .secs_since_valid_hangup = 52, /* hangup after secs idle */

	    .jitter_percent = 20,
	};

	ws_uri_ = WebsocketUri::parse_and_validate(uri_);
	std::string ws_relative_url = ws_uri_.get_relative_url();
	struct lws_client_connect_info connect_info = {
	    .context = context_,
	    .address = ws_uri_.get_hostname().c_str(), //"127.0.0.1"
	    .port = ws_uri_.get_port(),                // 7880,
	    .ssl_connection = false,
	    .path = ws_relative_url.c_str(),
	    .host = ws_uri_.get_hostname().c_str(),
	    .origin = ws_uri_.get_hostname().c_str(),
	    .protocol = protocols[0].name,
	    .ietf_version_or_minus_one = -1,
	    //.userdata = static_cast<void*>(this),
	    .retry_and_idle_policy = &retry,
	};

	wsi_ = lws_client_connect_via_info(&connect_info);
	if (wsi_ == NULL)
		throw std::runtime_error("lws connection failed");
}

void WebsocketClient::service() { lws_thread_ = new std::thread(lws_thread, this); }

void WebsocketClient::disconnect() { lws_context_destroy(context_); }

void WebsocketClient::send(std::unique_ptr<WebsocketData> message) {
	if (wsi_ == nullptr) {
		// log::error("Websocket is not connected");
		return;
	}
	{
		std::lock_guard<std::mutex> guard(lock_);
		msg_tx_queue_.push(std::move(message));
	}

	auto res = lws_callback_on_writable(wsi_);
	if (res != 0) {
		// log::error("Failed lws_callback_on_writable: {}", res); // idc
	}
	return;
}

void WebsocketClient::set_recv_cb(const std::function<void(std::shared_ptr<WebsocketData>&)>& cb) {
	func_recv_cb_ = cb;
}

void WebsocketClient::set_event_cb(const std::function<void(enum EventCode, EventReason)>& cb) {
	func_event_cb_ = cb;
}

int WebsocketClient::callback_wrapper(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                                      void* in, size_t len) {
	void* context_user = lws_context_user(lws_get_context(wsi));
	WebsocketClient* client = reinterpret_cast<WebsocketClient*>(context_user);
	return client->happlay_cb(wsi, reason, in, len);
}

int WebsocketClient::happlay_cb(struct lws* wsi, enum lws_callback_reasons reason, void* in,
                                size_t len) {
	switch (reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED: {
		this->conn_established_ = true;
		this->reconnect_attempts_ = 0;
		if (this->func_event_cb_)
			this->func_event_cb_(EventCode::Connected, std::string());
		lws_callback_on_writable(wsi);
		break;
	}
	case LWS_CALLBACK_CLIENT_RECEIVE: {
		// lwsl_info("Received data: %s\n", (char*)in);
		// Handle incoming messages here
		if (this->func_recv_cb_) {
			if (lws_frame_is_binary(wsi)) {
				std::shared_ptr<WebsocketData> data =
				    std::make_shared<WebsocketData>(in, len, WebsocketDataType::Binany);
				this->func_recv_cb_(data);
			} else {
				std::shared_ptr<WebsocketData> data =
				    std::make_shared<WebsocketData>(in, len, WebsocketDataType::Text);
				this->func_recv_cb_(data);
			}
		}

		break;
	}
	case LWS_CALLBACK_WSI_DESTROY: {
		if (this->func_event_cb_)
			this->func_event_cb_(EventCode::DisConnected, std::string());
		break;
	}
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
		if (!this->conn_established_) {
			// fall through to LWS_CALLBACK_CLOSED
		} else {
			break;
		}
	}
	case LWS_CALLBACK_CLIENT_CLOSED: {
		// try to reconnect
		this->conn_established_ = false;
		this->wsi_ = nullptr;
		this->restart_after_ =
		    std::chrono::steady_clock::now() +
		    std::chrono::seconds(std::max(2 * (int)this->reconnect_attempts_, 10));
		break;
	}
	case LWS_CALLBACK_CLIENT_WRITEABLE: {
		{
			std::lock_guard<std::mutex> guard(lock_);
			// check if there are messages in the queue and if connection has been established
			if (msg_tx_queue_.size() > 0 && this->conn_established_) {
				std::unique_ptr<WebsocketData> data = std::move(msg_tx_queue_.front());
				size_t payload_len = data->length;
				unsigned char* payload = (unsigned char*)malloc(LWS_PRE + payload_len);
				if (!payload) {
					lwsl_err("Failed to allocate buffer\n");
					return -1;
				}
				memcpy(payload + LWS_PRE, data->data, payload_len);
				if (data->type == WebsocketDataType::Binany) {
					int write = lws_write(wsi, payload + LWS_PRE, payload_len, LWS_WRITE_BINARY);
				} else {
					int write = lws_write(wsi, payload + LWS_PRE, payload_len, LWS_WRITE_TEXT);
				}
				free(payload);
				msg_tx_queue_.pop();
			}
		}
		break;
	}
	default: {
		break;
	}
	}

	return 0;
}

void WebsocketClient::lws_thread(WebsocketClient* client) {
	while (!client->stop_) {
		lws_service(client->context_, 50);
	}
	if (client->context_ != nullptr) {
		lws_context_destroy(client->context_);
		client->context_ = nullptr;
	}
}

} // namespace core
} // namespace livekit
