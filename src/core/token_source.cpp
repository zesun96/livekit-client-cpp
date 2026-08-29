#include "livekit/core/token_source.h"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace livekit {
namespace core {
namespace {

constexpr std::int64_t kJwtExpiryMarginSeconds = 60;

std::optional<std::string> DecodeBase64Url(std::string input) {
	static constexpr unsigned char kInvalid = 0xff;
	static constexpr char kAlphabet[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (auto& character : input) {
		if (character == '-') {
			character = '+';
		} else if (character == '_') {
			character = '/';
		}
	}
	if (input.size() % 4 == 1) {
		return std::nullopt;
	}
	while (input.size() % 4 != 0) {
		input.push_back('=');
	}
	std::string output;
	output.reserve(input.size() / 4 * 3);
	std::uint32_t accumulator = 0;
	int bits = 0;
	for (const auto character : input) {
		if (character == '=') {
			break;
		}
		unsigned char value = kInvalid;
		for (unsigned char index = 0; index < 64; ++index) {
			if (kAlphabet[index] == character) {
				value = index;
				break;
			}
		}
		if (value == kInvalid) {
			return std::nullopt;
		}
		accumulator = (accumulator << 6) | value;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			output.push_back(static_cast<char>((accumulator >> bits) & 0xff));
		}
	}
	return output;
}

std::optional<std::int64_t> JsonIntegerClaim(const std::string& json, const char* claim) {
	const std::string key = std::string{"\""} + claim + "\"";
	auto position = json.find(key);
	if (position == std::string::npos) {
		return std::nullopt;
	}
	position = json.find(':', position + key.size());
	if (position == std::string::npos) {
		return std::nullopt;
	}
	++position;
	while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {
		++position;
	}
	bool negative = false;
	if (position < json.size() && json[position] == '-') {
		negative = true;
		++position;
	}
	if (position == json.size() || !std::isdigit(static_cast<unsigned char>(json[position]))) {
		return std::nullopt;
	}
	const auto begin = json.data() + position;
	auto end_position = position;
	while (end_position < json.size() &&
	       std::isdigit(static_cast<unsigned char>(json[end_position]))) {
		++end_position;
	}
	std::int64_t value = 0;
	const auto parsed = std::from_chars(begin, json.data() + end_position, value);
	if (parsed.ec != std::errc{} || parsed.ptr != json.data() + end_position) {
		return std::nullopt;
	}
	return negative ? -value : value;
}

bool JwtIsReusable(const std::string& token) {
	const auto first_dot = token.find('.');
	if (first_dot == std::string::npos) {
		return false;
	}
	const auto second_dot = token.find('.', first_dot + 1);
	if (second_dot == std::string::npos) {
		return false;
	}
	auto payload = DecodeBase64Url(token.substr(first_dot + 1, second_dot - first_dot - 1));
	if (!payload.has_value()) {
		return false;
	}
	const auto now = std::chrono::duration_cast<std::chrono::seconds>(
	                     std::chrono::system_clock::now().time_since_epoch())
	                     .count();
	const auto expires = JsonIntegerClaim(*payload, "exp");
	if (!expires.has_value() || *expires <= now + kJwtExpiryMarginSeconds) {
		return false;
	}
	const auto not_before = JsonIntegerClaim(*payload, "nbf");
	return !not_before.has_value() || *not_before <= now;
}

TokenSourceResult Validate(TokenSourceResult result) {
	if (!result.error.empty()) {
		return result;
	}
	if (result.response.server_url.empty()) {
		result.error = "token source returned an empty server URL";
	} else if (result.response.participant_token.empty()) {
		result.error = "token source returned an empty participant token";
	}
	return result;
}

class LiteralTokenSource final : public TokenSourceInterface {
public:
	LiteralTokenSource(std::string url, std::string token)
	    : response_{std::move(url), std::move(token)} {}

	TokenSourceResult Fetch(const TokenSourceFetchOptions&, bool) override {
		return Validate({response_, {}});
	}

private:
	TokenSourceResponse response_;
};

class CallbackTokenSource final : public TokenSourceInterface {
public:
	CallbackTokenSource(TokenSourceCallback callback, bool cache)
	    : callback_(std::move(callback)), cache_(cache) {}

	TokenSourceResult Fetch(const TokenSourceFetchOptions& options, bool force_refresh) override {
		// Serialize callback invocation so simultaneous connection attempts cannot stampede a token
		// endpoint. The callback itself may safely maintain ordinary non-thread-safe state.
		std::lock_guard<std::mutex> guard(mutex_);
		if (cache_ && !force_refresh && cached_.has_value() && cached_options_ == options &&
		    JwtIsReusable(cached_->participant_token)) {
			return {*cached_, {}};
		}
		if (!callback_) {
			return {{}, "token source callback is empty"};
		}
		try {
			auto result = Validate(callback_(options, force_refresh));
			if (result) {
				cached_options_ = options;
				cached_ = result.response;
			}
			return result;
		} catch (const std::exception& error) {
			return {{}, std::string{"token source callback failed: "} + error.what()};
		} catch (...) {
			return {{}, "token source callback failed with an unknown exception"};
		}
	}

private:
	TokenSourceCallback callback_;
	bool cache_;
	std::mutex mutex_;
	TokenSourceFetchOptions cached_options_;
	std::optional<TokenSourceResponse> cached_;
};

} // namespace

std::shared_ptr<TokenSourceInterface> CreateLiteralTokenSource(std::string server_url,
                                                               std::string participant_token) {
	return std::make_shared<LiteralTokenSource>(std::move(server_url),
	                                            std::move(participant_token));
}

std::shared_ptr<TokenSourceInterface> CreateCallbackTokenSource(TokenSourceCallback callback,
                                                                bool cache) {
	return std::make_shared<CallbackTokenSource>(std::move(callback), cache);
}

} // namespace core
} // namespace livekit
