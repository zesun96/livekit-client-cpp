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

#pragma once

#ifndef _LKC_CORE_OPTION_E2EE_OPTION_H_
#define _LKC_CORE_OPTION_E2EE_OPTION_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace livekit {
namespace core {

enum class EncryptionType {
	None,
	Gcm,
	Custom,
};

enum class KeyDerivationFunction {
	Pbkdf2Sha256,
	HkdfSha256,
};

using E2eeKey = std::vector<std::uint8_t>;

inline constexpr std::string_view kDefaultE2eeRatchetSalt = "LKFrameEncryptionKey";
inline constexpr std::size_t kDefaultE2eeRatchetWindowSize = 16;
inline constexpr int kDefaultE2eeFailureTolerance = -1;
inline constexpr std::size_t kDefaultE2eeKeyRingSize = 16;

struct KeyProviderOptions {
	E2eeKey ratchet_salt{kDefaultE2eeRatchetSalt.begin(), kDefaultE2eeRatchetSalt.end()};
	E2eeKey unencrypted_magic_bytes;
	std::size_t ratchet_window_size = kDefaultE2eeRatchetWindowSize;
	int failure_tolerance = kDefaultE2eeFailureTolerance;
	std::size_t key_ring_size = kDefaultE2eeKeyRingSize;
	KeyDerivationFunction key_derivation = KeyDerivationFunction::Pbkdf2Sha256;
};

struct E2eeOptions {
	EncryptionType encryption_type = EncryptionType::Gcm;
	bool enabled = true;
	KeyProviderOptions key_provider;
	std::optional<E2eeKey> shared_key;
};

} // namespace core
} // namespace livekit

#endif /* _LKC_CORE_OPTION_E2EE_OPTION_H_ */
