/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_RPC_H_
#define _LKC_CORE_RPC_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace livekit {
namespace core {

enum class RpcErrorCode : uint32_t {
	UnsupportedMethod = 1400,
	RecipientNotFound = 1401,
	RequestPayloadTooLarge = 1402,
	UnsupportedServer = 1403,
	UnsupportedVersion = 1404,
	ApplicationError = 1500,
	ConnectionTimeout = 1501,
	ResponseTimeout = 1502,
	RecipientDisconnected = 1503,
	ResponsePayloadTooLarge = 1504,
	SendFailed = 1505,
};

struct RpcError {
	RpcErrorCode code = RpcErrorCode::ApplicationError;
	std::string message;
	std::string data;

	static RpcError BuiltIn(RpcErrorCode code, std::string data = {});
};

struct RpcResult {
	std::string payload;
	std::optional<RpcError> error;

	bool Ok() const { return !error.has_value(); }
	static RpcResult Success(std::string payload = {}) {
		return {std::move(payload), std::nullopt};
	}
	static RpcResult Failure(RpcError error) { return {{}, std::move(error)}; }
};

struct PerformRpcParams {
	std::string destination_identity;
	std::string method;
	std::string payload;
	std::chrono::milliseconds response_timeout{15'000};
};

struct RpcInvocationData {
	std::string request_id;
	std::string caller_identity;
	std::string payload;
	std::chrono::milliseconds response_timeout;
};

using RpcHandler = std::function<RpcResult(const RpcInvocationData&)>;

constexpr std::size_t kMaximumRpcPayloadBytes = 15'360;
constexpr std::size_t kMaximumRpcErrorMessageBytes = 256;

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_RPC_H_
