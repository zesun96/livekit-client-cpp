#pragma once

#include "example_logging.h"
#include "livekit/core/livekit_client.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace livekit::examples {

struct ConnectionArguments {
	std::string url;
	std::string token;
};

inline ConnectionArguments ReadConnectionArguments(int argc, char* argv[]) {
	ConnectionArguments result;
	if (argc >= 3) {
		result.url = argv[1];
		result.token = argv[2];
		return result;
	}
	if (const char* value = std::getenv("LIVEKIT_URL")) {
		result.url = value;
	}
	if (const char* value = std::getenv("LIVEKIT_TOKEN")) {
		result.token = value;
	}
	return result;
}

inline bool ValidateConnectionArguments(const ConnectionArguments& arguments,
                                        const char* executable) {
	if (!arguments.url.empty() && !arguments.token.empty()) {
		return true;
	}
	std::cerr << "Usage: " << executable << " <url> <token> [options]\n"
	          << "Alternatively set LIVEKIT_URL and LIVEKIT_TOKEN." << std::endl;
	return false;
}

inline bool WaitUntil(const std::function<bool()>& predicate,
                      std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (predicate()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return predicate();
}

class ClientRuntime {
public:
	ClientRuntime() : log_sink_(ConfigureExampleLogging()), initialized_(livekit::core::Init()) {}
	~ClientRuntime() {
		if (initialized_) {
			livekit::core::Destroy();
		}
		if (livekit::core::GetLogSink() == log_sink_) {
			livekit::core::SetLogSink(nullptr);
		}
	}

	bool initialized() const { return initialized_; }

private:
	std::shared_ptr<livekit::core::LogSinkInterface> log_sink_;
	bool initialized_;
};

} // namespace livekit::examples
