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

#pragma once

#ifndef _LKC_CORE_E2EE_KEY_PROVIDER_H_
#define _LKC_CORE_E2EE_KEY_PROVIDER_H_

#include "livekit/core/option/e2ee_option.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace livekit {
namespace core {

enum class KeyProviderErrorCode {
	InvalidKeyIndex,
	EmptyKey,
	EmptyParticipantIdentity,
	KeyNotFound,
	CryptoFailure,
};

struct KeyProviderError {
	KeyProviderErrorCode code = KeyProviderErrorCode::CryptoFailure;
	std::string message;
};

struct KeyOperationResult {
	E2eeKey key;
	std::optional<KeyProviderError> error;

	bool Ok() const noexcept { return !error.has_value(); }
	static KeyOperationResult Success(E2eeKey key = {});
	static KeyOperationResult Failure(KeyProviderErrorCode code, std::string message);
};

// Owns the E2EE chain keys for one room. All operations are thread-safe. Exported key copies are
// owned by the caller and should be cleared when they are no longer needed.
class KeyProvider final {
public:
	explicit KeyProvider(KeyProviderOptions options = {});
	~KeyProvider();

	KeyProvider(const KeyProvider&) = delete;
	KeyProvider& operator=(const KeyProvider&) = delete;
	KeyProvider(KeyProvider&&) = delete;
	KeyProvider& operator=(KeyProvider&&) = delete;

	const KeyProviderOptions& Options() const noexcept;

	KeyOperationResult SetSharedKey(E2eeKey key, std::size_t key_index = 0);
	KeyOperationResult ExportSharedKey(std::size_t key_index = 0) const;
	KeyOperationResult RatchetSharedKey(std::size_t key_index = 0);
	KeyOperationResult RemoveSharedKey(std::size_t key_index = 0);

	KeyOperationResult SetKey(std::string participant_identity, E2eeKey key,
	                          std::size_t key_index = 0);
	KeyOperationResult ExportKey(const std::string& participant_identity,
	                             std::size_t key_index = 0) const;
	KeyOperationResult RatchetKey(const std::string& participant_identity,
	                              std::size_t key_index = 0);
	KeyOperationResult RemoveKey(const std::string& participant_identity,
	                             std::size_t key_index = 0);
	KeyOperationResult RemoveParticipantKeys(const std::string& participant_identity);
	void Clear();

private:
	class Impl;
	friend class KeyProviderNativeAccess;
	std::unique_ptr<Impl> impl_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_E2EE_KEY_PROVIDER_H_
