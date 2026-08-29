/**
 * Copyright (c) 2026 sunze
 * Licensed under the Apache License, Version 2.0.
 */

#pragma once

#ifndef _LKC_CORE_TOKEN_SOURCE_H_
#define _LKC_CORE_TOKEN_SOURCE_H_

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace livekit {
namespace core {

struct TokenSourceFetchOptions {
	std::string room_name;
	std::string participant_name;
	std::string participant_identity;
	std::string participant_metadata;
	std::map<std::string, std::string> participant_attributes;
	std::string agent_name;
	std::string agent_metadata;
	std::string deployment;

	bool operator==(const TokenSourceFetchOptions&) const = default;
};

struct TokenSourceResponse {
	std::string server_url;
	std::string participant_token;
};

struct TokenSourceResult {
	TokenSourceResponse response;
	std::string error;

	explicit operator bool() const {
		return error.empty() && !response.server_url.empty() && !response.participant_token.empty();
	}
};

class TokenSourceInterface {
public:
	virtual ~TokenSourceInterface() = default;
	// Implementations may be called from the SDK recovery thread. A forced fetch must bypass any
	// application-side cache and obtain credentials suitable for a new full connection.
	virtual TokenSourceResult Fetch(const TokenSourceFetchOptions& options,
	                                bool force_refresh = false) = 0;
};

using TokenSourceCallback =
    std::function<TokenSourceResult(const TokenSourceFetchOptions&, bool force_refresh)>;

std::shared_ptr<TokenSourceInterface> CreateLiteralTokenSource(std::string server_url,
                                                               std::string participant_token);
std::shared_ptr<TokenSourceInterface> CreateCallbackTokenSource(TokenSourceCallback callback,
                                                                bool cache = true);

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TOKEN_SOURCE_H_
