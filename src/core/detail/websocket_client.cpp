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
#include <stdexcept>

namespace livekit {
namespace core {

WebsocketClient::WebsocketClient(const WebsocketConnectionOptions& connection_options,
                                 WebsocketUri uri)
    : uri_(uri) {

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));

	info.port = CONTEXT_PORT_NO_LISTEN; // We don't run a server
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	// spdlog::debug("setting this {}", (size_t)this);
	info.user =
	    this; // this is used in the callback_wrapper (lws_context_user(lws_get_context(wsi)))

	context_ = lws_create_context(&info);
	if (context_ == NULL)
		throw std::runtime_error("lws context creation failed");
}

WebsocketClient::~WebsocketClient() {
	if (context_ != nullptr) {
		lws_context_destroy(context_);
		context_ = nullptr;
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

	struct lws_client_connect_info connect_info = {
	    .context = context_,
	    .address = uri_.get_hostname().c_str(), //"127.0.0.1"
	    .port = uri_.get_port(),                // 7880,
	    .ssl_connection = false,
	    .path = "/",
	    .host = uri_.get_hostname().c_str(), // lws_canonical_hostname(context),
	    .origin = uri_.get_hostname().c_str(),
	    .protocol = protocols[0].name,
	    .retry_and_idle_policy = &retry,
	};

	wsi_ = lws_client_connect_via_info(&connect_info);
	if (wsi_ == NULL)
		throw std::runtime_error("lws connection failed");
}

void WebsocketClient::service() {
	auto res = lws_service(context_, -1); // -1 is non-blocking
}

void WebsocketClient::disconnect() { lws_context_destroy(context_); }

void WebsocketClient::send(std::string message) {
	if (wsi_ == nullptr) {
		// log::error("Websocket is not connected");
		return;
	}

	// log::debug("WS Queuing message to send: {}", message);
	msg_tx_queue_.push(message);

	auto res = lws_callback_on_writable(wsi_);
	// if (res != 0) log::error("Failed lws_callback_on_writable: {}", res); //idc
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
		break;
	}
	case LWS_CALLBACK_CLIENT_RECEIVE: {
		// Handle incoming messages here
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
		break;
	}
	default: {
		break;
	}
	}

	// check if there are messages in the queue and if connection has been established
	if (msg_tx_queue_.size() > 0 && this->conn_established_) {
		// log::debug("WS TX Queue size = {} so lws_callback_on_writable", msg_tx_queue.size());
		auto res = lws_callback_on_writable(wsi);
		// log::debug("Queue size: {}, so lws_callback_on_writable(wsi) which returned
		// {}", msg_tx_queue_.size(), res);
	}

	return 0;
}

} // namespace core
} // namespace livekit